#pragma once

#include "core/input/InputRouter.h"
#include "core/rom/RomValidator.h"
#include "core/storage/AppPaths.h"
#include "core/emulation/FrameMailbox.h"
#include "platform/sdl/SdlApi.h"
#include "platform/sdl/SdlRuntime.h"
#include "ui/UiState.h"
#include <QPointer>
#include <QSettings>
#include <QTimer>
#include <memory>

class GameSurface;
class ConfiguredCore;

class AppBootstrap final : public QObject {
public:
    struct Dependencies {
        std::shared_ptr<SdlApi> sdl;
        UiState::Validator validate;
        UiState::RomPicker pickRom;
        std::function<void()> pickPrivateFiles;
        std::function<std::unique_ptr<EmulatorCore>(const EmulationConfig&,
            std::shared_ptr<SdlAudioSink>, const AppPaths&)> createCore;
        std::function<EmulationResult(const EmulatorCore&)> coreResult;
    };
    explicit AppBootstrap(bool fakeMode, const QString& settingsFile = {},
                          const QString& appDataBase = {});
    AppBootstrap(bool fakeMode, const QString& settingsFile,
                 const QString& appDataBase, Dependencies dependencies);
    ~AppBootstrap() override;
    AppBootstrap(const AppBootstrap&) = delete;
    AppBootstrap& operator=(const AppBootstrap&) = delete;

    UiState* uiState() const noexcept { return uiState_.get(); }
    void attachSurface(QObject* root);
    void start(const QString& romPath = {});
    void shutdown();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void pollCore();
    void recordError(int code);

    bool fakeMode_;
    Dependencies dependencies_;
    AppPaths paths_;
    RomValidator validator_;
    std::unique_ptr<QSettings> settings_;
    std::shared_ptr<SdlApi> api_;
    std::unique_ptr<SdlRuntime> runtime_;
    std::shared_ptr<SdlAudioSink> audioSink_;
    ConfiguredCore* core_{nullptr}; // owned by session_
    std::unique_ptr<SessionController> session_;
    InputRouter router_;
    std::unique_ptr<UiState> uiState_;
    QTimer pumpTimer_;
    QTimer frameTimer_;
    QPointer<GameSurface> surface_;
    QPointer<QObject> root_;
    std::shared_ptr<FrameMailbox> frames_;
    bool fakeMenuOpened_{false};
    bool started_{false};
    bool shutDown_{false};
    EmulationErrorCode reportedError_{EmulationErrorCode::None};
};
