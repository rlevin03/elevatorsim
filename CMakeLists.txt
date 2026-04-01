cmake_minimum_required(VERSION 3.20)
project(elevatorsim LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Helps IDEs (IntelliSense/clangd) discover include paths/defines.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Threads REQUIRED)

# Platform socket library (ws2_32 on Windows, nothing needed on POSIX).
if(WIN32)
    set(SOCKET_LIB ws2_32)
else()
    set(SOCKET_LIB "")
endif()

# Core simulation — no socket dependency.
add_library(elevator_lib STATIC elevator.cpp)
target_include_directories(elevator_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(elevator_lib PUBLIC Threads::Threads)

# Controller process (TCP server + simulation).
add_executable(elevator_controller main.cpp)
target_link_libraries(elevator_controller PRIVATE elevator_lib ${SOCKET_LIB})

# Display process (TCP client + visual UI).
# Intentionally does NOT link elevator_lib — the display only speaks the text protocol.
add_executable(elevator_display display.cpp)
target_link_libraries(elevator_display PRIVATE Threads::Threads ${SOCKET_LIB})

# ── Tests ─────────────────────────────────────────────────────────────────────

include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

enable_testing()

add_executable(elevator_tests tests/test_elevator.cpp)
target_link_libraries(elevator_tests PRIVATE elevator_lib GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(elevator_tests)
