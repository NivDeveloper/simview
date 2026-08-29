# STANDALONE subproject: needs tensor's compiler (g++-16 -freflection)
# while simview builds with the system compiler — configure it
# separately (see Makefile here). It is also the impl-ABI proof: this
# g++/libstdc++ consumer links the AppleClang/libc++ libsimview.a,
# which works because the impl is POD-only by law.
cmake_minimum_required(VERSION 3.24)
project(xy-gpu LANGUAGES CXX)

find_package(SDL3 REQUIRED CONFIG)

include(FetchContent)
set(TENSOR_ENABLE_GPU ON)
set(TENSOR_BUILD_TESTS OFF)
set(TENSOR_BUILD_EXAMPLES OFF)
set(GPUD_BUILD_TESTS OFF)
FetchContent_Declare(tensor
    GIT_REPOSITORY https://github.com/NivDeveloper/tensor-dev.git
    GIT_TAG        1bdaf9daaf6709e39fb8c31d0735acc3a0fc8d98)
FetchContent_MakeAvailable(tensor)

add_executable(xy-gpu xy-gpu.cpp)
target_include_directories(xy-gpu PRIVATE ${SIMVIEW_ROOT}/include)
# Both C++ runtimes link into this binary: libsimview.a's internals
# use libc++ (AppleClang built it), this TU uses libstdc++ (g++). The
# impl's POD-only law is what makes that safe — no std type crosses
# between them — and libc++'s inline namespace keeps symbols apart.
target_link_libraries(xy-gpu PRIVATE
    ${SIMVIEW_ROOT}/build/libsimview.a
    tensor::gpu gpud::sdl SDL3::SDL3 c++)
set_target_properties(xy-gpu PROPERTIES
    CXX_STANDARD 26 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
target_compile_options(xy-gpu PRIVATE -freflection -Wall -Wextra)
