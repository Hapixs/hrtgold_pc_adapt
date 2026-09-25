#include "core/session/SessionController.h"
#include "core/input/InputTypes.h"

#include <QtTest>

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {
EmulationConfig validConfig() {
    return EmulationConfig{
        .romPath = std::filesystem::path{L"C:/private/heartgold.nds"},
        .savePath = std::filesystem::path{L"C:/private/game.sav"},
        .romSha256 = std::string(64, 'a'),
    };
}

EmulationResult failure(std::string message) {
    return EmulationResult{EmulationErrorCode::CoreFailure, std::move(message)};
}

class ProbeEmulatorCore final : public EmulatorCore {
public:
    EmulationResult load(const EmulationConfig&) override {
        calls.push_back("load");
        return loadResult;
    }

    EmulationResult start() override {
        calls.push_back("start");
        return startResult;
    }

    EmulationResult pause() override {
        calls.push_back("pause");
        if (duringPause) {
            duringPause();
        }
        return pauseResult;
    }

    EmulationResult resume() override {
        calls.push_back("resume");
        if (duringResume) {
            duringResume();
        }
        return resumeResult;
    }

    EmulationResult stop() override {
        calls.push_back("stop");
        return stopResult;
    }

    void setButtons(DsButtonMask pressed) override {
        buttonHistory.push_back(pressed);
        calls.push_back("buttons:" + std::to_string(pressed));
    }

    std::shared_ptr<const VideoFrame> latestFrame() const override { return {}; }
    bool isRunning() const noexcept override { return false; }

    EmulationResult loadResult;
    EmulationResult startResult;
    EmulationResult pauseResult;
    EmulationResult resumeResult;
    EmulationResult stopResult;
    std::function<void()> duringPause;
    std::function<void()> duringResume;
    std::vector<std::string> calls;
    std::vector<DsButtonMask> buttonHistory;
};
} // namespace

class SessionControllerTest final : public QObject {
    Q_OBJECT

private slots:
    void followsTheCompleteSessionLifecycle() {
        auto core = std::make_unique<ProbeEmulatorCore>();
        auto* observed = core.get();
        SessionController session(std::move(core));
        std::vector<SessionState> states;
        session.onStateChanged([&](SessionState state) { states.push_back(state); });

        QCOMPARE(session.state(), SessionState::NoRom);
        QVERIFY(session.load(validConfig()).ok());
        QVERIFY(session.openMenu().ok());
        QVERIFY(session.closeMenu().ok());
        QVERIFY(session.stop().ok());

        QCOMPARE(session.state(), SessionState::NoRom);
        QCOMPARE(states,
                 std::vector<SessionState>({SessionState::Loading,
                                            SessionState::Playing,
                                            SessionState::MenuOpen,
                                            SessionState::Playing,
                                            SessionState::Stopping,
                                            SessionState::NoRom}));
        QCOMPARE(observed->calls,
                 std::vector<std::string>({"load", "start", "buttons:0", "pause",
                                           "buttons:0", "resume", "buttons:0", "stop"}));
    }

    void rejectsInvalidTransitionsWithoutCallingTheCore() {
        auto core = std::make_unique<ProbeEmulatorCore>();
        auto* observed = core.get();
        SessionController session(std::move(core));
        std::vector<EmulationResult> errors;
        session.onError([&](const EmulationResult& error) { errors.push_back(error); });

        QVERIFY(!session.openMenu().ok());
        QVERIFY(!session.closeMenu().ok());
        QVERIFY(!session.stop().ok());
        QCOMPARE(session.state(), SessionState::NoRom);
        QVERIFY(observed->calls.empty());
        QVERIFY(errors.empty());

        QVERIFY(session.load(validConfig()).ok());
        const auto callsAfterLoad = observed->calls;
        QVERIFY(!session.load(validConfig()).ok());
        QVERIFY(!session.closeMenu().ok());
        QCOMPARE(session.state(), SessionState::Playing);
        QCOMPARE(observed->calls, callsAfterLoad);
        QVERIFY(errors.empty());
        QVERIFY(session.stop().ok());
    }

