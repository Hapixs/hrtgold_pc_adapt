# HeartGold PC Remastered Prototype Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a Windows 11 x64 prototype that runs a user-supplied HeartGold ROM, renders only the upper DS screen in a 16:9 composition, and provides a keyboard/gamepad-driven PC menu overlay.

**Architecture:** A C++20 Qt Quick application owns the window and overlay while a dedicated emulation worker owns a melonDS 1.1 core behind `EmulatorCore`. SDL3 supplies gamepad events and audio; pure C++ session, input, ROM, frame, and save components remain testable with `FakeEmulatorCore` and no Nintendo files.

**Tech Stack:** C++20, CMake 3.28+, Ninja, GCC/UCRT64, Qt 6.11 Core/Gui/Qml/Quick/QuickTest, SDL 3.4, melonDS 1.1 commit `b86390e4428bf38ce4c1ce0e9ca446d6d25955e8`, Qt Test.

**Spec:** `docs/superpowers/specs/2026-09-22-heartgold-pc-remastered-design.md`

## Global Constraints

- Target Windows 11 x64 first; use the MSYS2 UCRT64 toolchain.
- Compile project code as C++20; compile melonDS with its upstream C++17 setting.
- License project code as GPL-3.0-or-later because the distributed application links melonDS.
- Do not add ROMs, BIOS, firmware, Pokémon assets, extracted fonts, sounds, screenshots, saves, hashes tied to a user's file, or copyrighted reference material to Git.
- Accept only local `.nds` files whose header identifies HeartGold (`IPK` game-code prefix); never modify or copy the ROM.
- Store native saves below `%LOCALAPPDATA%/HeartGoldPCRemastered/saves/<sha256>/` and create a dated backup before replacing an existing save.
- Render only the upper 256 × 192 framebuffer. Preserve its 4:3 ratio at every window size; never expose the lower framebuffer to QML.
- Keep Qt, SDL3, and melonDS types out of `src/core/` public interfaces.
- Automated tests must run without commercial ROMs or private console files.
- `Options` and `Reprendre` are functional in this prototype; the six Pokémon feature tiles remain focusable and report that they are unavailable.
- The emulation thread never blocks the Qt render thread; bounded queues drop stale video/audio work instead of growing.

## Review Focus

1. **A valid NDS file for a different game:** `RomValidator` must return `UnsupportedGame` and the app must remain on the welcome screen; Task 2 pins this behavior.
2. **A ROM name containing path separators or Unicode:** save paths must derive only from the lowercase SHA-256 and remain below the app data directory; Task 2 pins this behavior.
3. **Menu opened while a DS direction is held:** the core must receive a neutral mask before pause and again before resume, preventing a ghost movement; Task 4 pins this behavior.
4. **Gamepad removed while the menu is open:** the menu must keep its selected tile, switch prompts to keyboard, and remain operable; Task 5 pins this behavior.
5. **A stale or malformed video frame:** `GameSurface` must keep the last valid 256 × 192 frame and never stretch or dereference an invalid buffer; Task 6 pins this behavior.

---

## File Map

```text
CMakeLists.txt                         project targets and feature switches
CMakePresets.json                     UCRT64 debug/release presets
cmake/MelonDS.cmake                   pinned melonDS target configuration
scripts/bootstrap.ps1                 installs the declared MSYS2 packages
scripts/build.ps1                     configures, builds, and runs tests
scripts/package.ps1                   creates a clean Windows bundle
LICENSES/GPL-3.0-or-later.txt         project license text
THIRD_PARTY.md                        dependency versions, licenses, sources
src/app/main.cpp                      process entry and dependency wiring
src/app/AppBootstrap.{h,cpp}          owns long-lived app services
src/core/emulation/EmulationTypes.h   frame, audio, input, error value types
src/core/emulation/EmulatorCore.h     emulator boundary
src/core/emulation/FrameMailbox.h     latest-frame handoff
src/core/input/InputTypes.h            semantic actions and device type
src/core/input/InputRouter.{h,cpp}     context-aware routing and held keys
src/core/rom/RomValidator.{h,cpp}      NDS header checks and SHA-256
src/core/session/SessionController.*  application state machine
src/core/storage/AppPaths.{h,cpp}      safe per-user paths
src/core/storage/SaveRepository.*     atomic save and dated backup
src/emulation/fake/FakeEmulatorCore.* deterministic test/demo core
src/emulation/melonds/MelonDsAdapter.* worker and melonDS translation
src/emulation/melonds/MelonPlatform.cpp melonDS Platform API implementation
src/platform/sdl/SdlApi.*             injectable SDL3 facade
src/platform/sdl/SdlRuntime.*         hot-plug gamepads and audio stream
src/ui/GameSurface.{h,cpp}             QQuickItem upper-screen texture
src/ui/UiState.{h,cpp}                 QObject bridge for session/menu/settings
src/ui/qml/Main.qml                    root 16:9 composition
src/ui/qml/WelcomePage.qml             ROM selection and errors
src/ui/qml/MainMenu.qml                tile grid and command hints
src/ui/qml/OptionsPanel.qml            display/filter/volume controls
src/ui/qml/components/MenuTile.qml     reusable focusable tile
tests/**                               Qt Test and Qt Quick Test suites
```

## Task 1: Repository, Toolchain, and Executable Skeleton

**Files:**
- Create: `.gitignore`
- Create: `CMakeLists.txt`
- Create: `CMakePresets.json`
- Create: `scripts/bootstrap.ps1`
- Create: `scripts/build.ps1`
- Create: `LICENSES/GPL-3.0-or-later.txt`
- Create: `THIRD_PARTY.md`
- Create: `README.md`
- Create: `src/app/main.cpp`
- Create: `tests/smoke/tst_bootstrap.cpp`

