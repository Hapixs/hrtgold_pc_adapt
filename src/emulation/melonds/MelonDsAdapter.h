#pragma once

#include "core/emulation/EmulatorCore.h"
#include "core/storage/SaveRepository.h"

class SdlAudioSink;
struct MelonDsTestAccess;

class MelonDsAdapter final : public EmulatorCore {
public:
    MelonDsAdapter(SaveRepository saves, std::shared_ptr<SdlAudioSink> audio,
                   std::filesystem::path dataDirectory);
    ~MelonDsAdapter() override;
    EmulationResult load(const EmulationConfig& config) override;
    EmulationResult start() override;
    EmulationResult pause() override;
    EmulationResult resume() override;
    EmulationResult stop() override;
    void setButtons(DsButtonMask pressed) override;
    std::shared_ptr<const VideoFrame> latestFrame() const override;
    bool isRunning() const noexcept override;
    EmulationResult lastResult() const;

private:
    friend struct MelonDsTestAccess;
    struct Impl;
    std::unique_ptr<Impl> impl_;
    EmulationResult startWorker(bool freeBios);
};
