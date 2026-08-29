# Headless runtime checks: the verification the examples must never
# carry. Plain mains; a missing device prints SKIP and exits 0.
foreach(t field_check loop_check sync_check native_check)
    add_executable(${t} ${t}.cpp)
    target_link_libraries(${t} PRIVATE simview::simview)
    if(t STREQUAL "native_check")
        # the one test that speaks SDL directly (tests are not consumers)
        target_link_libraries(${t} PRIVATE SDL3::SDL3)
    endif()
    set_target_properties(${t} PROPERTIES
        CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
    if(NOT MSVC)
        target_compile_options(${t} PRIVATE -Wall -Wextra)
    endif()
    add_test(NAME headless.${t} COMMAND ${t})
endforeach()
