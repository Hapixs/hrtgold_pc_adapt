cmake_minimum_required(VERSION 3.28)
project(HeartGoldPCRemastered VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

enable_testing()

find_program(POWERSHELL_EXECUTABLE NAMES pwsh powershell REQUIRED)
add_test(NAME tst_bootstrap_failfast COMMAND ${POWERSHELL_EXECUTABLE}
    -NoProfile -File ${CMAKE_CURRENT_SOURCE_DIR}/tests/scripts/tst_bootstrap_failfast.ps1)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets Qml Quick Test QuickTest)
find_package(SDL3 3.2 REQUIRED CONFIG)

add_compile_definitions(HEARTGOLD_PC_APP_NAME="HeartGold PC Remastered")

add_library(heartgold_core STATIC
    src/core/input/InputRouter.cpp
    src/core/rom/RomValidator.cpp
    src/core/session/SessionController.cpp
    src/core/storage/AppPaths.cpp
    src/core/storage/SaveRepository.cpp
    src/emulation/fake/FakeEmulatorCore.cpp
)
target_include_directories(heartgold_core PUBLIC src)
target_link_libraries(heartgold_core PRIVATE Qt6::Core)

qt_add_executable(heartgold_pc
    src/app/main.cpp
)
target_link_libraries(heartgold_pc PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    SDL3::SDL3
)

add_library(heartgold_sdl STATIC
    src/platform/sdl/SdlAudioSink.cpp
    src/platform/sdl/SdlApi.cpp
    src/platform/sdl/SdlRuntime.cpp
)
target_include_directories(heartgold_sdl PUBLIC src)
target_link_libraries(heartgold_sdl PUBLIC Qt6::Core SDL3::SDL3)
target_link_libraries(heartgold_pc PRIVATE heartgold_sdl)

add_library(heartgold_app STATIC src/app/AppBootstrap.cpp src/app/AppBootstrap.h)
target_include_directories(heartgold_app PUBLIC src)
target_link_libraries(heartgold_app PUBLIC heartgold_sdl heartgold_core heartgold_ui Qt6::Core)
target_link_libraries(heartgold_app PUBLIC Qt6::Widgets PRIVATE heartgold_melonds)
target_link_libraries(heartgold_pc PRIVATE heartgold_app)

qt_add_resources(heartgold_pc "qml"
    PREFIX "/HeartGoldPC"
    BASE "src"
    FILES
        src/ui/qml/Main.qml
        src/ui/qml/WelcomePage.qml
        src/ui/qml/MainMenu.qml
        src/ui/qml/OptionsPanel.qml
        src/ui/qml/components/MenuTile.qml
)

qt_add_executable(tst_bootstrap
    tests/smoke/tst_bootstrap.cpp
)
target_link_libraries(tst_bootstrap PRIVATE Qt6::Core Qt6::Test)

add_test(NAME tst_bootstrap COMMAND tst_bootstrap)

qt_add_executable(tst_rom_validator
    tests/core/tst_rom_validator.cpp
)
target_link_libraries(tst_rom_validator PRIVATE heartgold_core Qt6::Core Qt6::Test)

add_test(NAME tst_rom_validator COMMAND tst_rom_validator)

qt_add_executable(tst_save_repository
    tests/core/tst_save_repository.cpp
)
target_link_libraries(tst_save_repository PRIVATE heartgold_core Qt6::Core Qt6::Test)

add_test(NAME tst_save_repository COMMAND tst_save_repository)

qt_add_executable(tst_frame_mailbox
    tests/core/tst_frame_mailbox.cpp
)
target_link_libraries(tst_frame_mailbox PRIVATE heartgold_core Qt6::Core Qt6::Test)

add_test(NAME tst_frame_mailbox COMMAND tst_frame_mailbox)

qt_add_executable(tst_fake_emulator
    tests/emulation/tst_fake_emulator.cpp
)
target_link_libraries(tst_fake_emulator PRIVATE heartgold_core Qt6::Core Qt6::Test)

add_test(NAME tst_fake_emulator COMMAND tst_fake_emulator)

qt_add_executable(tst_session_controller
    tests/core/tst_session_controller.cpp
)
target_link_libraries(tst_session_controller PRIVATE heartgold_core Qt6::Core Qt6::Test)