    void openingAndClosingMenuNeutralizeButtonsBeforeCoreOperations() {
        auto core = std::make_unique<ProbeEmulatorCore>();
        auto* observed = core.get();
        SessionController session(std::move(core));
        QVERIFY(session.load(validConfig()).ok());
        session.setDsButtons(toMask(DsButton::Right));

        observed->duringPause = [&] {
            QCOMPARE(session.state(), SessionState::Playing);
            QCOMPARE(observed->buttonHistory.back(), DsButtonMask{0});
        };
        QVERIFY(session.openMenu().ok());

        observed->duringResume = [&] {
            QCOMPARE(session.state(), SessionState::MenuOpen);
            QCOMPARE(observed->buttonHistory.back(), DsButtonMask{0});
        };
        QVERIFY(session.closeMenu().ok());

        QCOMPARE(observed->buttonHistory,
                 std::vector<DsButtonMask>({toMask(DsButton::Right), 0, 0}));
        QVERIFY(session.stop().ok());
    }

    void menuStateDoesNotForwardGameButtons() {
        auto core = std::make_unique<ProbeEmulatorCore>();
        auto* observed = core.get();
        SessionController session(std::move(core));
        QVERIFY(session.load(validConfig()).ok());
        QVERIFY(session.openMenu().ok());
        const auto historyAtMenuOpen = observed->buttonHistory;

        session.setDsButtons(toMask(DsButton::A));

        QCOMPARE(observed->buttonHistory, historyAtMenuOpen);
        QVERIFY(session.closeMenu().ok());
        QVERIFY(session.stop().ok());
    }

    void loadFailurePublishesErrorExactlyOnce() {
        auto core = std::make_unique<ProbeEmulatorCore>();
        core->loadResult = EmulationResult{EmulationErrorCode::InvalidRom, "bad ROM"};
        SessionController session(std::move(core));
        std::vector<SessionState> states;
        std::vector<EmulationResult> errors;
        session.onStateChanged([&](SessionState state) { states.push_back(state); });
        session.onError([&](const EmulationResult& error) { errors.push_back(error); });

        const auto result = session.load(validConfig());

        QCOMPARE(result.code, EmulationErrorCode::InvalidRom);
        QCOMPARE(session.state(), SessionState::Error);
        QCOMPARE(states,
                 std::vector<SessionState>({SessionState::Loading, SessionState::Error}));
        QCOMPARE(errors.size(), std::size_t{1});
        QCOMPARE(errors.front().code, EmulationErrorCode::InvalidRom);
        QVERIFY(!session.openMenu().ok());
        QCOMPARE(errors.size(), std::size_t{1});
    }

    void loadFailureWithoutAnActiveCoreAllowsAnotherLoadAttempt() {
        auto core = std::make_unique<ProbeEmulatorCore>();
        auto* observed = core.get();
        core->loadResult = EmulationResult{EmulationErrorCode::InvalidRom, "bad ROM"};
        SessionController session(std::move(core));
        int errorCount = 0;
        session.onError([&](const EmulationResult&) { ++errorCount; });
        QVERIFY(!session.load(validConfig()).ok());
        observed->loadResult = {};

        QVERIFY(session.load(validConfig()).ok());

        QCOMPARE(session.state(), SessionState::Playing);
        QCOMPARE(errorCount, 1);
        QCOMPARE(observed->calls, std::vector<std::string>({"load", "load", "start"}));
        QVERIFY(session.stop().ok());
    }

    void startFailurePublishesErrorExactlyOnce() {
        auto core = std::make_unique<ProbeEmulatorCore>();
        core->startResult = failure("start failed");
        SessionController session(std::move(core));
        int errorCount = 0;
        session.onError([&](const EmulationResult&) { ++errorCount; });

        QVERIFY(!session.load(validConfig()).ok());

        QCOMPARE(session.state(), SessionState::Error);
        QCOMPARE(errorCount, 1);
    }

    void startFailureAllowsConservativeStopCleanup() {
        auto core = std::make_unique<ProbeEmulatorCore>();
        auto* observed = core.get();
        core->startResult = failure("start failed");
        SessionController session(std::move(core));
        QVERIFY(!session.load(validConfig()).ok());

        QVERIFY(session.stop().ok());

        QCOMPARE(session.state(), SessionState::NoRom);
        QCOMPARE(observed->calls,
                 std::vector<std::string>({"load", "start", "buttons:0", "stop"}));
    }

