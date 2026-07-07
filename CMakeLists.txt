cmake_minimum_required(VERSION 3.21)
project(KumCAD LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

option(LCAD_BUILD_TESTS "Build unit tests" ON)

add_subdirectory(src/core)
add_subdirectory(src/app)

if(LCAD_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests/core)
endif()