add_test(NAME tst_session_controller COMMAND tst_session_controller)

qt_add_executable(tst_input_router
    tests/core/tst_input_router.cpp
)
target_link_libraries(tst_input_router PRIVATE heartgold_core Qt6::Core Qt6::Test)

add_test(NAME tst_input_router COMMAND tst_input_router)

qt_add_executable(tst_sdl_runtime
    tests/platform/tst_sdl_runtime.cpp
    src/platform/sdl/SdlAudioSink.cpp
    src/platform/sdl/SdlRuntime.cpp
)
target_include_directories(tst_sdl_runtime PRIVATE src)
target_link_libraries(tst_sdl_runtime PRIVATE Qt6::Core Qt6::Test)
add_test(NAME tst_sdl_runtime COMMAND tst_sdl_runtime)

qt_add_executable(tst_game_surface
    tests/ui/tst_game_surface.cpp
)
add_library(heartgold_ui STATIC
    src/ui/GameSurface.cpp
    src/ui/GameSurface.h
    src/ui/UiState.cpp
    src/ui/UiState.h
)
target_include_directories(heartgold_ui PUBLIC src)
target_link_libraries(heartgold_ui PUBLIC heartgold_core Qt6::Quick)
target_link_libraries(heartgold_pc PRIVATE heartgold_ui)
target_link_libraries(tst_game_surface PRIVATE heartgold_ui Qt6::Test)
add_test(NAME tst_game_surface COMMAND tst_game_surface)
set_tests_properties(tst_game_surface PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software"
)

qt_add_executable(tst_ui_state tests/ui/tst_ui_state.cpp)
target_link_libraries(tst_ui_state PRIVATE heartgold_core heartgold_ui Qt6::Core Qt6::Test)
add_test(NAME tst_ui_state COMMAND tst_ui_state)

qt_add_executable(tst_qml_main_menu tests/qml/quicktest.cpp)
target_link_libraries(tst_qml_main_menu PRIVATE Qt6::QuickTest)
add_test(NAME tst_qml_main_menu COMMAND tst_qml_main_menu -input ${CMAKE_CURRENT_SOURCE_DIR}/tests/qml)
set_tests_properties(tst_qml_main_menu PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software"
)

qt_add_executable(tst_app_bootstrap tests/app/tst_app_bootstrap.cpp)
target_link_libraries(tst_app_bootstrap PRIVATE heartgold_app Qt6::Test)
add_test(NAME tst_app_bootstrap COMMAND tst_app_bootstrap)
set_tests_properties(tst_app_bootstrap PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software"
)

include(cmake/MelonDS.cmake)
add_library(heartgold_melonds STATIC
    src/emulation/melonds/MelonDsAdapter.cpp
    src/emulation/melonds/MelonPlatform.cpp
    src/emulation/melonds/MelonRomLoader.cpp
)
target_include_directories(heartgold_melonds PUBLIC src)
target_link_libraries(heartgold_melonds PRIVATE core heartgold_core heartgold_sdl)
qt_add_executable(tst_melonds_adapter
    tests/emulation/tst_melonds_adapter.cpp
    src/emulation/melonds/testing/MelonDsTestAccess.cpp
)
target_include_directories(tst_melonds_adapter PRIVATE third_party/melonds/src)
target_link_libraries(tst_melonds_adapter PRIVATE heartgold_melonds heartgold_core heartgold_sdl Qt6::Test)
add_test(NAME tst_melonds_adapter COMMAND tst_melonds_adapter)
set_tests_properties(tst_melonds_adapter PROPERTIES TIMEOUT 30)
qt_add_executable(tst_melon_platform src/emulation/melonds/testing/tst_melon_platform.cpp)
target_include_directories(tst_melon_platform PRIVATE third_party/melonds/src)
target_link_libraries(tst_melon_platform PRIVATE heartgold_melonds Qt6::Test)
add_test(NAME tst_melon_platform COMMAND tst_melon_platform)

qt_add_executable(tst_application_flow tests/integration/tst_application_flow.cpp)
target_link_libraries(tst_application_flow PRIVATE heartgold_app Qt6::Test)
add_test(NAME tst_application_flow COMMAND tst_application_flow)
set_tests_properties(tst_application_flow PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software"
)
