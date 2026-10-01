// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/AppBootstrap.h"
#include "emulation/fake/FakeEmulatorCore.h"
#include "emulation/melonds/MelonDsAdapter.h"
#include "ui/GameSurface.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QGuiApplication>
#include <QStandardPaths>

// The session keeps one stable interface; every successful selection builds a
// new adapter with a repository bound to exactly that selection's save path.
class ConfiguredCore final : public EmulatorCore {
public:
    using Factory = std::function<std::unique_ptr<EmulatorCore>(const EmulationConfig&)>;
    explicit ConfiguredCore(Factory factory) : factory_(std::move(factory)) {}
    EmulationResult load(const EmulationConfig& config) override {
        if (core_) core_->stop();
        core_.reset();
        stoppedResult_ = {};
        try {
            core_ = factory_(config);
            if (core_) return core_->load(config);
        } catch (...) {}
        return {EmulationErrorCode::CoreFailure, "Core construction failed."};
    }
    EmulationResult start() override { return core_->start(); }
    EmulationResult pause() override { return core_->pause(); }
    EmulationResult resume() override { return core_->resume(); }
    EmulationResult stop() override {
        if (!core_) return {};
        const auto result = core_->stop();
        // melonDS stop() joins even when it returns a historical save/core error.
        // Once inactive, cleanup has succeeded; keep the error for the journal.
        stoppedResult_ = result;
        return core_->isRunning() ? result : EmulationResult{};
    }
    void setButtons(DsButtonMask buttons) override { if (core_) core_->setButtons(buttons); }
    std::shared_ptr<const VideoFrame> latestFrame() const override { return core_ ? core_->latestFrame() : nullptr; }
    bool isRunning() const noexcept override { return core_ && core_->isRunning(); }
    const EmulatorCore* active() const noexcept { return core_.get(); }
    EmulationResult stoppedResult() const { return stoppedResult_; }
private:
    Factory factory_;
    std::unique_ptr<EmulatorCore> core_;
    EmulationResult stoppedResult_;
};

namespace {
std::filesystem::path dataBase(const QString& overrideBase) {
    if (!overrideBase.isEmpty()) return overrideBase.toStdWString();
    // main sets the app identity to HeartGoldPCRemastered (no organization
    // subdirectory). AppPaths appends that same application directory once.
    return std::filesystem::path(QStandardPaths::writableLocation(
        QStandardPaths::AppLocalDataLocation).toStdWString()).parent_path();
}
}
AppBootstrap::AppBootstrap(bool fakeMode, const QString& settingsFile, const QString& appDataBase)
    : AppBootstrap(fakeMode, settingsFile, appDataBase, {}) {}

AppBootstrap::AppBootstrap(bool fakeMode, const QString& settingsFile,
                           const QString& appDataBase, Dependencies dependencies)
    : fakeMode_(fakeMode), dependencies_(std::move(dependencies)), paths_(dataBase(appDataBase)),
      settings_(settingsFile.isEmpty()
          ? std::make_unique<QSettings>(QString::fromStdWString((paths_.root() / "settings.ini").wstring()), QSettings::IniFormat)
          : std::make_unique<QSettings>(settingsFile, QSettings::IniFormat)),
      api_(dependencies_.sdl ? dependencies_.sdl : std::make_shared<Sdl3Api>()),
      runtime_(std::make_unique<SdlRuntime>(*api_)), audioSink_(runtime_->audioSink()) {
    auto configured = std::make_unique<ConfiguredCore>([this](const EmulationConfig& config) -> std::unique_ptr<EmulatorCore> {
        if (fakeMode_) return std::make_unique<FakeEmulatorCore>();
        if (dependencies_.createCore) return dependencies_.createCore(config, audioSink_, paths_);
        SaveRepository saves(paths_.saveDirectory(config.romSha256), std::chrono::system_clock::now);
        return std::make_unique<MelonDsAdapter>(std::move(saves), audioSink_, paths_.root());
    });
    core_ = configured.get();
    session_ = std::make_unique<SessionController>(std::move(configured));
    auto validate = dependencies_.validate;
    if (!validate) validate = [this](const std::filesystem::path& path) { return validator_.validate(path); };
    auto picker = dependencies_.pickRom;
    if (!picker) picker = [] {
        return QFileDialog::getOpenFileName(nullptr, QStringLiteral("Choisir une ROM HeartGold"), {},
                                             QStringLiteral("Jeux Nintendo DS (*.nds)"));
    };
    uiState_ = std::make_unique<UiState>(*session_, router_, std::move(validate), paths_, *settings_,
        std::move(picker), [this](float volume) { if (runtime_) runtime_->setVolume(volume); });
    QObject::connect(uiState_.get(), &UiState::errorOccurred, this, &AppBootstrap::recordError);
    QObject::connect(uiState_.get(), &UiState::privateFilesRequested, this, [this] {
        if (dependencies_.pickPrivateFiles) dependencies_.pickPrivateFiles();
        else {
            // The vertical prototype boots with FreeBIOS. This recovery picker
            // is reserved for a future private-system-file mode; no copy/store.
            QFileDialog::getOpenFileNames(nullptr, QStringLiteral("Sélectionner les fichiers système DS"), {},
                                          QStringLiteral("Fichiers système DS (*.bin)"));
        }
    });
    QObject::connect(runtime_.get(), &SdlRuntime::inputEvent, uiState_.get(),
                     [this](InputEvent event) { uiState_->handleInput(event); });
    QObject::connect(runtime_.get(), &SdlRuntime::activeDeviceChanged, uiState_.get(),
                     [this](InputDeviceKind kind) { uiState_->setActiveDevice(kind); });
    QObject::connect(runtime_.get(), &SdlRuntime::audioError, uiState_.get(), [this] {
        uiState_->reportEmulationError({EmulationErrorCode::AudioUnavailable, {}});
    });
    QObject::connect(uiState_.get(), &UiState::sessionStateChanged, this, [this] {
        if (uiState_->sessionState() == SessionState::Loading) {
            reportedError_ = EmulationErrorCode::None;
            frames_ = std::make_shared<FrameMailbox>();
            if (surface_) surface_->setFrameMailbox(frames_);
        }
    });
    pumpTimer_.setInterval(8);
    QObject::connect(&pumpTimer_, &QTimer::timeout, runtime_.get(), &SdlRuntime::pump);
    frameTimer_.setInterval(16);
    QObject::connect(&frameTimer_, &QTimer::timeout, this, [this] {
        pollCore();
        const auto frame = core_->latestFrame();
        if (frames_ && frame) frames_->publish(frame);
        if (fakeMode_ && !fakeMenuOpened_ && frame && session_->state() == SessionState::Playing) {
            fakeMenuOpened_ = true;
            session_->openMenu();
        }
    });
    QObject::connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this, &AppBootstrap::shutdown);
    QObject::connect(qGuiApp, &QGuiApplication::applicationStateChanged, this,
        [this](Qt::ApplicationState state) {
            applicationActive_ = state == Qt::ApplicationActive;
            uiState_->setApplicationActive(applicationActive_ && windowActive_);
        });
}