**Interfaces:**
- Consumes: the validated spec.
- Produces: `heartgold_pc` and `tst_bootstrap` CMake targets; `scripts/build.ps1 -Preset ucrt-debug` as the canonical build command.

- [ ] **Step 1: Initialize Git and record the approved design artifacts**

Run from the project root:

```powershell
git init -b main
git add docs/superpowers/specs docs/superpowers/plans
git commit -m "docs: define HeartGold PC Remastered prototype"
```

Expected: a clean `main` branch containing only project instructions, the spec, and this plan.

- [ ] **Step 2: Add the bootstrap and dependency declarations**

`scripts/bootstrap.ps1` must install MSYS2 with `winget` when `C:\msys64\usr\bin\bash.exe` is absent, then run:

```powershell
& 'C:\msys64\usr\bin\bash.exe' -lc @'
pacman -Syu --noconfirm
pacman -S --needed --noconfirm \
  git \
  mingw-w64-ucrt-x86_64-toolchain \
  mingw-w64-ucrt-x86_64-cmake \
  mingw-w64-ucrt-x86_64-ninja \
  mingw-w64-ucrt-x86_64-qt6-base \
  mingw-w64-ucrt-x86_64-qt6-declarative \
  mingw-w64-ucrt-x86_64-qt6-shadertools \
  mingw-w64-ucrt-x86_64-qt6-tools \
  mingw-w64-ucrt-x86_64-sdl3
'@
```

If MSYS2 is absent, the script first runs:

```powershell
winget install --id MSYS2.MSYS2 --exact --accept-package-agreements --accept-source-agreements
```

`THIRD_PARTY.md` records Qt 6.11.2, SDL 3.4.16, melonDS 1.1, their upstream URLs, and licenses. `.gitignore` excludes `/build/`, `/dist/`, `*.nds`, `*.sav`, `*.dsv`, BIOS/firmware filename patterns, logs, and local settings.

- [ ] **Step 3: Add the first failing smoke test and minimal CMake project**

Create `tests/smoke/tst_bootstrap.cpp`:

```cpp
#include <QtTest>

class BootstrapTest final : public QObject {
    Q_OBJECT
private slots:
    void applicationIdentityIsStable() {
        QCOMPARE(QStringLiteral("HeartGold PC Remastered"),
                 QStringLiteral(HEARTGOLD_PC_APP_NAME));
    }
};

QTEST_GUILESS_MAIN(BootstrapTest)
#include "tst_bootstrap.moc"
```

Configure before defining `HEARTGOLD_PC_APP_NAME`:

```powershell
./scripts/build.ps1 -Preset ucrt-debug
```

Expected: compile failure mentioning `HEARTGOLD_PC_APP_NAME`.

- [ ] **Step 4: Complete the application skeleton**

`CMakeLists.txt` must declare:

```cmake
cmake_minimum_required(VERSION 3.28)
project(HeartGoldPCRemastered VERSION 0.1.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
enable_testing()

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Qml Quick Test QuickTest)
find_package(SDL3 3.2 REQUIRED CONFIG)
add_compile_definitions(HEARTGOLD_PC_APP_NAME="HeartGold PC Remastered")
```

`src/app/main.cpp` creates `QGuiApplication`, sets organization `HeartGoldPCRemastered`, loads an inline temporary `ApplicationWindow`, and returns nonzero if the QML engine has no root object. `CMakePresets.json` uses `C:/msys64/ucrt64/bin/ninja.exe`, `C:/msys64/ucrt64/bin/gcc.exe`, and `C:/msys64/ucrt64/bin/g++.exe` for `ucrt-debug` and `ucrt-release`.

- [ ] **Step 5: Build and run the smoke test**

```powershell
./scripts/build.ps1 -Preset ucrt-debug
```

Expected: configure succeeds, `heartgold_pc` links, and CTest reports `tst_bootstrap` passing.

- [ ] **Step 6: Commit the bootstrapped repository**

```powershell
git add .gitignore CMakeLists.txt CMakePresets.json LICENSES THIRD_PARTY.md README.md scripts src/app tests/smoke
git commit -m "build: bootstrap Windows Qt application"
```

## Task 2: ROM Validation, Safe Paths, and Atomic Saves

**Files:**
- Create: `src/core/rom/RomValidator.h`
- Create: `src/core/rom/RomValidator.cpp`
- Create: `src/core/storage/AppPaths.h`
- Create: `src/core/storage/AppPaths.cpp`
- Create: `src/core/storage/SaveRepository.h`
- Create: `src/core/storage/SaveRepository.cpp`
- Create: `tests/core/tst_rom_validator.cpp`
- Create: `tests/core/tst_save_repository.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: a local `std::filesystem::path` selected by the user.
- Produces: `RomValidation { RomStatus status; std::string title; std::string gameCode; std::string sha256; uint64_t size; }`, `AppPaths::saveDirectory(std::string_view sha256)`, and `SaveRepository::replace(std::span<const uint8_t>)`.

- [ ] **Step 1: Write failing ROM tests with generated byte arrays**

`tests/core/tst_rom_validator.cpp` builds a 4096-byte fixture, writes `POKEMON HG` at offset `0x00`, `IPKF` at `0x0C`, an ARM9 offset of `0x200` at `0x20`, and an ARM9 size of `4` at `0x2C`. Add tests:

```cpp
void acceptsHeartGoldHeader() {
    const auto result = validator.validate(writeFixture("IPKF"));
    QCOMPARE(result.status, RomStatus::ValidHeartGold);
    QCOMPARE(result.gameCode, std::string("IPKF"));
    QCOMPARE(result.sha256.size(), size_t{64});
}

void rejectsAnotherGame() {
    const auto result = validator.validate(writeFixture("ABCD"));
    QCOMPARE(result.status, RomStatus::UnsupportedGame);
}

