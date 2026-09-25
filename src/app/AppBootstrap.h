#pragma once

#include "core/input/InputRouter.h"
#include "core/rom/RomValidator.h"
#include "core/storage/AppPaths.h"
#include "platform/sdl/SdlApi.h"
#include "platform/sdl/SdlRuntime.h"
#include "ui/UiState.h"

#include <QPointer>
#include <QSettings>
#include <QTimer>

#include <memory>

class FakeEmulatorCore;
class GameSurface;

class AppBootstrap final {
public:
    explicit AppBootstrap(bool fakeMode);
    ~AppBootstrap();

    AppBootstrap(const AppBootstrap&) = delete;
    AppBootstrap& operator=(const AppBootstrap&) = delete;

    UiState* uiState() const noexcept { return uiState_.get(); }
    void attachSurface(QObject* root);
    void start();

private:
    bool fakeMode_;
    Sdl3Api api_;
    SdlRuntime runtime_;
    std::shared_ptr<SdlAudioSink> audioSink_;
    FakeEmulatorCore* fakeCore_{nullptr};
    std::unique_ptr<SessionController> session_;
    InputRouter router_;
    RomValidator validator_;
    AppPaths paths_;
    QSettings settings_;
    std::unique_ptr<UiState> uiState_;
    QTimer pumpTimer_;
    QTimer frameTimer_;
    QPointer<GameSurface> surface_;
    bool fakeMenuOpened_{false};
};