AppBootstrap::~AppBootstrap() {
    shutdown();
    if (root_) root_->removeEventFilter(this);
    uiState_.reset(); // detaches the session callbacks before its owner is freed
    session_.reset(); // destroys/join-checks core before releasing the audio sink
    audioSink_.reset();
}
void AppBootstrap::attachSurface(QObject* root) {
    if (root_) root_->removeEventFilter(this);
    root_ = root;
    if (root_) root_->installEventFilter(this);
    surface_ = root ? root->findChild<GameSurface*>(QStringLiteral("gameSurface")) : nullptr;
    if (surface_) surface_->setFrameMailbox(frames_);
}
bool AppBootstrap::eventFilter(QObject* watched, QEvent* event) {
    if (watched == root_ && event->type() == QEvent::Close) shutdown();
    if (watched == root_ && (event->type() == QEvent::WindowDeactivate ||
                            event->type() == QEvent::WindowActivate)) {
        windowActive_ = event->type() == QEvent::WindowActivate;
        uiState_->setApplicationActive(applicationActive_ && windowActive_);
    }
    return QObject::eventFilter(watched, event);
}
void AppBootstrap::start(const QString& romPath) {
    if (started_ || shutDown_) return;
    started_ = true;
    pumpTimer_.start(); frameTimer_.start();
    if (fakeMode_) {
        const std::string hash(64, '0');
        session_->load({std::filesystem::path{L"C:/fake/heartgold.nds"},
                        paths_.saveDirectory(hash) / "game.sav", hash});
    } else if (!romPath.isEmpty()) {
        uiState_->loadRomPath(romPath, false);
    } else {
        uiState_->restoreLastGame();
    }
}
void AppBootstrap::shutdown() {
    if (shutDown_) return;
    shutDown_ = true;
    frameTimer_.stop(); pumpTimer_.stop();
    pollCore();
    uiState_->stopSession();
    // A stop may discover a final save error while flushing. Log its code only.
    if (!core_->stoppedResult().ok()) recordError(static_cast<int>(core_->stoppedResult().code));
    runtime_.reset(); // worker has joined; now audio/gamepads may close
}
void AppBootstrap::pollCore() {
    if (!core_->active() || (session_->state() != SessionState::Playing &&
                            session_->state() != SessionState::MenuOpen)) return;
    EmulationResult result;
    if (dependencies_.coreResult) result = dependencies_.coreResult(*core_->active());
    else if (const auto* melon = dynamic_cast<const MelonDsAdapter*>(core_->active())) result = melon->lastResult();
    if (!result.ok() && result.code != reportedError_) {
        reportedError_ = result.code;
        uiState_->reportEmulationError(result);
    } else if (result.ok() && !core_->isRunning()) {
        uiState_->stopSession();
    }
}
void AppBootstrap::recordError(int code) {
    static constexpr const char* names[]{"None", "InvalidRom", "MissingPrivateFile", "SaveIo", "AudioUnavailable", "CoreFailure"};
    if (code <= 0 || code >= int(std::size(names))) return;
    const auto directory = QString::fromStdWString(paths_.root().wstring());
    if (!QDir().mkpath(directory)) return;
    const auto filename = directory + QStringLiteral("/application.log");
    QFile log(filename);
    if (log.size() > 64 * 1024) {
        QFile::remove(filename + QStringLiteral(".1"));
        log.rename(filename + QStringLiteral(".1"));
        log.setFileName(filename);
    }
    if (log.open(QIODevice::WriteOnly | QIODevice::Append))
        log.write(QDateTime::currentDateTimeUtc().toString(Qt::ISODate).toUtf8() + " " + names[code] + "\n");
}