void rejectsTruncatedArm9Range() {
    const auto result = validator.validate(writeFixture("IPKF", 0x5000, 4));
    QCOMPARE(result.status, RomStatus::MalformedHeader);
}
```

Run:

```powershell
./scripts/build.ps1 -Preset ucrt-debug -Test tst_rom_validator
```

Expected: compile failure because `RomValidator` does not exist.

- [ ] **Step 2: Implement deterministic NDS header validation**

Use these declarations in `RomValidator.h`:

```cpp
enum class RomStatus { ValidHeartGold, FileNotFound, IoError, TooSmall,
                       MalformedHeader, UnsupportedGame };

struct RomValidation {
    RomStatus status;
    std::string title;
    std::string gameCode;
    std::string sha256;
    std::uint64_t size;
};

class RomValidator {
public:
    RomValidation validate(const std::filesystem::path& path) const;
};
```

Read without modifying the file, reject sizes below `0x200`, trim the 12-byte title at NUL/space, require the first three game-code bytes to equal `IPK`, verify `arm9Offset + arm9Size <= fileSize` without integer overflow, and calculate lowercase SHA-256 with `QCryptographicHash`.

- [ ] **Step 3: Write failing storage containment and backup tests**

Add tests that pass a ROM display name of `..\\évasion.nds` but a hash of 64 `a` characters, then assert:

```cpp
const auto saveDir = paths.saveDirectory(validHash);
QVERIFY(saveDir.string().starts_with(paths.root().string()));
QCOMPARE(saveDir.filename().string(), validHash);
```

Write bytes `{1,2,3}` to `game.sav`, call `replace({4,5,6})`, and assert the active file contains `{4,5,6}` while exactly one `game-YYYYMMDD-HHMMSS.sav.bak` contains `{1,2,3}`. Add a failure test using a read-only directory and expect `SaveWriteError::NotWritable`.

- [ ] **Step 4: Implement app paths and atomic replacement**

Use:

```cpp
class AppPaths {
public:
    explicit AppPaths(std::filesystem::path localAppData);
    std::filesystem::path root() const;
    std::filesystem::path saveDirectory(std::string_view sha256) const;
};

enum class SaveWriteError { None, InvalidHash, NotWritable, RenameFailed };

class SaveRepository {
public:
    SaveRepository(std::filesystem::path directory,
                   std::function<std::chrono::system_clock::time_point()> clock);
    SaveWriteError replace(std::span<const std::uint8_t> bytes);
    std::vector<std::uint8_t> load() const;
};
```

Accept only `[0-9a-f]{64}` as a directory key. `replace` writes `game.sav.tmp`, flushes and closes it, copies an existing `game.sav` to a timestamped `.bak`, then renames the temporary file. Remove only the temporary file after a failed rename; preserve the original and its backup.

- [ ] **Step 5: Run focused and full tests**

```powershell
./scripts/build.ps1 -Preset ucrt-debug -Test 'tst_rom_validator|tst_save_repository'
./scripts/build.ps1 -Preset ucrt-debug
```

Expected: all ROM, containment, backup, and smoke cases pass.

- [ ] **Step 6: Commit ROM and save boundaries**

```powershell
git add CMakeLists.txt src/core/rom src/core/storage tests/core
git commit -m "feat: validate HeartGold ROMs and protect native saves"
```

## Task 3: Emulator Contract, Frame Mailbox, and Fake Core

**Files:**
- Create: `src/core/emulation/EmulationTypes.h`
- Create: `src/core/emulation/EmulatorCore.h`
- Create: `src/core/emulation/FrameMailbox.h`
- Create: `src/emulation/fake/FakeEmulatorCore.h`
- Create: `src/emulation/fake/FakeEmulatorCore.cpp`
- Create: `tests/core/tst_frame_mailbox.cpp`
- Create: `tests/emulation/tst_fake_emulator.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: validated ROM/save paths and a pressed-positive DS button mask.
- Produces: a Qt/SDL-free `EmulatorCore`, immutable 256 × 192 RGBA frames, stereo signed-16-bit audio chunks, and a deterministic fake implementation.

- [ ] **Step 1: Write failing frame and fake-core tests**

Add:

```cpp
void mailboxReturnsNewestCompleteFrame() {
    FrameMailbox box;
    box.publish(makeFrame(7, 0xff112233));
    box.publish(makeFrame(8, 0xff445566));
    const auto frame = box.latest();
    QVERIFY(frame);
    QCOMPARE(frame->sequence, std::uint64_t{8});
    QCOMPARE(frame->rgba.front(), std::uint32_t{0xff445566});
}

void fakeCoreHonorsLifecycle() {
    FakeEmulatorCore core;
    QVERIFY(core.load(validConfig()).ok());
    QVERIFY(core.start().ok());
    QVERIFY(core.pause().ok());
    QVERIFY(core.resume().ok());
    QVERIFY(core.stop().ok());
    QCOMPARE(core.calls(), std::vector<std::string>({"load","start","pause","resume","stop"}));
}
```

Expected: compile failure for the missing types.

- [ ] **Step 2: Define the stable core-owned types**

`EmulationTypes.h` contains:

```cpp
constexpr int kDsScreenWidth = 256;
constexpr int kDsScreenHeight = 192;

struct VideoFrame {
    std::uint64_t sequence{};
    int width{kDsScreenWidth};
    int height{kDsScreenHeight};
    std::array<std::uint32_t, kDsScreenWidth * kDsScreenHeight> rgba{};
};

enum class DsButton : std::uint16_t {
    A=1<<0, B=1<<1, Select=1<<2, Start=1<<3,
    Right=1<<4, Left=1<<5, Up=1<<6, Down=1<<7,
    R=1<<8, L=1<<9, X=1<<10, Y=1<<11
};
using DsButtonMask = std::uint16_t;

enum class EmulationErrorCode { None, InvalidRom, MissingPrivateFile,
                                SaveIo, AudioUnavailable, CoreFailure };
struct EmulationResult {
    EmulationErrorCode code{EmulationErrorCode::None};
    std::string message;
    bool ok() const noexcept { return code == EmulationErrorCode::None; }
};
```

