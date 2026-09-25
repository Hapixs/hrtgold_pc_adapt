#pragma once

#include "core/input/InputTypes.h"
#include "core/session/SessionController.h"

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
    RouteDecision route(const InputEvent& event, SessionState state);

private:
    DsButtonMask heldMask_{0};
};
