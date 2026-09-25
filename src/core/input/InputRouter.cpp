#include "core/input/InputRouter.h"

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
           action == InputAction::Confirm;
}
} // namespace

RouteDecision InputRouter::route(const InputEvent& event, SessionState state) {
    if (state != SessionState::Playing) {
        heldMask_ = 0;
    }

    if (event.repeated && suppressesRepeat(event.action)) {
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
                heldMask_ = 0;
                return {RouteTarget::Application, UiCommand::OpenMenu, 0};
            }
            return {};
        }

        const auto button = dsButtonFor(event.action);
        if (button == 0) {
            return {};
        }
        if (event.phase == InputPhase::Pressed) {
            heldMask_ = static_cast<DsButtonMask>(heldMask_ | button);
        } else {
            heldMask_ = static_cast<DsButtonMask>(heldMask_ & ~button);
        }
        return {RouteTarget::Emulator, UiCommand::None, heldMask_};
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

    return {};
}