`EmulationConfig` contains `romPath`, `savePath`, `romSha256`, `sampleRate{48000}`, and `useDirectBoot{true}`.

- [ ] **Step 3: Define `EmulatorCore` and the nonblocking mailbox**

```cpp
class EmulatorCore {
public:
    virtual ~EmulatorCore() = default;
    virtual EmulationResult load(const EmulationConfig&) = 0;
    virtual EmulationResult start() = 0;
    virtual EmulationResult pause() = 0;
    virtual EmulationResult resume() = 0;
    virtual EmulationResult stop() = 0;
    virtual void setButtons(DsButtonMask pressed) = 0;
    virtual std::shared_ptr<const VideoFrame> latestFrame() const = 0;
    virtual bool isRunning() const noexcept = 0;
};
```

`FrameMailbox` stores `std::atomic<std::shared_ptr<const VideoFrame>>`; `publish` rejects null frames and frames not exactly 256 × 192. Readers receive the last complete immutable frame without locking.

- [ ] **Step 4: Implement `FakeEmulatorCore`**

The fake accepts only a config with nonempty ROM hash and paths, owns a `std::jthread`, publishes a color-bar frame every 16 ms, records lifecycle calls under a mutex, and stops through `std::stop_token`. `pause` freezes the sequence counter; `setButtons` stores the latest pressed-positive mask for assertions.

- [ ] **Step 5: Verify lifecycle and concurrency**

```powershell
./scripts/build.ps1 -Preset ucrt-debug -Test 'tst_frame_mailbox|tst_fake_emulator'
./scripts/build.ps1 -Preset ucrt-debug
```

Expected: sequence increases while playing, stays fixed while paused, resumes increasing, and the worker joins within 250 ms on stop.

- [ ] **Step 6: Commit the emulation seam**

```powershell
git add CMakeLists.txt src/core/emulation src/emulation/fake tests/core/tst_frame_mailbox.cpp tests/emulation
git commit -m "feat: define testable emulator boundary"
```

## Task 4: Session State Machine and Input Routing

**Files:**
- Create: `src/core/session/SessionController.h`
- Create: `src/core/session/SessionController.cpp`
- Create: `src/core/input/InputTypes.h`
- Create: `src/core/input/InputRouter.h`
- Create: `src/core/input/InputRouter.cpp`
- Create: `tests/core/tst_session_controller.cpp`
- Create: `tests/core/tst_input_router.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `EmulatorCore`, `EmulationConfig`, semantic input events.
- Produces: `SessionState`, observable state/error callbacks, and exclusive routing to either UI commands or a DS mask.

- [ ] **Step 1: Write the failing session transition tests**

Cover `NoRom → Loading → Playing → MenuOpen → Playing → Stopping → NoRom`, rejected transitions, and load failure. Pin the held-direction case:

```cpp
void openingMenuNeutralizesInputAroundPause() {
    auto fake = std::make_unique<FakeEmulatorCore>();
    auto* observed = fake.get();
    SessionController session(std::move(fake));
    QVERIFY(session.load(validConfig()).ok());
    session.setDsButtons(toMask(DsButton::Right));
    QVERIFY(session.openMenu().ok());
    QCOMPARE(observed->buttonHistory().back(), DsButtonMask{0});
    QVERIFY(session.closeMenu().ok());
    QCOMPARE(observed->buttonHistory().back(), DsButtonMask{0});
}
```

- [ ] **Step 2: Implement the explicit session machine**

Use:

```cpp
enum class SessionState { NoRom, Loading, Playing, MenuOpen, Stopping, Error };

class SessionController {
public:
    explicit SessionController(std::unique_ptr<EmulatorCore> core);
    EmulationResult load(const EmulationConfig&);
    EmulationResult openMenu();
    EmulationResult closeMenu();
    EmulationResult stop();
    void setDsButtons(DsButtonMask);
    SessionState state() const noexcept;
    void onStateChanged(std::function<void(SessionState)> callback);
    void onError(std::function<void(const EmulationResult&)> callback);
};
```

All public mutations assert they run on the owning thread. `openMenu` sends mask zero, waits for `pause` to return, then publishes `MenuOpen`. `closeMenu` sends zero, resumes, and publishes `Playing`. Any failed core operation publishes `Error` exactly once.

- [ ] **Step 3: Write failing routing tests**

Define `InputAction` values `MoveUp`, `MoveDown`, `MoveLeft`, `MoveRight`, `Confirm`, `Back`, `OpenMenu`, `ToggleFullscreen`; `InputPhase` values `Pressed`, `Released`; and `InputDeviceKind` values `Keyboard`, `Xbox`, `PlayStation`, `GenericGamepad`.

Tests assert:

```cpp
QCOMPARE(router.route({InputAction::MoveUp, InputPhase::Pressed, InputDeviceKind::Keyboard}, SessionState::Playing).target,
         RouteTarget::Emulator);
QCOMPARE(router.route({InputAction::MoveUp, InputPhase::Pressed, InputDeviceKind::Xbox}, SessionState::MenuOpen).target,
         RouteTarget::Overlay);
QCOMPARE(router.route({InputAction::OpenMenu, InputPhase::Pressed, InputDeviceKind::Keyboard}, SessionState::Playing).command,
         UiCommand::OpenMenu);
