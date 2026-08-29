add_executable(ising-cpu ising-cpu.cpp)
target_link_libraries(ising-cpu PRIVATE simview::simview)
set_target_properties(ising-cpu PROPERTIES
    CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
if(NOT MSVC)
    target_compile_options(ising-cpu PRIVATE -Wall -Wextra)
endif()
