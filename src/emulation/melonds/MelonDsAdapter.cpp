// SPDX-License-Identifier: GPL-3.0-or-later
#include "emulation/melonds/MelonDsAdapterInternal.h"
#include "emulation/melonds/MelonConversions.h"
#include "emulation/melonds/MelonRomLoader.h"
#include "platform/sdl/SdlAudioSink.h"
#include "NDS.h"
#include <fstream>
#include <cwctype>

namespace {
EmulationResult failure(EmulationErrorCode code, const char* message) { return {code, message}; }
EmulationResult badState() { return failure(EmulationErrorCode::CoreFailure, "The emulator cannot perform this operation in its current state."); }
bool sameSavePath(const std::filesystem::path& first, const std::filesystem::path& second) {
    auto left = first.lexically_normal().generic_wstring();
    auto right = second.lexically_normal().generic_wstring();
#ifdef _WIN32
    // App-generated paths use normal Windows case-insensitive naming. Lexical
    // comparison also works before the hash directory or save file exists.
    std::ranges::transform(left, left.begin(), [](wchar_t c){ return std::towlower(c); });
    std::ranges::transform(right, right.begin(), [](wchar_t c){ return std::towlower(c); });
#endif
    return left == right;
}
}

MelonDsAdapter::Impl::Impl(SaveRepository repository, std::shared_ptr<SdlAudioSink> sink,
                          std::filesystem::path directory)
    : saves(std::move(repository)), audio(std::move(sink)) {
    platform.dataDirectory = std::move(directory);
    platform.sensitiveDirectories = {platform.dataDirectory};
    platform.stop = [this](int reason) { recordStop(reason); };
    platform.save = [this](std::span<const std::uint8_t> bytes) {
        // One complete newest snapshot, never an unbounded queue of patches.
        std::lock_guard lock(mutex);
        pendingSave.emplace(bytes.begin(), bytes.end());
        changed.notify_all();
    };
    platform.firmware = [this](std::span<const std::uint8_t> bytes) {
        std::lock_guard lock(mutex);
        pendingFirmware.emplace(bytes.begin(), bytes.end());
        changed.notify_all();
    };
}
MelonDsAdapter::MelonDsAdapter(SaveRepository saves, std::shared_ptr<SdlAudioSink> audio,
                               std::filesystem::path directory)
    : impl_(std::make_unique<Impl>(std::move(saves), std::move(audio), std::move(directory))) {}
