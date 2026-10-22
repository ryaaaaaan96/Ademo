cmake_minimum_required(VERSION 3.30)
project(QtToolchainCheck LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Qt6 6.12.0 EXACT REQUIRED COMPONENTS
    Core Widgets SerialPort SerialBus Network Test QuickWidgets)

add_executable(qt_toolchain_check main.cpp)
target_link_libraries(qt_toolchain_check PRIVATE
    Qt6::Core Qt6::Widgets Qt6::SerialPort Qt6::SerialBus Qt6::Network)

enable_testing()
add_test(NAME qt_toolchain_runtime
    COMMAND qt_toolchain_check -platform offscreen)
