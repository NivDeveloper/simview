# STANDALONE subproject: tensor needs g++-16 -freflection, so this
# example configures apart (see Makefile) and builds EVERYTHING with
# that one compiler — simview included, from source. One toolchain,
# one C++ runtime: gpud's virtual Device interface is not a stable ABI
# across standard libraries, so the mixed-runtime link is not an
# option here (gpud's own doctrine: build from source).
cmake_minimum_required(VERSION 3.24)
project(xy-gpu LANGUAGES CXX)

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
    GIT_TAG        d87d0f051d8963b5c7deb2d09f26fc74ba1c9b3c)
FetchContent_MakeAvailable(tensor)

add_executable(xy-gpu xy-gpu.cpp)
target_link_libraries(xy-gpu PRIVATE simview::simview tensor::gpu)
set_target_properties(xy-gpu PROPERTIES
    CXX_STANDARD 26 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
target_compile_options(xy-gpu PRIVATE -freflection -Wall -Wextra)
