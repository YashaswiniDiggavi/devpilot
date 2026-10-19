cmake_minimum_required(VERSION 3.20)
project(devpilot LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_options(-Wall -Wextra -Wpedantic)

find_package(CURL REQUIRED)
find_package(nlohmann_json REQUIRED)

add_library(devpilot_core
    src/process.cpp
    src/diagnostics.cpp
    src/llm.cpp
    src/patch.cpp)
target_include_directories(devpilot_core PUBLIC include)
target_link_libraries(devpilot_core PUBLIC CURL::libcurl nlohmann_json::nlohmann_json)

add_executable(devpilot src/main.cpp)
target_link_libraries(devpilot PRIVATE devpilot_core)

enable_testing()
find_package(GTest REQUIRED)
add_executable(unit_tests tests/test_diagnostics.cpp tests/test_patch.cpp)
target_link_libraries(unit_tests PRIVATE devpilot_core GTest::gtest_main)
add_test(NAME unit_tests COMMAND unit_tests)
