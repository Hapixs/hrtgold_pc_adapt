#include "core/session/SessionController.h"

#include <cassert>
#include <utility>

SessionController::SessionController(std::unique_ptr<EmulatorCore> core)
    : core_(std::move(core)), owningThread_(std::this_thread::get_id()) {
    assert(core_);
}

EmulationResult SessionController::load(const EmulationConfig& config) {
    assertOwningThread();
    const auto canRetryInactiveLoad =
        state_ == SessionState::Error && !coreMayNeedStop_;
    if (state_ != SessionState::NoRom && !canRetryInactiveLoad) {
        return rejected("A ROM can be loaded only when no session is active.");
    }

    acceptsDsInput_ = false;
    publishState(SessionState::Loading);
    auto result = core_->load(config);
    if (!result.ok()) {
        coreMayNeedStop_ = false;
        return publishCoreError(result);
    }

    // Tentatively require cleanup while start runs; if it fails, isRunning()
    // classifies whether partially active resources still require a stop.
    coreMayNeedStop_ = true;
    result = core_->start();
    if (!result.ok()) {
        coreMayNeedStop_ = core_->isRunning();
        return publishCoreError(result);
    }

    acceptsDsInput_ = true;
    publishState(SessionState::Playing);
    return result;
}

EmulationResult SessionController::openMenu() {
    assertOwningThread();
    if (state_ != SessionState::Playing) {
        return rejected("The menu can be opened only while a session is playing.");
    }

    acceptsDsInput_ = false;
    core_->setButtons(0);
    const auto result = core_->pause();
    if (!result.ok()) {
        return publishCoreError(result);
    }

    publishState(SessionState::MenuOpen);
    return result;
}

EmulationResult SessionController::closeMenu() {
    assertOwningThread();
    if (state_ != SessionState::MenuOpen) {
        return rejected("The menu can be closed only while it is open.");
    }

    core_->setButtons(0);
    const auto result = core_->resume();
    if (!result.ok()) {
        return publishCoreError(result);
    }

    acceptsDsInput_ = true;
    publishState(SessionState::Playing);
    return result;
}

EmulationResult SessionController::stop() {
    assertOwningThread();
    if (state_ == SessionState::Error && !coreMayNeedStop_) {
        acceptsDsInput_ = false;
        publishState(SessionState::NoRom);
        return {};
    }
    const auto canCleanUpError =
        state_ == SessionState::Error && coreMayNeedStop_;
    if (state_ != SessionState::Playing && state_ != SessionState::MenuOpen &&
        !canCleanUpError) {
        return rejected("A session can be stopped only while playing or paused.");
    }

    acceptsDsInput_ = false;
    publishState(SessionState::Stopping);
    core_->setButtons(0);
    const auto result = core_->stop();
    if (!result.ok()) {
        return publishCoreError(result);
    }

    coreMayNeedStop_ = false;
    publishState(SessionState::NoRom);
    return result;
}

void SessionController::setDsButtons(DsButtonMask pressed) {
    assertOwningThread();
    if (state_ == SessionState::Playing && acceptsDsInput_) {
        core_->setButtons(pressed);
    }
}

SessionState SessionController::state() const noexcept {
    return state_;
}

void SessionController::onStateChanged(std::function<void(SessionState)> callback) {
    assertOwningThread();
    stateChangedCallback_ = std::move(callback);
}

void SessionController::onError(
    std::function<void(const EmulationResult&)> callback) {
    assertOwningThread();
    errorCallback_ = std::move(callback);
}

void SessionController::assertOwningThread() const noexcept {
    assert(std::this_thread::get_id() == owningThread_);
}

void SessionController::publishState(SessionState state) {
    state_ = state;
    if (stateChangedCallback_) {
        stateChangedCallback_(state_);
    }
}

EmulationResult SessionController::publishCoreError(const EmulationResult& result) {
    publishState(SessionState::Error);
    if (errorCallback_) {
        errorCallback_(result);
    }
    return result;
}

EmulationResult SessionController::rejected(const char* message) {
    return EmulationResult{EmulationErrorCode::CoreFailure, message};
}
