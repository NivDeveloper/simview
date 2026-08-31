add_executable(orbit orbit.cpp)
target_link_libraries(orbit PRIVATE simview::simview)
set_target_properties(orbit PROPERTIES
    CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
if(NOT MSVC)
    target_compile_options(orbit PRIVATE ${SIMVIEW_WARNINGS})
endif()

# Windows loads DLLs by name at startup; a missing SDL3.dll raises a
# modal dialog rather than an error. Keep them beside the binary.
if(WIN32)
    add_custom_command(TARGET orbit POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_RUNTIME_DLLS:orbit> $<TARGET_FILE_DIR:orbit>
        COMMAND_EXPAND_LISTS)
endif()
