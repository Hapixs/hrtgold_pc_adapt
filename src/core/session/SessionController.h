#pragma once

#include "core/emulation/EmulatorCore.h"

#include <functional>
#include <memory>
#include <thread>

enum class SessionState { NoRom, Loading, Playing, MenuOpen, Stopping, Error };

class SessionController {
public:
    explicit SessionController(std::unique_ptr<EmulatorCore> core);

    EmulationResult load(const EmulationConfig& config);
    EmulationResult openMenu();
    EmulationResult closeMenu();
    EmulationResult stop();
    void setDsButtons(DsButtonMask pressed);
    SessionState state() const noexcept;
    void onStateChanged(std::function<void(SessionState)> callback);
    void onError(std::function<void(const EmulationResult&)> callback);

private:
    void assertOwningThread() const noexcept;
    void publishState(SessionState state);
    EmulationResult publishCoreError(const EmulationResult& result);
    static EmulationResult rejected(const char* message);

    std::unique_ptr<EmulatorCore> core_;
    std::thread::id owningThread_;
    SessionState state_{SessionState::NoRom};
    bool coreMayNeedStop_{false};
    bool acceptsDsInput_{false};
    std::function<void(SessionState)> stateChangedCallback_;
    std::function<void(const EmulationResult&)> errorCallback_;
};
