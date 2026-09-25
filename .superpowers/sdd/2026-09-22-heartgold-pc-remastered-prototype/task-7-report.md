# Task 7 implementation report

Implementation commit: `4ff754580f604c9f127deaad4f458f1801522a79` (`feat: add controller-ready PC menu prototype`).

## Files

- `src/ui/UiState.h`, `src/ui/UiState.cpp`: QObject bridge for session state, input routing, exact tile names and commands, device footer prompts, path validation, and whitelisted settings.
- `src/app/AppBootstrap.h`, `src/app/AppBootstrap.cpp`, `src/app/main.cpp`: fake-core composition, shared SDL audio sink lifetime, bounded input/frame polling, QML registration, and `--fake-core` startup.
- `src/ui/qml/Main.qml`, `WelcomePage.qml`, `MainMenu.qml`, `OptionsPanel.qml`, `components/MenuTile.qml`: original 1920 × 1080 interface, 2 × 4 grid, accessible controls and focus indication, Options and Resume flows.
- `tests/ui/tst_ui_state.cpp`, `tests/app/tst_app_bootstrap.cpp`, `tests/qml/quicktest.cpp`, `tests/qml/tst_MainMenu.qml`, `tst_OptionsPanel.qml`, `tst_WelcomePage.qml`: bridge, composition, keyboard, accessibility, safe-area, and action tests.
- `CMakeLists.txt`: targets and QML resources.
- `src/core/emulation/FrameMailbox.h`: renamed the internal `slots` member to `frameSlots`; Qt's `slots` macro otherwise broke compilation when core and Qt headers met. No public behavior changed; `tst_frame_mailbox` passes.

## RED and GREEN evidence

- RED: `task-7-red.txt` records the missing `ui/UiState.h` compile failure before bridge implementation.
- RED: the initial QML run reported missing `src/ui/qml` import under GDB. The GUI test executable hid normal console output on Windows; GDB exposed the real QML diagnostic. `task-7-qml-diagnostic.txt` records the initial silent shell capture; the direct GDB result was inspected during the run.
- RED: `task-7-options-red.txt` records `OptionsPanel is not a type`; `task-7-second-red.txt` records missing `app/AppBootstrap.h`. `task-7-controls-red.txt` records missing styled-control aliases before the visual fix.
- GREEN: focused `tst_frame_mailbox|tst_ui_state|tst_qml_main_menu|tst_app_bootstrap` run passed 4/4 CTest targets. The QML runner exercised 8 behavior tests across the menu, options, and welcome components.
- GREEN: `./scripts/build.ps1 -Preset ucrt-debug` passed the full 12/12 CTest targets after the final source changes. Full output is in `task-7-full-green.txt`.

## Bounded smoke and visual inspection

- `heartgold_pc.exe --fake-core` under `QT_QPA_PLATFORM=offscreen` stayed alive for the 3-second bound and produced no QML load error. The captured stderr (`task-7-fake-smoke-stderr.txt`) contains only offscreen Windows theme/font warnings.
- A temporary Qt Quick Test harness under ignored `build/ucrt-debug/visual/` generated `menu-1280.png`, `menu-1920.png`, `menu-3840.png`, `options-1920.png`, `resume-1280.png`, and `welcome-1280.png`. All were inspected: the 2 × 4 menu, focus ring, footer, Options controls, and welcome button remain visible; the generated color-bar region remains 4:3 with side fill at 1280 × 720. The QML safe-area test also checks tile bounds at 1280 × 720, 1920 × 1080, and 3840 × 2160.
- These captures render QML over generated color bars in a Qt test harness. The actual app was smoke-tested headlessly; physical gamepad interaction, native file dialog use, and interactive desktop resizing were not exercised. No commercial ROM, private console file, or copyrighted image asset was used.

## Self-review

- Product actions and settings remain in `UiState`; QML handles layout and direct keyboard focus/navigation. The six future tiles stay focusable and announce unavailable status. `Options` and `Reprendre` change actual bridge/session state.
- Every `SessionController` state callback calls `InputRouter::synchronizeSessionState`; the bridge test verifies that held game input does not leak across menu/resume.
- The bridge references the session and owns no emulator. `AppBootstrap` stops the session before destroying the core and releases its shared audio sink before SDL runtime destruction. The QML engine in `main.cpp` is destroyed before `AppBootstrap`.
- Settings writes are restricted to volume, smooth scaling, fullscreen, and `reopenLastGame`; the selected ROM path is stored only after validated successful loading when reopening is enabled, and removed when disabled.
- No unresolved test failures. The remaining visual limitation is the absence of a screenshot of the real `GameSurface` instance in the running app; the existing `tst_game_surface` aspect test passed in the full suite.