MelonDsAdapter::~MelonDsAdapter() { stop(); }
EmulationResult MelonDsAdapter::load(const EmulationConfig& config) {
    std::lock_guard operation(impl_->operations);
    std::lock_guard lock(impl_->mutex);
    if (impl_->worker.joinable() || impl_->running) return badState();
    auto extension = config.romPath.extension().wstring();
    std::ranges::transform(extension, extension.begin(), [](wchar_t c){ return std::towlower(c); });
    if (!config.useDirectBoot || config.sampleRate != 48000 || !config.romPath.is_absolute() ||
        extension != L".nds" || !config.savePath.is_absolute() || config.romSha256.size() != 64 ||
        !std::ranges::all_of(config.romSha256, [](char c){ return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }) ||
        !impl_->platform.dataDirectory.is_absolute())
        return failure(EmulationErrorCode::InvalidRom, "A local NDS file, valid hash, app data directory and 48 kHz direct boot are required.");
    const auto repositoryPath = impl_->saves.activeSavePath().lexically_normal();
    const auto requestedSave = config.savePath.lexically_normal();
    if (!repositoryPath.is_absolute() || !sameSavePath(requestedSave, repositoryPath) ||
        !sameSavePath(requestedSave.filename(), "game.sav") ||
        !sameSavePath(requestedSave.parent_path().filename(), config.romSha256))
        return failure(EmulationErrorCode::SaveIo, "The save path and ROM hash must match the bound save repository.");
    impl_->config = config;
    // Size checks and repository reads/writes now refer to the same path,
    // including when the caller used an equivalent spelling with '..'.
    impl_->config.savePath = repositoryPath;
    impl_->loaded = true;
    impl_->platform.sensitiveDirectories = {config.romPath.parent_path(), config.savePath.parent_path(), impl_->platform.dataDirectory};
    return {};
}
EmulationResult MelonDsAdapter::start() { return startWorker(false); }
EmulationResult MelonDsAdapter::startWorker(bool freeBios) {
    std::lock_guard operation(impl_->operations);
    std::unique_lock lock(impl_->mutex);
    if (impl_->worker.joinable() || (!freeBios && !impl_->loaded)) return badState();
    impl_->stopRequested = false;
    impl_->paused = false;
    impl_->pauseAcknowledged = false;
    impl_->stopReason = -1;
    impl_->result = {};
    impl_->buttons = 0;
    std::promise<EmulationResult> started;
    auto ready = started.get_future();
    try {
        impl_->worker = std::jthread([this, freeBios, started = std::move(started)](std::stop_token token) mutable {
            impl_->run(token, freeBios, std::move(started));
        });
    } catch (...) {
        impl_->running = false;
        return failure(EmulationErrorCode::CoreFailure, "The emulator worker could not start.");
    }
    lock.unlock();
    const auto result = ready.get();
    // Failed starts release every core resource before returning to the caller.
    if (!result.ok()) impl_->worker.join();
    return result;
}
EmulationResult MelonDsAdapter::pause() {
    std::lock_guard operation(impl_->operations);
    std::unique_lock lock(impl_->mutex);
    if (!impl_->running || impl_->paused) return badState();
    impl_->paused = true;
    impl_->changed.notify_all();
    impl_->changed.wait(lock, [this]{ return impl_->pauseAcknowledged || !impl_->running; });
    return impl_->running ? EmulationResult{} : badState();
}
EmulationResult MelonDsAdapter::resume() {
    std::lock_guard operation(impl_->operations);
    std::lock_guard lock(impl_->mutex);
    if (!impl_->running || !impl_->paused) return badState();
    impl_->paused = false;
    impl_->pauseAcknowledged = false;
    impl_->changed.notify_all();
    return {};
}
EmulationResult MelonDsAdapter::stop() {
    std::lock_guard operation(impl_->operations);
    {
        std::lock_guard lock(impl_->mutex);
        if (impl_->worker.joinable()) {
            impl_->worker.request_stop();
            impl_->stopRequested = true;
        }
        impl_->changed.notify_all();
    }
    if (impl_->worker.joinable()) impl_->worker.join();
    return lastResult();
}
void MelonDsAdapter::setButtons(DsButtonMask buttons) { impl_->buttons.store(buttons); }
std::shared_ptr<const VideoFrame> MelonDsAdapter::latestFrame() const { return impl_->mailbox.latest(); }
bool MelonDsAdapter::isRunning() const noexcept { return impl_->running.load(); }
EmulationResult MelonDsAdapter::lastResult() const { std::lock_guard lock(impl_->mutex); return impl_->result; }
void MelonDsAdapter::Impl::recordStop(int reason) {
    std::lock_guard lock(mutex);
    if (stopReason < 0) stopReason = reason;
    stopRequested = true;
    changed.notify_all();
}
bool MelonDsAdapter::Impl::flushPending() {
    std::optional<std::vector<std::uint8_t>> save, firmware;
    {
        std::lock_guard lock(mutex);
        save.swap(pendingSave);
        firmware.swap(pendingFirmware);
    }
    bool ok = !save || saves.replace(*save) == SaveWriteError::None;
    if (firmware) {
        auto* file = melonDS::Platform::OpenLocalFile("generated-firmware.bin", melonDS::Platform::Write);
        if (!file) ok = false;
        else {
            ok = melonDS::Platform::FileWrite(firmware->data(), 1, firmware->size(), file) == firmware->size() && ok;
            ok = melonDS::Platform::CloseFile(file) && ok;
        }
    }
    if (!ok) {
        std::lock_guard lock(mutex);
        result = failure(EmulationErrorCode::SaveIo, "The emulator could not persist its save data.");
        stopRequested = true;
    }
    return ok;
}
void MelonDsAdapter::Impl::run(std::stop_token stop, bool freeBios, std::promise<EmulationResult> started) {
    bool delivered = false;
    MelonPlatformScope scope(platform);
    try {
        auto nds = std::make_unique<melonDS::NDS>();
        nds->UserData = &platform;
        if (!freeBios) nds->SetNDSCart(loadCartridge(config, saves, &platform));
        nds->Reset();
        if (!freeBios) nds->SetupDirectBoot("game.nds"); // Never propagate a private filename into core logs.
        nds->Start();
        auto frame = std::make_shared<VideoFrame>();
        std::array<std::int16_t, 2048> audioBuffer{};
        const auto cadence = std::chrono::nanoseconds(16715114); // 560190 cycles / 33.513982 MHz
        auto deadline = std::chrono::steady_clock::now();
        running = true;
        started.set_value({});
        delivered = true;
        while (!stop.stop_requested()) {
            if (!flushPending()) break;
            {
                std::unique_lock lock(mutex);
                if (stopRequested) break;
                if (paused) {
                    pauseAcknowledged = true;
                    changed.notify_all();
                    changed.wait(lock, [&]{ return !paused || stopRequested || pendingSave || pendingFirmware; });
                    deadline = std::chrono::steady_clock::now();
                    continue;
                }
            }
            nds->SetKeyMask(toActiveLowKeyMask(buttons.load()));
            nds->RunFrame();
            const auto audioFrames = nds->SPU.ReadOutput(audioBuffer.data(), 1024);
            {
                // Pause is acknowledged only after this block; publication cannot
                // continue after pause() returns. No render-thread lock is taken.
                std::lock_guard lock(mutex);
                if (!paused && !stopRequested) {
                    const auto* top = nds->GPU.Framebuffer[nds->GPU.FrontBuffer][0].get();
                    frame->sequence = ++frameSequence;
                    for (std::size_t i = 0; i < frame->rgba.size(); ++i) frame->rgba[i] = toProjectRgba(top[i]);
                    mailbox.publish(frame);
                    if (audio && audioFrames > 0) audio->submitAudio({audioBuffer.data(), static_cast<std::size_t>(audioFrames)*2});
                }
            }
            deadline += cadence;
            if (deadline < std::chrono::steady_clock::now()) deadline = std::chrono::steady_clock::now();
            std::unique_lock lock(mutex);
            changed.wait_until(lock, deadline, [&]{ return paused || stopRequested || pendingSave || pendingFirmware; });
        }
        nds->Stop();
        flushPending();
        nds.reset(); // joins the core renderer before clearing isRunning
        std::lock_guard lock(mutex);
        if (result.ok()) {
            switch (stopReason) {
            case melonDS::Platform::PowerOff: result.message = "PowerOff"; break;
            case melonDS::Platform::External: result.message = "External"; break;
            case melonDS::Platform::GBAModeNotSupported: result = failure(EmulationErrorCode::CoreFailure, "GBAModeNotSupported"); break;
            case melonDS::Platform::BadExceptionRegion: result = failure(EmulationErrorCode::CoreFailure, "BadExceptionRegion"); break;
            default: result.message = "Unknown"; break;
            }
        }
    } catch (const EmulationResult& error) {
        std::lock_guard lock(mutex);
        result = error;
    } catch (...) {
        std::lock_guard lock(mutex);
        result = failure(EmulationErrorCode::CoreFailure, "The emulator worker failed.");
    }
    {
        std::lock_guard lock(mutex);
        running = false;
        if (!delivered) started.set_value(result);
        changed.notify_all();
    }
}
