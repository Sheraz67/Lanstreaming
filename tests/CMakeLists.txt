# On Windows, collect runtime DLLs to copy next to test executables
if(LANCAST_PLATFORM_WINDOWS)
    set(_test_dll_dirs "")
    if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
        list(APPEND _test_dll_dirs "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin")
    endif()
    if(DEFINED ENV{FFMPEG_ROOT})
        list(APPEND _test_dll_dirs "$ENV{FFMPEG_ROOT}/bin")
    endif()

    set(_test_runtime_dlls "")
    foreach(_dir IN LISTS _test_dll_dirs)
        file(GLOB _dlls "${_dir}/*.dll")
        list(APPEND _test_runtime_dlls ${_dlls})
    endforeach()
endif()

function(lancast_add_test name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE ${ARGN} GTest::gtest_main)
    add_test(NAME ${name} COMMAND ${name})
    if(LANCAST_PLATFORM_WINDOWS)
        if(_test_runtime_dlls)
            add_custom_command(TARGET ${name} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different ${_test_runtime_dlls} $<TARGET_FILE_DIR:${name}>
                COMMENT "Copying runtime DLLs for ${name}"
            )
        endif()
        add_custom_command(TARGET ${name} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:SDL3::SDL3-shared> $<TARGET_FILE_DIR:${name}>
            COMMENT "Copying SDL3.dll for ${name}"
        )
    endif()
endfunction()

lancast_add_test(test_ring_buffer lancast_core)

lancast_add_test(test_protocol lancast_net)
lancast_add_test(test_packet_roundtrip lancast_net)
lancast_add_test(test_large_frame_roundtrip lancast_net)
lancast_add_test(test_video_codec lancast_encode lancast_decode)
lancast_add_test(test_audio_codec lancast_encode lancast_decode)
lancast_add_test(test_phase5_protocol lancast_net)