```

Also press `Up` and `Left`, release `Up`, and verify the DS mask still contains `Left` only.

- [ ] **Step 4: Implement positive-mask and context routing**

`RouteDecision` is:

```cpp
enum class RouteTarget { None, Emulator, Overlay, Application };
enum class UiCommand { None, OpenMenu, CloseMenu, MoveUp, MoveDown, MoveLeft,
                       MoveRight, Confirm, Back, ToggleFullscreen };
struct RouteDecision { RouteTarget target; UiCommand command; DsButtonMask dsMask; };
```

In `Playing`, movement and confirm/back update the DS mask; `OpenMenu` is an application command. In `MenuOpen`, no action changes the DS mask and all menu actions target the overlay. Repeats are ignored for `OpenMenu`, `Back`, and `Confirm`.

- [ ] **Step 5: Run state and routing tests**

```powershell
./scripts/build.ps1 -Preset ucrt-debug -Test 'tst_session_controller|tst_input_router'
```

Expected: every transition and exclusive-routing assertion passes.

- [ ] **Step 6: Commit orchestration**

```powershell
git add CMakeLists.txt src/core/session src/core/input tests/core
git commit -m "feat: orchestrate sessions and exclusive input routing"
```

## Task 5: SDL3 Gamepad and Audio Runtime

**Files:**
- Create: `src/platform/sdl/SdlApi.h`
- Create: `src/platform/sdl/SdlApi.cpp`
- Create: `src/platform/sdl/SdlRuntime.h`
- Create: `src/platform/sdl/SdlRuntime.cpp`
- Create: `tests/platform/tst_sdl_runtime.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: SDL3 events, stereo 48 kHz signed-16-bit sample spans, volume `[0,1]`.
- Produces: semantic `InputEvent` callbacks, `InputDeviceKind` changes, bounded audio submission, and nonfatal audio status.

- [ ] **Step 1: Write failing tests against an injectable SDL facade**

`SdlApi` exposes only `init`, `pollEvent`, `openGamepad`, `closeGamepad`, `openAudioStream`, `putAudio`, and `shutdown`. A fake queue feeds add/remove/button/axis events. Add:

```cpp
void unplugKeepsSelectionAndFallsBackToKeyboard() {
    FakeSdlApi api;
    SdlRuntime runtime(api);
    QSignalSpy deviceSpy(&runtime, &SdlRuntime::activeDeviceChanged);
    api.push(gamepadAdded(12, GamepadKind::Xbox));
    runtime.pump();
    api.push(gamepadRemoved(12));
    runtime.pump();
    QCOMPARE(deviceSpy.last().at(0).value<InputDeviceKind>(), InputDeviceKind::Keyboard);
}

void audioQueueIsBounded() {
    FakeSdlApi api;
    SdlRuntime runtime(api);
    runtime.submitAudio(twoSecondsOfStereo());
    QVERIFY(api.queuedAudioBytes() <= 48000 * 2 * sizeof(std::int16_t) / 4);
}
```

The 250 ms audio cap prevents latency growth; excess oldest samples are dropped before submission.

- [ ] **Step 2: Implement gamepad discovery and normalization**

Initialize only `SDL_INIT_GAMEPAD | SDL_INIT_AUDIO | SDL_INIT_EVENTS`. Map D-pad, left-stick thresholds at `±16000`, south/east buttons, and Start to semantic actions. Track the last device that produced a pressed event. Use SDL gamepad type to emit Xbox, PlayStation, or generic prompts. Removal closes only the matching handle and emits keyboard fallback when no gamepad remains.

- [ ] **Step 3: Implement bounded audio with graceful failure**

Open an `SDL_AudioStream` for `SDL_AUDIO_S16`, two channels, 48000 Hz. `submitAudio` applies volume with saturated integer multiplication, drops data when no device is available, and never throws. Expose `audioAvailable()` and an error callback; the application continues silently on failure.

- [ ] **Step 4: Run facade tests and a hardware-optional smoke run**

```powershell
./scripts/build.ps1 -Preset ucrt-debug -Test tst_sdl_runtime
./build/ucrt-debug/heartgold_pc.exe --fake-core
```

Expected: automated tests pass without hardware. If a controller is present, its south/east/Start buttons appear in the diagnostic log; absence of a controller remains a pass.

- [ ] **Step 5: Commit the platform runtime**

```powershell
git add CMakeLists.txt src/platform/sdl tests/platform
git commit -m "feat: add hot-plug gamepad and bounded audio runtime"
```

## Task 6: Upper-Screen Qt Quick Rendering

**Files:**
- Create: `src/ui/GameSurface.h`
- Create: `src/ui/GameSurface.cpp`
- Create: `tests/ui/tst_game_surface.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `FrameMailbox`, an item rectangle, and `smooth` boolean.
- Produces: a `QQuickItem` that uploads only valid upper-screen frames and exposes `fitRect(container, 4.0/3.0)` for deterministic layout tests.

- [ ] **Step 1: Write failing geometry and malformed-frame tests**

```cpp
void fitRectPreservesFourByThreeInFullHd() {
    QCOMPARE(GameSurface::fitRect(QSizeF(1920,1080)), QRectF(240,0,1440,1080));
}

void fitRectPreservesFourByThreeInFourK() {
    QCOMPARE(GameSurface::fitRect(QSizeF(3840,2160)), QRectF(480,0,2880,2160));
}

