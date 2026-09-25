#pragma once

#include "core/input/InputTypes.h"
#include "core/session/SessionController.h"

#include <array>

enum class RouteTarget { None, Emulator, Overlay, Application };

enum class UiCommand {
    None,
    OpenMenu,
    CloseMenu,
    MoveUp,
    MoveDown,
    MoveLeft,
    MoveRight,
    Confirm,
    Back,
    ToggleFullscreen,
};

struct RouteDecision {
    RouteTarget target{RouteTarget::None};
    UiCommand command{UiCommand::None};
    DsButtonMask dsMask{0};
};

class InputRouter {
public:
    void synchronizeSessionState(SessionState state) noexcept;
    RouteDecision route(const InputEvent& event, SessionState state);

private:
    DsButtonMask combinedMask() const noexcept;
    void clearHeldButtons() noexcept;

    std::array<DsButtonMask, 4> heldMasks_{};
};
