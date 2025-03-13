cmake_minimum_required(VERSION 3.14)
project(chord VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake" ${CMAKE_MODULE_PATH})

# Build options
option(BUILD_TESTS "Build test programs" ON)
option(BUILD_CLI "Build the CLI application" ON)

add_subdirectory(chord)
add_subdirectory(chord-dht)

if (BUILD_CLI)
    add_subdirectory(chord-cli)
endif ()

if (BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif ()