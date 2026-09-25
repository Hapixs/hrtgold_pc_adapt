#include "app/AppBootstrap.h"

#include "emulation/fake/FakeEmulatorCore.h"
#include "ui/GameSurface.h"

#include <QStandardPaths>

AppBootstrap::AppBootstrap(bool fakeMode)
    : fakeMode_(fakeMode), runtime_(api_), audioSink_(runtime_.audioSink()),
      paths_(qEnvironmentVariable("LOCALAPPDATA",
                                  QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation))
                 .toStdWString()),
      settings_(QSettings::IniFormat, QSettings::UserScope,
                QStringLiteral("HeartGoldPCRemastered"),
                QStringLiteral("HeartGoldPCRemastered")) {
    auto core = std::make_unique<FakeEmulatorCore>();
    fakeCore_ = core.get();
    session_ = std::make_unique<SessionController>(std::move(core));
    uiState_ = std::make_unique<UiState>(
        *session_, router_, validator_, paths_, settings_, UiState::RomPicker{},
        [this](float volume) { runtime_.setVolume(volume); });

    QObject::connect(&runtime_, &SdlRuntime::inputEvent, uiState_.get(),
                     [this](InputEvent event) { uiState_->handleInput(event); });
    QObject::connect(&runtime_, &SdlRuntime::activeDeviceChanged, uiState_.get(),
                     [this](InputDeviceKind kind) { uiState_->setActiveDevice(kind); });
    pumpTimer_.setInterval(8);
    QObject::connect(&pumpTimer_, &QTimer::timeout, &runtime_, &SdlRuntime::pump);
    frameTimer_.setInterval(16);
    QObject::connect(&frameTimer_, &QTimer::timeout, uiState_.get(), [this] {
        const auto frame = fakeCore_->latestFrame();
        if (surface_ && frame) surface_->acceptFrame(frame);
        if (fakeMode_ && !fakeMenuOpened_ && frame &&
            session_->state() == SessionState::Playing) {
            fakeMenuOpened_ = true;
            session_->openMenu();
        }
    });
}

AppBootstrap::~AppBootstrap() {
    frameTimer_.stop();
    pumpTimer_.stop();
    if (session_ && (session_->state() == SessionState::Playing ||
                     session_->state() == SessionState::MenuOpen ||
                     session_->state() == SessionState::Error)) {
        session_->stop();
    }
    uiState_.reset();
    session_.reset();
    audioSink_.reset();
}

void AppBootstrap::attachSurface(QObject* root) {
    surface_ = root ? root->findChild<GameSurface*>(QStringLiteral("gameSurface")) : nullptr;
    if (surface_) surface_->acceptFrame(fakeCore_->latestFrame());
}

void AppBootstrap::start() {
    pumpTimer_.start();
    frameTimer_.start();
    if (!fakeMode_) return;
    const EmulationConfig config{
        std::filesystem::path{L"C:/fake/heartgold.nds"},
        std::filesystem::path{L"C:/fake/native.sav"}, std::string(64, '0')};
    session_->load(config);
}
