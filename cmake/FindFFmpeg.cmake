# FindFFmpeg.cmake - Find FFmpeg libraries
# Sets: FFMPEG_FOUND, FFMPEG_INCLUDE_DIRS, FFMPEG_LIBRARIES

include(FindPackageHandleStandardArgs)

# Platform-specific path hints
set(_ffmpeg_hints "")
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    # Homebrew hints for macOS (Apple Silicon and Intel)
    list(APPEND _ffmpeg_hints /opt/homebrew /usr/local)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    # vcpkg or manual FFmpeg install
    if(DEFINED ENV{FFMPEG_ROOT})
        list(APPEND _ffmpeg_hints $ENV{FFMPEG_ROOT})
    endif()
    if(DEFINED VCPKG_INSTALLED_DIR)
        list(APPEND _ffmpeg_hints "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")
    endif()
endif()

set(_ffmpeg_components avcodec avformat avutil swscale swresample)

foreach(_comp ${_ffmpeg_components})
    find_path(${_comp}_INCLUDE_DIR
        NAMES lib${_comp}/${_comp}.h
        PATH_SUFFIXES ffmpeg
        HINTS ${_ffmpeg_hints}
        PATH_SUFFIXES include
    )
    find_library(${_comp}_LIBRARY
        NAMES ${_comp}
        HINTS ${_ffmpeg_hints}
        PATH_SUFFIXES lib
    )

    if(${_comp}_INCLUDE_DIR AND ${_comp}_LIBRARY)
        set(${_comp}_FOUND TRUE)
        list(APPEND FFMPEG_INCLUDE_DIRS ${${_comp}_INCLUDE_DIR})
        list(APPEND FFMPEG_LIBRARIES ${${_comp}_LIBRARY})

        if(NOT TARGET FFmpeg::${_comp})
            add_library(FFmpeg::${_comp} IMPORTED INTERFACE)
            set_target_properties(FFmpeg::${_comp} PROPERTIES
                INTERFACE_INCLUDE_DIRECTORIES "${${_comp}_INCLUDE_DIR}"
                INTERFACE_LINK_LIBRARIES "${${_comp}_LIBRARY}"
            )
        endif()
    endif()
endforeach()

if(FFMPEG_INCLUDE_DIRS)
    list(REMOVE_DUPLICATES FFMPEG_INCLUDE_DIRS)
endif()

find_package_handle_standard_args(FFmpeg
    REQUIRED_VARS FFMPEG_LIBRARIES FFMPEG_INCLUDE_DIRS
    HANDLE_COMPONENTS
)
