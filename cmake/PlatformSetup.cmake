# PlatformSetup.cmake - Platform-specific configuration

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(LANCAST_PLATFORM_LINUX TRUE)
    add_compile_definitions(LANCAST_PLATFORM_LINUX)

    # X11 (needed for screen capture)
    find_package(X11 REQUIRED)
    if(NOT X11_Xext_FOUND)
        message(FATAL_ERROR "X11 Xext extension not found. Install libxext-dev")
    endif()

    # PulseAudio (needed for audio capture)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(PULSEAUDIO REQUIRED libpulse)
    pkg_check_modules(PULSEAUDIO_SIMPLE REQUIRED libpulse-simple)

elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(LANCAST_PLATFORM_WINDOWS TRUE)
    add_compile_definitions(LANCAST_PLATFORM_WINDOWS)
    if(MSVC)
        add_compile_options(/W4 /permissive-)
        add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
    endif()

elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(LANCAST_PLATFORM_MACOS TRUE)
    add_compile_definitions(LANCAST_PLATFORM_MACOS)

    enable_language(OBJCXX)
    set(CMAKE_OBJCXX_STANDARD 20)
    set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)

    find_library(SCREENCAPTUREKIT_FRAMEWORK ScreenCaptureKit REQUIRED)
    find_library(COREMEDIA_FRAMEWORK CoreMedia REQUIRED)
    find_library(COREVIDEO_FRAMEWORK CoreVideo REQUIRED)
    find_library(FOUNDATION_FRAMEWORK Foundation REQUIRED)
    find_library(COREGRAPHICS_FRAMEWORK CoreGraphics REQUIRED)
endif()