void malformedFrameKeepsPreviousSequence() {
    surface.acceptFrame(validFrame(5));
    surface.acceptFrame(frameWithSize(128,96));
    QCOMPARE(surface.displayedSequence(), std::uint64_t{5});
}
```

- [ ] **Step 2: Implement the scene-graph item**

Subclass `QQuickItem`, set `ItemHasContents`, and in `updatePaintNode` copy the newest frame into a `QImage` with `Format_RGBA8888`, create/update a `QSGTexture`, and place a `QSGSimpleTextureNode` at `fitRect(boundingRect().size())`. Set texture filtering to nearest for `smooth == false` and linear otherwise. Delete textures only on the render thread through the scene graph node.

Do not expose a method or property for the lower screen. Reject null frames, wrong dimensions, and sequences not newer than the displayed sequence.

- [ ] **Step 3: Run offscreen scene tests**

```powershell
$env:QT_QPA_PLATFORM='offscreen'
./scripts/build.ps1 -Preset ucrt-debug -Test tst_game_surface
Remove-Item Env:QT_QPA_PLATFORM
```

Expected: exact rectangles pass at 720p, 1080p, ultrawide, and 4K; malformed frames retain the last valid image.

- [ ] **Step 4: Commit the render boundary**

```powershell
git add CMakeLists.txt src/ui/GameSurface.* tests/ui/tst_game_surface.cpp
git commit -m "feat: render upper DS screen without distortion"
```

## Task 7: QML Welcome Screen, Main Menu, Options, and UI Bridge

**Files:**
- Create: `src/ui/UiState.h`
- Create: `src/ui/UiState.cpp`
- Create: `src/app/AppBootstrap.h`
- Create: `src/app/AppBootstrap.cpp`
- Create: `src/ui/qml/Main.qml`
- Create: `src/ui/qml/WelcomePage.qml`
- Create: `src/ui/qml/MainMenu.qml`
- Create: `src/ui/qml/OptionsPanel.qml`
- Create: `src/ui/qml/components/MenuTile.qml`
- Create: `tests/qml/tst_MainMenu.qml`
- Create: `tests/ui/tst_ui_state.cpp`
- Modify: `src/app/main.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: state/error callbacks from `SessionController`, `InputDeviceKind`, menu commands, file-dialog result.
- Produces: QML properties `sessionState`, `activeDevice`, `selectedTile`, `statusMessage`, `volume`, `smoothScaling`, `fullscreen`; commands `chooseRom`, `activateSelected`, `resumeGame`, and `setOption`.

- [ ] **Step 1: Write failing `UiState` action tests**

Use a fake session and assert:

```cpp
void unavailableTileShowsMessageWithoutLeavingMenu() {
    state.setSelectedTile(0); // Pokédex
    state.activateSelected();
    QCOMPARE(state.statusMessage(), QStringLiteral("Disponible dans une prochaine étape"));
    QCOMPARE(state.sessionState(), SessionState::MenuOpen);
}

void resumeTileClosesMenu() {
    state.setSelectedTile(7);
    state.activateSelected();
    QCOMPARE(fakeSession.closeMenuCalls(), 1);
}
```

- [ ] **Step 2: Implement the QObject bridge without product logic in QML**

`UiState` owns no emulator. It references `SessionController`, `RomValidator`, settings, and callbacks supplied by `AppBootstrap`. It validates the selected path before constructing `EmulationConfig`. Map tile indexes exactly as `0 Pokédex`, `1 Équipe`, `2 Sac`, `3 Dresseur`, `4 Pokégear`, `5 Carte`, `6 Options`, `7 Reprendre`.

Persist only volume, smooth scaling, fullscreen, and `reopenLastGame`; store a ROM path only when the latter is true.

- [ ] **Step 3: Write failing QML navigation tests**

`tst_MainMenu.qml` creates the menu at 1920 × 1080 and sends keys:

```qml
function test_wrap_and_activate() {
    compare(menu.selectedIndex, 0)
    keyClick(Qt.Key_Right)
    compare(menu.selectedIndex, 1)
    keyClick(Qt.Key_Down)
    compare(menu.selectedIndex, 5)
    keyClick(Qt.Key_Return)
    compare(spy.signalArguments[0][0], 5)
}

function test_back_requests_resume() {
    keyClick(Qt.Key_Escape)
    compare(resumeSpy.count, 1)
}
```

Also assert every tile has a visible focus ring, a nonempty accessible name, and unavailable tiles expose `Accessible.description`.

- [ ] **Step 4: Build the original visual system**

Use a 1920 × 1080 logical canvas with 5% safe margins, two rows of four tiles, original flat-color gradients, rounded rectangles, and simple line icons created from Qt Quick Shapes. Use no Pokémon or BDSP image assets. `Main.qml` layers: side-fill background, `GameSurface`, 55% black veil while menu is open, `MainMenu`, then `OptionsPanel`.

Keyboard controls are arrows/WASD, Enter/E confirm, Escape back, Tab menu, and F11 fullscreen. The footer reads device-specific labels supplied by `UiState`; it does not infer controller type in QML.

- [ ] **Step 5: Wire the fake-core vertical slice**

`AppBootstrap` first wires the already available fake core, session, SDL runtime, and UI state. `src/app/main.cpp --fake-core` selects this composition, registers `GameSurface`, exposes only `UiState` to QML, and loads `qrc:/HeartGoldPC/ui/qml/Main.qml`. The fake mode bypasses the file dialog and starts with a generated color-bar background.

- [ ] **Step 6: Run UI automation and inspect 720p/1080p/4K**

```powershell
$env:QT_QPA_PLATFORM='offscreen'
./scripts/build.ps1 -Preset ucrt-debug -Test 'tst_ui_state|tst_qml_main_menu'
Remove-Item Env:QT_QPA_PLATFORM
./build/ucrt-debug/heartgold_pc.exe --fake-core
```

Expected: automation passes. Manual resizing preserves the 4:3 game image; focus, footer prompts, Options, and Reprendre remain visible at 1280 × 720, 1920 × 1080, and 3840 × 2160.

- [ ] **Step 7: Commit the complete fake-core prototype**

