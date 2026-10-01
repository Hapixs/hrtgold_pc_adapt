#pragma once

#include "core/emulation/EmulationTypes.h"

enum class InputAction {
    MoveUp,
    MoveDown,
    MoveLeft,
    MoveRight,
    Confirm,
    Back,
    OpenMenu,
    ToggleFullscreen,
};

enum class InputPhase { Pressed, Released };

enum class InputDeviceKind { Keyboard, Xbox, PlayStation, GenericGamepad };

struct InputEvent {
    InputAction action;
    InputPhase phase;
    InputDeviceKind device;
    bool repeated{false};
};

constexpr DsButtonMask toMask(DsButton button) noexcept {
    return static_cast<DsButtonMask>(button);
}
