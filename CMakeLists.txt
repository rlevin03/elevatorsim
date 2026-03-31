cmake_minimum_required(VERSION 3.20)
project(elevatorsim LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Helps IDEs (IntelliSense/clangd) discover include paths/defines.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Threads REQUIRED)

# Core simulation as a static library so both the executable and tests can link it.
add_library(elevator_lib STATIC elevator.cpp)
target_include_directories(elevator_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(elevator_lib PUBLIC Threads::Threads)

add_executable(elevatorsim main.cpp)
target_link_libraries(elevatorsim PRIVATE elevator_lib)

# ── Tests ─────────────────────────────────────────────────────────────────────

include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
)
# Prevents GTest from overriding compiler/linker settings on MSVC.
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

enable_testing()

add_executable(elevator_tests tests/test_elevator.cpp)
target_link_libraries(elevator_tests PRIVATE elevator_lib GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(elevator_tests)