```powershell
git add CMakeLists.txt src/app src/ui tests/qml tests/ui
git commit -m "feat: add controller-ready PC menu prototype"
```

## Task 8: Pinned melonDS 1.1 Adapter

**Files:**
- Create: `cmake/MelonDS.cmake`
- Create: `src/emulation/melonds/MelonDsAdapter.h`
- Create: `src/emulation/melonds/MelonDsAdapter.cpp`
- Create: `src/emulation/melonds/MelonPlatform.cpp`
- Create: `tests/emulation/tst_melonds_adapter.cpp`
- Create: `.gitmodules`
- Modify: `CMakeLists.txt`
- Modify: `THIRD_PARTY.md`

**Interfaces:**
- Consumes: `EmulationConfig`, `DsButtonMask`, `SaveRepository`, `FrameMailbox`, `SdlRuntime::submitAudio`.
- Produces: `MelonDsAdapter final : EmulatorCore` and no melonDS symbols outside `src/emulation/melonds/`.

- [ ] **Step 1: Add and pin the GPL core as a submodule**

```powershell
git submodule add https://github.com/melonDS-emu/melonDS.git third_party/melonds
git -C third_party/melonds checkout b86390e4428bf38ce4c1ce0e9ca446d6d25955e8
git add .gitmodules third_party/melonds
```

`cmake/MelonDS.cmake` sets `BUILD_QT_SDL=OFF`, `ENABLE_OGLRENDERER=OFF`, `ENABLE_GDBSTUB=OFF`, `ENABLE_JIT=ON`, and `ENABLE_LTO_RELEASE=OFF` before `add_subdirectory(third_party/melonds)`. Link the adapter privately to upstream target `core`; no other project target may link it.

- [ ] **Step 2: Write the core smoke test before the adapter**

The test constructs a default `melonDS::NDS`, which uses melonDS FreeBIOS and blank firmware, then runs without a cartridge:

```cpp
void freeBiosProducesAnUpperFrameWithoutNintendoFiles() {
    MelonDsAdapter adapter(testSaveRepository(), silentAudioSink());
    const auto probe = adapter.runFreeBiosProbeForTest(3);
    QVERIFY(probe.ok());
    const auto frame = adapter.latestFrame();
    QVERIFY(frame);
    QCOMPARE(frame->width, 256);
    QCOMPARE(frame->height, 192);
}
```

Run and expect a link failure for missing `melonDS::Platform` functions:

```powershell
./scripts/build.ps1 -Preset ucrt-debug -Test tst_melonds_adapter
```

- [ ] **Step 3: Implement the melonDS Platform boundary**

Implement every declaration in upstream `Platform.h` with these fixed policies:

- files use 64-bit-safe C stdio and app-owned absolute paths;
- threads, mutexes and semaphores wrap `std::thread`, `std::mutex`, `std::condition_variable` with clean joins;
- timing uses `std::chrono::steady_clock`;
- dynamic library calls use `SDL_LoadObject`, `SDL_LoadFunction`, and `SDL_UnloadObject`;
- `WriteNDSSave` copies the complete buffer and schedules `SaveRepository::replace` on the adapter worker;
- firmware writes remain in the per-user data directory and never alter a user-supplied firmware file;
- microphone input returns silence;
- camera, LAN and local multiplayer return their documented inactive/no-packet values;
- `SignalStop` records the reason and requests the adapter worker to stop;
- logs route to a bounded rotating local log and redact ROM/save directory prefixes.

Copy no Qt/SDL frontend globals from melonDS. Where upstream platform code is adapted, retain its GPL header and record the source path in `THIRD_PARTY.md`.

- [ ] **Step 4: Implement ROM loading and direct boot**

On the worker thread:

```cpp
auto nds = std::make_unique<melonDS::NDS>();
auto cart = melonDS::NDSCart::ParseROM(std::move(romBytes), romSize,
                                      this, melonDS::NDSCart::NDSCartArgs{
                                          .SDCard = std::nullopt,
                                          .SRAM = std::move(saveBytes),
                                          .SRAMLength = saveSize
                                      });
nds->SetNDSCart(std::move(cart));
nds->Reset();
nds->SetupDirectBoot(config.romPath.filename().string());
nds->Start();
```

Reject ROMs larger than `UINT32_MAX`, `ParseROM == nullptr`, and save data larger than the core-reported save length. Translate the pressed-positive project mask into melonDS's active-low `SetKeyMask` representation in one named function covered by all 12 button cases.

- [ ] **Step 5: Publish only the top framebuffer and bounded audio**

Each worker iteration calls `RunFrame()`, reads `GPU.Framebuffer[GPU.FrontBuffer][0]`, converts the 256 × 192 pixels to project RGBA, and publishes through `FrameMailbox`. Never copy index `[1]` and never store a lower-screen pointer in a member.

Read at most 1024 stereo frames with `SPU.ReadOutput`, pass them to the audio callback, and drop audio while paused. Pace against `steady_clock` at the DS frame cadence while allowing stop/pause commands to wake the worker immediately.

- [ ] **Step 6: Test lifecycle, masks, save callback, and stop timeout**

Add tests for the 12-button translation table, FreeBIOS probe, `SignalStop`, save callback to a temporary directory, repeated pause/resume, and stop within 500 ms. All tests use no ROM; the real-ROM path is exercised manually in Task 9.

```powershell
./scripts/build.ps1 -Preset ucrt-debug -Test tst_melonds_adapter
```

Expected: every adapter test passes and dependency inspection shows only the adapter target linking `core`.

- [ ] **Step 7: Commit the pinned emulator integration**

```powershell
git add .gitmodules third_party/melonds cmake/MelonDS.cmake CMakeLists.txt THIRD_PARTY.md src/emulation/melonds tests/emulation/tst_melonds_adapter.cpp
git commit -m "feat: integrate pinned melonDS core"
```

