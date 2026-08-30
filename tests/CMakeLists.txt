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
            view_check resize_check viewport_check lines_check)
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
        ${implot3d_SOURCE_DIR}
        $<TARGET_PROPERTY:gpud::sdl,INTERFACE_INCLUDE_DIRECTORIES>)
    target_link_libraries(${t} PRIVATE SDL3::SDL3 simview_probe)
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
