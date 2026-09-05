# STANDALONE subproject: tensor needs g++-16 -freflection, so this
# example configures apart (see Makefile) and builds EVERYTHING with
# that one compiler — simview included, from source. One toolchain,
# one C++ runtime: gpud's virtual Device interface is not a stable ABI
# across standard libraries, so the mixed-runtime link is not an
# option here (gpud's own doctrine: build from source).
cmake_minimum_required(VERSION 3.24)
project(ising LANGUAGES CXX)

include(FetchContent)
set(SIMVIEW_BUILD_EXAMPLES OFF)
set(SIMVIEW_BUILD_TESTS OFF)
set(TENSOR_ENABLE_GPU ON)
set(TENSOR_BUILD_TESTS OFF)
set(TENSOR_BUILD_EXAMPLES OFF)
set(GPUD_BUILD_TESTS OFF)

# simview first: its gpud FetchContent pin declares first and wins;
# tensor's pin names the SAME gpud hash, so the second declare is a
# no-op (the two repos must always agree on the gpud version).
add_subdirectory(${SIMVIEW_ROOT} simview)
FetchContent_Declare(tensor
    GIT_REPOSITORY https://github.com/NivDeveloper/tensor-dev.git
    GIT_TAG        4a27a3c3e5971c393759363ae5ac8d45b6912397)
FetchContent_MakeAvailable(tensor)

add_executable(ising ising.cpp)
target_link_libraries(ising PRIVATE simview::simview tensor::gpu)
set_target_properties(ising PROPERTIES
    CXX_STANDARD 26 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
target_compile_options(ising PRIVATE -freflection -Wall -Wextra)
