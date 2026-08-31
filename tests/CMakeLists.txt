add_subdirectory(installed_surface)

# Four kinds of thing, one folder each: harness/ is the assertion and
# image vocabulary, fakes/ the backends a display would otherwise be
# needed for, probe/ the declared test surface, checks/ the tests.
#
# Plain mains; a missing device prints SKIP and exits 0. A check is
# `pure` by being NAMED here — an exact-match test on one name silently
# filed every later device-free check under `device`, where the
# sanitizer job never runs it.
set(pure_checks sync_check)
foreach(t field_check loop_check sync_check gpud_check ui_check plot_check
            view_check resize_check viewport_check lines_check decouple_check
            hang_check timing_check world_check input_check grid_check)
    add_executable(${t} checks/${t}.cpp)
    target_link_libraries(${t} PRIVATE simview::simview)
    # Quoted includes resolve against the INCLUDING file's directory,
    # so once headers and sources live apart every one of them needs
    # this. Folder-qualified includes then say which stratum a header
    # belongs to at the point of use.
    target_include_directories(${t} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    # Tests may speak the UI stack, SDL and gpud; consumers may not.
    # Granting them HERE is what makes that a decision rather than a
    # side effect of how the library links its archives.
    # gpud::sdl is NOT linked: simview links it privately and a static
    # private link still reaches the final line, so naming it again
    # only makes the linker report a duplicate. The include dirs are
    # what tests actually lack.
    target_include_directories(${t} PRIVATE
        ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends ${implot_SOURCE_DIR}
        ${implot3d_SOURCE_DIR})
    # nvrhi + Vulkan headers: the viewport fake draws offscreen the
    # way the real backend does.
    target_link_libraries(${t} PRIVATE SDL3::SDL3 simview_probe nvrhi
        Vulkan::Headers)
    if(WIN32)
        target_compile_definitions(${t} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    endif()
    set_target_properties(${t} PROPERTIES
        CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
    if(NOT MSVC)
        target_compile_options(${t} PRIVATE ${SIMVIEW_WARNINGS})
    endif()

    # Windows resolves DLL imports at LOAD time, and a missing SDL3.dll
    # raises a modal dialog that blocks forever where no desktop can
    # dismiss it — a hang, not an error. Copy the runtime DLLs beside
    # every executable so there is nothing to miss.
    if(WIN32)
        add_custom_command(TARGET ${t} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    $<TARGET_RUNTIME_DLLS:${t}> $<TARGET_FILE_DIR:${t}>
            COMMAND_EXPAND_LISTS)
    endif()
    add_test(NAME headless.${t} COMMAND ${t})
    # Labels split what needs a GPU from what does not: `ctest -L pure`
    # is the device-free half (what the sanitizer job runs), `-LE
    # device` is how a platform that cannot host one opts out BY NAME.
    # 77 is check::skip_code: a machine that cannot host the test
    # reports Skipped, never Passed.
    set_tests_properties(headless.${t} PROPERTIES SKIP_RETURN_CODE 77)
    if(t IN_LIST pure_checks)
        set_tests_properties(headless.${t} PROPERTIES LABELS "pure")
    else()
        set_tests_properties(headless.${t} PROPERTIES LABELS "device")
    endif()
endforeach()

# timing_check needs the stamps on: the switch every gate leaves off.
set_tests_properties(headless.timing_check PROPERTIES
    ENVIRONMENT "SIMVIEW_TIMINGS=1")

# hang_check passes on the SENTENCE: a stalled frame must end in
# "waited N ms for the shot's frame", never return. Its own bound — a
# fraction of the 20 s every other gate runs under — keeps the proof
# quick; a skip still reads as a skip.
set_tests_properties(headless.hang_check PROPERTIES
    ENVIRONMENT "SIMVIEW_WAIT_MS=500"
    PASS_REGULAR_EXPRESSION "waited 500 ms for the shot's frame"
    SKIP_REGULAR_EXPRESSION "^SKIP:")

# The world's arithmetic, with no device: it includes src/world/Math.h
# directly, so it needs the src include dir the foreach checks lack —
# and it is `pure`, which is what puts it in the sanitizer job.
add_executable(world_math_check checks/world_math_check.cpp)
target_include_directories(world_math_check PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src)
set_target_properties(world_math_check PROPERTIES
    CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
if(NOT MSVC)
    target_compile_options(world_math_check PRIVATE ${SIMVIEW_WARNINGS})
endif()
add_test(NAME headless.world_math_check COMMAND world_math_check)
set_tests_properties(headless.world_math_check PROPERTIES
    SKIP_RETURN_CODE 77 LABELS "pure")

# S2's bring-up gate: the app-owned Vulkan stack with NVRHI and gpud
# both adopting it, headless. It speaks src/ internals on purpose —
# the App does not drive this stack until the swap commit — so it gets
# the src include dir and the renderer targets the foreach checks
# deliberately lack.
add_executable(platform_check checks/platform_check.cpp)
target_include_directories(platform_check PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(platform_check PRIVATE simview::simview SDL3::SDL3
    gpud::vulkan nvrhi_vk nvrhi Vulkan::Headers)
if(WIN32)
    target_compile_definitions(platform_check PRIVATE NOMINMAX
        WIN32_LEAN_AND_MEAN)
endif()
set_target_properties(platform_check PROPERTIES
    CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
if(NOT MSVC)
    target_compile_options(platform_check PRIVATE ${SIMVIEW_WARNINGS})
endif()
if(WIN32)
    add_custom_command(TARGET platform_check POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_RUNTIME_DLLS:platform_check> $<TARGET_FILE_DIR:platform_check>
        COMMAND_EXPAND_LISTS)
endif()
add_test(NAME headless.platform_check COMMAND platform_check)
set_tests_properties(headless.platform_check PROPERTIES SKIP_RETURN_CODE 77
    LABELS "device")