    void pauseFailurePublishesErrorExactlyOnce() {
        auto core = std::make_unique<ProbeEmulatorCore>();
        auto* observed = core.get();
        core->pauseResult = failure("pause failed");
        SessionController session(std::move(core));
        int errorCount = 0;
        session.onError([&](const EmulationResult&) { ++errorCount; });
        QVERIFY(session.load(validConfig()).ok());
        session.setDsButtons(toMask(DsButton::Left));

        QVERIFY(!session.openMenu().ok());

        QCOMPARE(session.state(), SessionState::Error);
        QCOMPARE(errorCount, 1);
        QCOMPARE(observed->buttonHistory.back(), DsButtonMask{0});
    }

    void pauseFailureAllowsStopCleanup() {
        auto core = std::make_unique<ProbeEmulatorCore>();
        auto* observed = core.get();
        core->pauseResult = failure("pause failed");
        SessionController session(std::move(core));
        QVERIFY(session.load(validConfig()).ok());
        QVERIFY(!session.openMenu().ok());

        QVERIFY(session.stop().ok());

        QCOMPARE(session.state(), SessionState::NoRom);
        QCOMPARE(observed->calls.back(), std::string("stop"));
    }

    void reentrantInputCannotUndoNeutralizationDuringPause() {
        auto core = std::make_unique<ProbeEmulatorCore>();
        auto* observed = core.get();
        SessionController session(std::move(core));
        QVERIFY(session.load(validConfig()).ok());
        session.setDsButtons(toMask(DsButton::Right));
        observed->duringPause = [&] { session.setDsButtons(toMask(DsButton::A)); };

        QVERIFY(session.openMenu().ok());

        QCOMPARE(observed->buttonHistory,
                 std::vector<DsButtonMask>({toMask(DsButton::Right), 0}));
        QVERIFY(session.closeMenu().ok());
        QVERIFY(session.stop().ok());
    }

    void resumeFailurePublishesErrorExactlyOnce() {
        auto core = std::make_unique<ProbeEmulatorCore>();
        auto* observed = core.get();
        core->resumeResult = failure("resume failed");
        SessionController session(std::move(core));
        int errorCount = 0;
        session.onError([&](const EmulationResult&) { ++errorCount; });
        QVERIFY(session.load(validConfig()).ok());
        QVERIFY(session.openMenu().ok());

        QVERIFY(!session.closeMenu().ok());

        QCOMPARE(session.state(), SessionState::Error);
        QCOMPARE(errorCount, 1);
        QCOMPARE(observed->buttonHistory.back(), DsButtonMask{0});
    }

    void resumeFailureAllowsStopCleanup() {
        auto core = std::make_unique<ProbeEmulatorCore>();
        auto* observed = core.get();
        core->resumeResult = failure("resume failed");
        SessionController session(std::move(core));
        QVERIFY(session.load(validConfig()).ok());
        QVERIFY(session.openMenu().ok());
        QVERIFY(!session.closeMenu().ok());

        QVERIFY(session.stop().ok());

        QCOMPARE(session.state(), SessionState::NoRom);
        QCOMPARE(observed->calls.back(), std::string("stop"));
    }

    void stopFailurePublishesErrorExactlyOnce() {
        auto core = std::make_unique<ProbeEmulatorCore>();
        core->stopResult = failure("stop failed");
        SessionController session(std::move(core));
        std::vector<SessionState> states;
        int errorCount = 0;
        session.onStateChanged([&](SessionState state) { states.push_back(state); });
        session.onError([&](const EmulationResult&) { ++errorCount; });
        QVERIFY(session.load(validConfig()).ok());

        QVERIFY(!session.stop().ok());

        QCOMPARE(session.state(), SessionState::Error);
        QCOMPARE(states.back(), SessionState::Error);
        QCOMPARE(errorCount, 1);
    }
};

QTEST_GUILESS_MAIN(SessionControllerTest)
#include "tst_session_controller.moc"
