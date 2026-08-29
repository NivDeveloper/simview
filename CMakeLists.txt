cmake_minimum_required(VERSION 3.24)
project(simview LANGUAGES CXX
        DESCRIPTION "A window on a running tensor simulation")

# SDL3 first: the app needs it, and finding it here is also what flips
# gpud's GPUD_BACKEND_SDL default ON when tensor fetches gpud below.
find_package(SDL3 REQUIRED CONFIG)

# tensor, hash-pinned; TENSOR_ENABLE_GPU brings gpud (itself pinned by
# tensor) and its backends. FETCHCONTENT_SOURCE_DIR_TENSOR/GPUD
# override to local checkouts for development (see Makefile).
include(FetchContent)
set(TENSOR_ENABLE_GPU ON)
set(TENSOR_BUILD_TESTS OFF)
set(TENSOR_BUILD_EXAMPLES OFF)
set(GPUD_BUILD_TESTS OFF)
FetchContent_Declare(tensor
    GIT_REPOSITORY https://github.com/NivDeveloper/tensor-dev.git
    GIT_TAG        9f80b16d145214cfe0184b778f0f27896a68d1c8)
FetchContent_MakeAvailable(tensor)

# The display shaders: slangc -> SPIR-V at build time, one entry each.
find_program(SLANGC slangc PATHS /usr/local/bin /opt/homebrew/bin REQUIRED)
set(SHADER_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)
file(MAKE_DIRECTORY ${SHADER_DIR})
foreach(entry vsmain fsmain)
    add_custom_command(
        OUTPUT ${SHADER_DIR}/${entry}.spv
        COMMAND ${SLANGC} ${CMAKE_CURRENT_SOURCE_DIR}/shaders/display.slang
                -target spirv -entry ${entry} -fvk-use-entrypoint-name
                -emit-spirv-directly -o ${SHADER_DIR}/${entry}.spv
        DEPENDS shaders/display.slang
        COMMENT "slangc ${entry}")
endforeach()
add_custom_target(shaders DEPENDS ${SHADER_DIR}/vsmain.spv ${SHADER_DIR}/fsmain.spv)

add_executable(simview src/main.cpp)
add_dependencies(simview shaders)
target_link_libraries(simview PRIVATE tensor::gpu gpud::sdl SDL3::SDL3)
target_compile_definitions(simview PRIVATE
    SIMVIEW_SHADER_DIR="${SHADER_DIR}")
set_target_properties(simview PROPERTIES
    CXX_STANDARD 26 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
target_compile_options(simview PRIVATE -freflection -Wall -Wextra)
