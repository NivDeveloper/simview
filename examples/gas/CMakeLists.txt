add_executable(gas gas.cpp)
target_link_libraries(gas PRIVATE simview::simview)
set_target_properties(gas PROPERTIES
    CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
if(NOT MSVC)
    target_compile_options(gas PRIVATE ${SIMVIEW_WARNINGS})
endif()

# Windows loads DLLs by name at startup; a missing SDL3.dll raises a
# modal dialog rather than an error. Keep them beside the binary.
if(WIN32)
    add_custom_command(TARGET gas POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_RUNTIME_DLLS:gas> $<TARGET_FILE_DIR:gas>
        COMMAND_EXPAND_LISTS)
endif()
