add_executable(ising-cpu ising-cpu.cpp)
target_link_libraries(ising-cpu PRIVATE simview::simview)
set_target_properties(ising-cpu PROPERTIES
    CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
if(NOT MSVC)
    target_compile_options(ising-cpu PRIVATE -Wall -Wextra)
endif()

# Windows loads DLLs by name at startup; a missing SDL3.dll raises a
# modal dialog rather than an error. Keep them beside the binary.
if(WIN32)
    add_custom_command(TARGET ising-cpu POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_RUNTIME_DLLS:ising-cpu> $<TARGET_FILE_DIR:ising-cpu>
        COMMAND_EXPAND_LISTS)
endif()
