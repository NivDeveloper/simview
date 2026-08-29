# Headless runtime checks: the verification the examples must never
# carry. Plain mains; a missing device prints SKIP and exits 0.
foreach(t field_check loop_check sync_check gpud_check ui_check plot_check)
    add_executable(${t} ${t}.cpp)
    target_link_libraries(${t} PRIVATE simview::simview)
    set_target_properties(${t} PROPERTIES
        CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
    if(NOT MSVC)
        target_compile_options(${t} PRIVATE -Wall -Wextra)
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
    if(t STREQUAL "sync_check")
        set_tests_properties(headless.${t} PROPERTIES LABELS "pure")
    else()
        set_tests_properties(headless.${t} PROPERTIES LABELS "device")
    endif()
endforeach()