## Task 9: Real Application Wiring, Failure Paths, and Windows Package

**Files:**
- Modify: `src/app/AppBootstrap.h`
- Modify: `src/app/AppBootstrap.cpp`
- Create: `scripts/package.ps1`
- Create: `tests/integration/tst_application_flow.cpp`
- Create: `docs/testing/manual-prototype-checklist.md`
- Modify: `src/app/main.cpp`
- Modify: `src/ui/UiState.cpp`
- Modify: `src/ui/qml/WelcomePage.qml`
- Modify: `README.md`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: all prior public interfaces and a ROM path selected through `QFileDialog`.
- Produces: the real `heartgold_pc.exe`, an offline fake-core mode, an internal Windows bundle under `dist/HeartGoldPCRemastered/`, and evidence for every prototype acceptance criterion.

- [ ] **Step 1: Write failing application-flow integration tests**

Inject fake validator, core, paths, SDL facade, and file picker. Cover:

```cpp
void invalidRomReturnsToWelcomeWithReason();
void coreFailureReturnsToWelcomeAndKeepsLog();
void missingAudioContinuesSessionSilently();
void saveFailurePausesAndBlocksResumeUntilDismissed();
void choosePlayOpenMenuNavigateOptionsResumeStop();
```

The final flow asserts state order, zero leaked game input while the menu is open, and a joined worker after window close.

- [ ] **Step 2: Compose production dependencies in `AppBootstrap`**

`AppBootstrap` constructs `AppPaths` from `QStandardPaths::AppLocalDataLocation`, `RomValidator`, `SaveRepository`, `SdlRuntime`, `MelonDsAdapter`, `SessionController`, and `UiState` in dependency order. Destruction order is the reverse: stop session, close audio/gamepads, release QML, then exit.

`main.cpp` selects `FakeEmulatorCore` only for explicit `--fake-core`; production is the default. Add `--rom <absolute-path>` for local manual testing without storing the path.

- [ ] **Step 3: Implement user-visible failure recovery**

Map errors exactly:

| Error | French message | Result |
|---|---|---|
| Invalid/malformed file | `Ce fichier n’est pas une ROM HeartGold valide.` | stay on welcome |
| Unsupported game | `Ce prototype accepte uniquement Pokémon HeartGold.` | stay on welcome |
| Missing private file | `Sélectionnez vos fichiers système DS pour utiliser ce mode.` | open private-file picker |
| Audio unavailable | `Audio indisponible — la partie continue sans son.` | nonblocking banner |
| Save not writable | `La sauvegarde ne peut pas être protégée. La partie est en pause.` | blocking dialog, remain paused |
| Core failure | `L’émulation s’est arrêtée. Un journal local a été conservé.` | stop and return to welcome |

No message includes an absolute ROM, BIOS, firmware, or save path.

- [ ] **Step 4: Run the complete automated suite**

```powershell
./scripts/build.ps1 -Preset ucrt-debug
./scripts/build.ps1 -Preset ucrt-release
```

Expected: all CTest and Quick Test targets pass in both presets; production and fake executables link.

- [ ] **Step 5: Perform the private real-ROM acceptance pass**

Run locally with a legally obtained HeartGold ROM that is never copied into the repository:

```powershell
./build/ucrt-release/heartgold_pc.exe --rom 'D:\Private\Pokemon HeartGold.nds'
```

Record pass/fail only in `docs/testing/manual-prototype-checklist.md` for:

- boot to playable overworld;
- upper screen visible and lower screen never visible;
- native save survives close/reopen and a backup exists before replacement;
- Tab/Start opens the menu and freezes the game;
- keyboard and one controller navigate every tile;
- controller removal keeps selection and enables keyboard prompts;
- Options changes volume, nearest/linear filtering, and fullscreen;
- Reprendre returns without ghost input;
- 1280 × 720, 1920 × 1080, and 3840 × 2160 preserve 4:3;
- process closes cleanly after ten pause/resume cycles.

Do not record the ROM hash, path, screenshots, save contents, or extracted game data.

- [ ] **Step 6: Build and inspect the Windows bundle**

`scripts/package.ps1` builds `ucrt-release`, runs CTest, copies `heartgold_pc.exe`, invokes `windeployqt6 --qmldir src/ui/qml`, copies `SDL3.dll` and required UCRT64 runtime DLLs, and places `LICENSES/` plus `THIRD_PARTY.md` beside the executable. It fails if this scan finds forbidden extensions:

```powershell
$forbidden = Get-ChildItem -Recurse $dist | Where-Object {
    $_.Extension -in '.nds','.sav','.dsv','.bin','.rom'
}
if ($forbidden) { throw "Protected/private files found in package." }
```

Run:

```powershell
./scripts/package.ps1
./dist/HeartGoldPCRemastered/heartgold_pc.exe --fake-core
```

Expected: bundle starts on a clean PATH, fake mode works, and the forbidden-file scan is empty.

- [ ] **Step 7: Commit the verified prototype**

```powershell
git add CMakeLists.txt README.md scripts/package.ps1 src/app src/ui docs/testing tests/integration
git commit -m "feat: deliver HeartGold PC Remastered vertical prototype"
git status --short
```

Expected: clean working tree after the final commit.

## Final Verification Gate

Before reporting completion, run and retain the outputs of:

```powershell
./scripts/build.ps1 -Preset ucrt-debug
./scripts/build.ps1 -Preset ucrt-release
./scripts/package.ps1
git status --short
git log --oneline --decorate -10
```

The final report must state automated test counts, manual checklist results, package location, melonDS commit, and any acceptance criterion that did not pass. It must not claim the prototype complete while any required test or manual acceptance item is unresolved.
