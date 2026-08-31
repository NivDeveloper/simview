add_executable(shapes shapes.cpp)
target_link_libraries(shapes PRIVATE simview::simview)
set_target_properties(shapes PROPERTIES
    CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
if(NOT MSVC)
    target_compile_options(shapes PRIVATE ${SIMVIEW_WARNINGS})
endif()

# Windows loads DLLs by name at startup; a missing SDL3.dll raises a
# modal dialog rather than an error. Keep them beside the binary.
if(WIN32)
    add_custom_command(TARGET shapes POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_RUNTIME_DLLS:shapes> $<TARGET_FILE_DIR:shapes>
        COMMAND_EXPAND_LISTS)
endif()
