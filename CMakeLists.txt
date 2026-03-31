cmake_minimum_required(VERSION 3.20)
project(elevatorsim LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(elevatorsim main.cpp)

# Link pthreads on Linux/macOS; nothing extra needed on MSVC.
find_package(Threads REQUIRED)
target_link_libraries(elevatorsim PRIVATE Threads::Threads)
