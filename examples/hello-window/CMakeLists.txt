add_executable(hello-window hello-window.cpp)
target_link_libraries(hello-window PRIVATE simview::simview)
set_target_properties(hello-window PROPERTIES
    CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
if(NOT MSVC)
    target_compile_options(hello-window PRIVATE -Wall -Wextra)
endif()

# Windows loads DLLs by name at startup; a missing SDL3.dll raises a
# modal dialog rather than an error. Keep them beside the binary.
if(WIN32)
    add_custom_command(TARGET hello-window POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_RUNTIME_DLLS:hello-window> $<TARGET_FILE_DIR:hello-window>
        COMMAND_EXPAND_LISTS)
endif()
