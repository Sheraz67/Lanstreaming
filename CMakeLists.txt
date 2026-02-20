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
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(SDL3)

# --- GoogleTest via FetchContent ---
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.15.2
    GIT_SHALLOW    TRUE
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
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

# --- Capture library (platform-specific) ---
if(LANCAST_PLATFORM_LINUX)
    add_library(lancast_capture STATIC
        src/capture/screen_capture_x11.cpp
    )
    target_include_directories(lancast_capture PUBLIC src)
    target_link_libraries(lancast_capture PUBLIC
        lancast_core
        X11::X11
        X11::Xext
        FFmpeg::swscale
        FFmpeg::avutil
    )

    add_library(lancast_audio_capture STATIC
        src/capture/audio_capture_pulse.cpp
        src/capture/mic_capture_pulse.cpp
    )
    target_include_directories(lancast_audio_capture PUBLIC src ${PULSEAUDIO_INCLUDE_DIRS} ${PULSEAUDIO_SIMPLE_INCLUDE_DIRS})
    target_link_libraries(lancast_audio_capture PUBLIC
        lancast_core
        ${PULSEAUDIO_LIBRARIES}
        ${PULSEAUDIO_SIMPLE_LIBRARIES}
    )
elseif(LANCAST_PLATFORM_MACOS)
    add_library(lancast_capture STATIC
        src/capture/screen_capture_mac.mm
    )
    target_include_directories(lancast_capture PUBLIC src)
    target_link_libraries(lancast_capture PUBLIC
        lancast_core
        FFmpeg::swscale
        FFmpeg::avutil
        ${SCREENCAPTUREKIT_FRAMEWORK}
        ${COREMEDIA_FRAMEWORK}
        ${COREVIDEO_FRAMEWORK}
        ${FOUNDATION_FRAMEWORK}
        ${COREGRAPHICS_FRAMEWORK}
    )

    add_library(lancast_audio_capture STATIC
        src/capture/audio_capture_mac.mm
    )
    target_include_directories(lancast_audio_capture PUBLIC src)
    target_link_libraries(lancast_audio_capture PUBLIC
        lancast_core
        lancast_capture
    )
endif()

# --- Render library ---
add_library(lancast_render STATIC
    src/render/sdl_renderer.cpp
)
target_include_directories(lancast_render PUBLIC src)
target_link_libraries(lancast_render PUBLIC lancast_core SDL3::SDL3)

# --- Audio Render library ---
add_library(lancast_audio_render STATIC
    src/render/audio_player.cpp
)
target_include_directories(lancast_audio_render PUBLIC src)
target_link_libraries(lancast_audio_render PUBLIC lancast_core SDL3::SDL3)

# --- Encode library ---
add_library(lancast_encode STATIC
    src/encode/video_encoder.cpp
    src/encode/audio_encoder.cpp
)
target_include_directories(lancast_encode PUBLIC src)
target_link_libraries(lancast_encode PUBLIC lancast_core FFmpeg::avcodec FFmpeg::avutil FFmpeg::swresample)

# --- Decode library ---
add_library(lancast_decode STATIC
    src/decode/video_decoder.cpp
    src/decode/audio_decoder.cpp
)
target_include_directories(lancast_decode PUBLIC src)
target_link_libraries(lancast_decode PUBLIC lancast_core FFmpeg::avcodec FFmpeg::avutil FFmpeg::swresample)

# --- App library (session orchestrators) ---
add_library(lancast_app STATIC
    src/app/host_session.cpp
    src/app/client_session.cpp
    src/app/launcher_ui.cpp
)
target_include_directories(lancast_app PUBLIC src)
target_link_libraries(lancast_app PUBLIC lancast_net lancast_capture lancast_audio_capture lancast_render lancast_audio_render lancast_encode lancast_decode)

# --- Main executable ---
add_executable(lancast src/main.cpp)
target_link_libraries(lancast PRIVATE lancast_app)

# --- Install rules ---
install(TARGETS lancast DESTINATION bin)

# --- Tests ---
enable_testing()
add_subdirectory(tests)
