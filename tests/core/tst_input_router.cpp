#include "core/input/InputRouter.h"

#include <QtTest>

class InputRouterTest final : public QObject {
    Q_OBJECT

private slots:
    void routesPlayingActionsExclusively() {
        InputRouter router;

        const auto movement = router.route(
            {InputAction::MoveUp, InputPhase::Pressed, InputDeviceKind::Keyboard},
            SessionState::Playing);
        QCOMPARE(movement.target, RouteTarget::Emulator);
        QCOMPARE(movement.command, UiCommand::None);
        QCOMPARE(movement.dsMask, toMask(DsButton::Up));

        const auto menu = router.route(
            {InputAction::OpenMenu, InputPhase::Pressed, InputDeviceKind::Keyboard},
            SessionState::Playing);
        QCOMPARE(menu.target, RouteTarget::Application);
        QCOMPARE(menu.command, UiCommand::OpenMenu);
        QCOMPARE(menu.dsMask, DsButtonMask{0});

        const auto fullscreen = router.route(
            {InputAction::ToggleFullscreen, InputPhase::Pressed,
             InputDeviceKind::Keyboard},
            SessionState::Playing);
        QCOMPARE(fullscreen.target, RouteTarget::Application);
        QCOMPARE(fullscreen.command, UiCommand::ToggleFullscreen);
        QCOMPARE(fullscreen.dsMask, DsButtonMask{0});
    }

    void composesAndReleasesAPressedPositiveMask() {
        InputRouter router;

        const auto up = router.route(
            {InputAction::MoveUp, InputPhase::Pressed, InputDeviceKind::Xbox},
            SessionState::Playing);
        const auto upLeft = router.route(
            {InputAction::MoveLeft, InputPhase::Pressed, InputDeviceKind::Xbox},
            SessionState::Playing);
        const auto left = router.route(
            {InputAction::MoveUp, InputPhase::Released, InputDeviceKind::Xbox},
            SessionState::Playing);

        QCOMPARE(up.dsMask, toMask(DsButton::Up));
        QCOMPARE(upLeft.dsMask,
                 static_cast<DsButtonMask>(toMask(DsButton::Up) |
                                           toMask(DsButton::Left)));
        QCOMPARE(left.dsMask, toMask(DsButton::Left));
    }

    void mapsConfirmAndBackToDsButtonsWhilePlaying() {
        InputRouter router;

        const auto confirm = router.route(
            {InputAction::Confirm, InputPhase::Pressed, InputDeviceKind::PlayStation},
            SessionState::Playing);
        const auto both = router.route(
            {InputAction::Back, InputPhase::Pressed, InputDeviceKind::PlayStation},
            SessionState::Playing);
        const auto back = router.route(
            {InputAction::Confirm, InputPhase::Released, InputDeviceKind::PlayStation},
            SessionState::Playing);

        QCOMPARE(confirm.dsMask, toMask(DsButton::A));
        QCOMPARE(both.dsMask,
                 static_cast<DsButtonMask>(toMask(DsButton::A) | toMask(DsButton::B)));
        QCOMPARE(back.dsMask, toMask(DsButton::B));
    }

    void routesMenuActionsOnlyToTheOverlay() {
        InputRouter router;
        const auto actions = {InputAction::MoveUp, InputAction::MoveDown,
                              InputAction::MoveLeft, InputAction::MoveRight,
                              InputAction::Confirm, InputAction::Back};
        const auto commands = {UiCommand::MoveUp, UiCommand::MoveDown,
                               UiCommand::MoveLeft, UiCommand::MoveRight,
                               UiCommand::Confirm, UiCommand::Back};
        auto command = commands.begin();

        for (const auto action : actions) {
            const auto decision = router.route(
                {action, InputPhase::Pressed, InputDeviceKind::GenericGamepad},
                SessionState::MenuOpen);
            QCOMPARE(decision.target, RouteTarget::Overlay);
            QCOMPARE(decision.command, *command++);
            QCOMPARE(decision.dsMask, DsButtonMask{0});
        }
    }

    void menuContextClearsHeldDsStateWithoutLeakingAButtonDecision() {
        InputRouter router;
        router.route({InputAction::MoveLeft, InputPhase::Pressed,
                      InputDeviceKind::Keyboard},
                     SessionState::Playing);

        const auto overlay = router.route(
            {InputAction::MoveUp, InputPhase::Pressed, InputDeviceKind::Xbox},
            SessionState::MenuOpen);
        const auto resumed = router.route(
            {InputAction::MoveDown, InputPhase::Pressed, InputDeviceKind::Keyboard},
            SessionState::Playing);

        QCOMPARE(overlay.target, RouteTarget::Overlay);
        QCOMPARE(overlay.dsMask, DsButtonMask{0});
        QCOMPARE(resumed.dsMask, toMask(DsButton::Down));
    }

    void openMenuTogglesCloseWhileTheOverlayIsOpen() {
        InputRouter router;

        const auto decision = router.route(
            {InputAction::OpenMenu, InputPhase::Pressed, InputDeviceKind::Xbox},
            SessionState::MenuOpen);

        QCOMPARE(decision.target, RouteTarget::Application);
        QCOMPARE(decision.command, UiCommand::CloseMenu);
        QCOMPARE(decision.dsMask, DsButtonMask{0});
    }

    void ignoresReleasedOverlayActionsAndSuppressedRepeats() {
        InputRouter router;
        const auto released = router.route(
            {InputAction::MoveRight, InputPhase::Released, InputDeviceKind::Keyboard},
            SessionState::MenuOpen);
        const auto repeatedMenu = router.route(
            {InputAction::OpenMenu, InputPhase::Pressed, InputDeviceKind::Keyboard, true},
            SessionState::Playing);
        const auto repeatedConfirm = router.route(
            {InputAction::Confirm, InputPhase::Pressed, InputDeviceKind::Xbox, true},
            SessionState::MenuOpen);
        const auto repeatedBack = router.route(
            {InputAction::Back, InputPhase::Pressed, InputDeviceKind::Xbox, true},
            SessionState::MenuOpen);
        const auto repeatedMovement = router.route(
            {InputAction::MoveRight, InputPhase::Pressed, InputDeviceKind::Xbox, true},
            SessionState::MenuOpen);

        QCOMPARE(released.target, RouteTarget::None);
        QCOMPARE(repeatedMenu.target, RouteTarget::None);
        QCOMPARE(repeatedConfirm.target, RouteTarget::None);
        QCOMPARE(repeatedBack.target, RouteTarget::None);
        QCOMPARE(repeatedMovement.target, RouteTarget::Overlay);
        QCOMPARE(repeatedMovement.command, UiCommand::MoveRight);
    }

    void ignoresGameActionsOutsideAnActiveSession() {
        InputRouter router;

        const auto decision = router.route(
            {InputAction::Confirm, InputPhase::Pressed, InputDeviceKind::Keyboard},
            SessionState::NoRom);

        QCOMPARE(decision.target, RouteTarget::None);
        QCOMPARE(decision.command, UiCommand::None);
        QCOMPARE(decision.dsMask, DsButtonMask{0});
    }
};

QTEST_GUILESS_MAIN(InputRouterTest)
#include "tst_input_router.moc"
