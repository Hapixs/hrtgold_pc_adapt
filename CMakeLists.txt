cmake_minimum_required(VERSION 3.28)
project(HeartGoldPCRemastered VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

enable_testing()

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Qml Quick Test QuickTest)
find_package(SDL3 3.2 REQUIRED CONFIG)

add_compile_definitions(HEARTGOLD_PC_APP_NAME="HeartGold PC Remastered")

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
