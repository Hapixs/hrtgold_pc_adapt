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

    void keepsAButtonHeldUntilEveryDeviceReleasesIt() {
        InputRouter router;
        const auto keyboardDown = router.route(
            {InputAction::MoveUp, InputPhase::Pressed, InputDeviceKind::Keyboard},
            SessionState::Playing);
        const auto xboxDown = router.route(
            {InputAction::MoveUp, InputPhase::Pressed, InputDeviceKind::Xbox},
            SessionState::Playing);
        const auto keyboardUp = router.route(
            {InputAction::MoveUp, InputPhase::Released, InputDeviceKind::Keyboard},
            SessionState::Playing);
        const auto xboxUp = router.route(
            {InputAction::MoveUp, InputPhase::Released, InputDeviceKind::Xbox},
            SessionState::Playing);

        QCOMPARE(keyboardDown.dsMask, toMask(DsButton::Up));
        QCOMPARE(xboxDown.dsMask, toMask(DsButton::Up));
        QCOMPARE(keyboardUp.dsMask, toMask(DsButton::Up));
        QCOMPARE(xboxUp.dsMask, DsButtonMask{0});
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
        router.synchronizeSessionState(SessionState::Playing);
        router.route({InputAction::MoveLeft, InputPhase::Pressed,
                      InputDeviceKind::Keyboard},
                     SessionState::Playing);

        router.synchronizeSessionState(SessionState::MenuOpen);
        const auto overlay = router.route(
            {InputAction::MoveUp, InputPhase::Pressed, InputDeviceKind::Xbox},
            SessionState::MenuOpen);
        router.synchronizeSessionState(SessionState::Playing);
        const auto resumed = router.route(
            {InputAction::MoveDown, InputPhase::Pressed, InputDeviceKind::Keyboard},
            SessionState::Playing);

        QCOMPARE(overlay.target, RouteTarget::Overlay);
        QCOMPARE(overlay.dsMask, DsButtonMask{0});
        QCOMPARE(resumed.dsMask, toMask(DsButton::Down));
    }

    void explicitSessionSynchronizationClearsAnUnseenMenuRoundTrip() {
        InputRouter router;
        router.synchronizeSessionState(SessionState::Playing);
        router.route({InputAction::MoveLeft, InputPhase::Pressed,
                      InputDeviceKind::Keyboard},
                     SessionState::Playing);

        router.synchronizeSessionState(SessionState::MenuOpen);
        router.synchronizeSessionState(SessionState::Playing);
        const auto nextPress = router.route(
            {InputAction::MoveDown, InputPhase::Pressed, InputDeviceKind::Keyboard},
            SessionState::Playing);

        QCOMPARE(nextPress.dsMask, toMask(DsButton::Down));
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

    void welcomeConfirmRequestsRomAndSuppressesRepeat() {
        InputRouter router;
        const auto decision = router.route(
            {InputAction::Confirm, InputPhase::Pressed, InputDeviceKind::Xbox},
            SessionState::NoRom);
        QCOMPARE(decision.target, RouteTarget::Application);
        QCOMPARE(decision.command, UiCommand::ChooseRom);
        QCOMPARE(decision.dsMask, DsButtonMask{0});
        QCOMPARE(router.route({InputAction::Confirm, InputPhase::Pressed,
                               InputDeviceKind::Xbox, true}, SessionState::NoRom).target,
                 RouteTarget::None);
        QCOMPARE(router.route({InputAction::MoveUp, InputPhase::Pressed,
                               InputDeviceKind::Xbox}, SessionState::NoRom).target,
                 RouteTarget::None);
    }

    void fullscreenToggleSuppressesAutoRepeatButArrowsStillRepeat() {
        InputRouter router;
        const auto first = router.route(
            {InputAction::ToggleFullscreen, InputPhase::Pressed, InputDeviceKind::Keyboard},
            SessionState::MenuOpen);
        const auto repeat = router.route(
            {InputAction::ToggleFullscreen, InputPhase::Pressed, InputDeviceKind::Keyboard, true},
            SessionState::MenuOpen);
        const auto release = router.route(
            {InputAction::ToggleFullscreen, InputPhase::Released, InputDeviceKind::Keyboard},
            SessionState::MenuOpen);
        const auto arrowRepeat = router.route(
            {InputAction::MoveRight, InputPhase::Pressed, InputDeviceKind::Keyboard, true},
            SessionState::MenuOpen);
        QCOMPARE(first.target, RouteTarget::Application);
        QCOMPARE(first.command, UiCommand::ToggleFullscreen);
        QCOMPARE(repeat.target, RouteTarget::None);
        QCOMPARE(release.target, RouteTarget::None);
        QCOMPARE(arrowRepeat.target, RouteTarget::Overlay);
    }
};

QTEST_GUILESS_MAIN(InputRouterTest)
#include "tst_input_router.moc"
