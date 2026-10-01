#include "core/input/InputRouter.h"

#include <cstddef>

namespace {
DsButtonMask dsButtonFor(InputAction action) noexcept {
    switch (action) {
    case InputAction::MoveUp:
        return toMask(DsButton::Up);
    case InputAction::MoveDown:
        return toMask(DsButton::Down);
    case InputAction::MoveLeft:
        return toMask(DsButton::Left);
    case InputAction::MoveRight:
        return toMask(DsButton::Right);
    case InputAction::Confirm:
        return toMask(DsButton::A);
    case InputAction::Back:
        return toMask(DsButton::B);
    case InputAction::OpenMenu:
    case InputAction::ToggleFullscreen:
        return 0;
    }
    return 0;
}

UiCommand overlayCommandFor(InputAction action) noexcept {
    switch (action) {
    case InputAction::MoveUp:
        return UiCommand::MoveUp;
    case InputAction::MoveDown:
        return UiCommand::MoveDown;
    case InputAction::MoveLeft:
        return UiCommand::MoveLeft;
    case InputAction::MoveRight:
        return UiCommand::MoveRight;
    case InputAction::Confirm:
        return UiCommand::Confirm;
    case InputAction::Back:
        return UiCommand::Back;
    case InputAction::OpenMenu:
    case InputAction::ToggleFullscreen:
        return UiCommand::None;
    }
    return UiCommand::None;
}

bool suppressesRepeat(InputAction action) noexcept {
    return action == InputAction::OpenMenu || action == InputAction::Back ||
           action == InputAction::Confirm || action == InputAction::ToggleFullscreen;
}

std::size_t deviceIndex(InputDeviceKind device) noexcept {
    switch (device) {
    case InputDeviceKind::Keyboard:
        return 0;
    case InputDeviceKind::Xbox:
        return 1;
    case InputDeviceKind::PlayStation:
        return 2;
    case InputDeviceKind::GenericGamepad:
        return 3;
    }
    return 0;
}
} // namespace

void InputRouter::synchronizeSessionState(SessionState state) noexcept {
    if (state != SessionState::Playing) {
        clearHeldButtons();
    }
}

RouteDecision InputRouter::route(const InputEvent& event, SessionState state) {
    // DS buttons are held state, not repeated commands. Only a fresh physical
    // press may arm them after a menu/focus transition cleared the masks.
    if (event.repeated && (state == SessionState::Playing || suppressesRepeat(event.action))) {
        return {};
    }

    if (event.action == InputAction::ToggleFullscreen) {
        if (event.phase == InputPhase::Pressed) {
            return {RouteTarget::Application, UiCommand::ToggleFullscreen, 0};
        }
        return {};
    }

    if (state == SessionState::Playing) {
        if (event.action == InputAction::OpenMenu) {
            if (event.phase == InputPhase::Pressed) {
                clearHeldButtons();
                return {RouteTarget::Application, UiCommand::OpenMenu, 0};
            }
            return {};
        }

        const auto button = dsButtonFor(event.action);
        if (button == 0) {
            return {};
        }
        auto& deviceMask = heldMasks_[deviceIndex(event.device)];
        if (event.phase == InputPhase::Pressed) {
            deviceMask = static_cast<DsButtonMask>(deviceMask | button);
        } else {
            deviceMask = static_cast<DsButtonMask>(deviceMask & ~button);
        }
        return {RouteTarget::Emulator, UiCommand::None, combinedMask()};
    }

    if (state == SessionState::MenuOpen && event.phase == InputPhase::Pressed) {
        if (event.action == InputAction::OpenMenu) {
            return {RouteTarget::Application, UiCommand::CloseMenu, 0};
        }
        const auto command = overlayCommandFor(event.action);
        if (command != UiCommand::None) {
            return {RouteTarget::Overlay, command, 0};
        }
    }

    if ((state == SessionState::NoRom || state == SessionState::Error) &&
        event.action == InputAction::Confirm && event.phase == InputPhase::Pressed) {
        return {RouteTarget::Application, UiCommand::ChooseRom, 0};
    }

    return {};
}

DsButtonMask InputRouter::combinedMask() const noexcept {
    DsButtonMask combined = 0;
    for (const auto mask : heldMasks_) {
        combined = static_cast<DsButtonMask>(combined | mask);
    }
    return combined;
}

void InputRouter::clearHeldButtons() noexcept {
    heldMasks_.fill(0);
}
