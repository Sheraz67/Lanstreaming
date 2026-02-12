cmake_minimum_required(VERSION 3.21)
project(lancast VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

# --- Platform setup ---
include(PlatformSetup)

# --- FFmpeg ---
find_package(FFmpeg REQUIRED)

# --- SDL3 via FetchContent ---
include(FetchContent)

FetchContent_Declare(
    SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG        release-3.2.8
    GIT_SHALLOW    TRUE
)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(SDL3)

# --- GoogleTest via FetchContent ---
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.15.2
    GIT_SHALLOW    TRUE
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# --- Core library (header-only + logger) ---
add_library(lancast_core STATIC
    src/core/logger.cpp
)
target_include_directories(lancast_core PUBLIC src)
target_link_libraries(lancast_core PUBLIC ${FFMPEG_LIBRARIES})
target_include_directories(lancast_core PUBLIC ${FFMPEG_INCLUDE_DIRS})

# --- Network library ---
add_library(lancast_net STATIC
    src/net/socket.cpp
    src/net/packet_fragmenter.cpp
    src/net/packet_assembler.cpp
    src/net/server.cpp
    src/net/client.cpp
)
target_include_directories(lancast_net PUBLIC src)
target_link_libraries(lancast_net PUBLIC lancast_core)

# --- Main executable ---
add_executable(lancast src/main.cpp)
target_link_libraries(lancast PRIVATE lancast_net lancast_core SDL3::SDL3)

# --- Tests ---
enable_testing()
add_subdirectory(tests)
