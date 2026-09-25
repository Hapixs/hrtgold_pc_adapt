cmake_minimum_required(VERSION 3.28)
project(HeartGoldPCRemastered VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

enable_testing()

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Qml Quick Test QuickTest)
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
