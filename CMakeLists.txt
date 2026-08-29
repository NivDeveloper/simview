cmake_minimum_required(VERSION 3.24)
project(simview VERSION 0.1.0 LANGUAGES CXX
        DESCRIPTION "A window on a running numerical simulation")

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(SIMVIEW_BUILD_EXAMPLES "Build simview examples" ${PROJECT_IS_TOP_LEVEL})
option(SIMVIEW_BUILD_TESTS "Build simview tests" ${PROJECT_IS_TOP_LEVEL})

# SDL3 must be found BEFORE gpud: gpud's own SDL3 probe is what turns
# its sdl backend on. Consumers of the core surface see neither
# library's headers (CLAUDE.md's include graph; the install-check test
# proves it on every run).
find_package(SDL3 REQUIRED CONFIG)

# The engine builds ON gpud (the sibling projects' GPU interchange):
# the sdl backend owns device bring-up, and gpud vocabulary is what
# the opt-in door (include/simview/gpud.h) speaks.
include(FetchContent)
set(GPUD_BACKEND_VULKAN OFF)
set(GPUD_BUILD_TESTS OFF)
FetchContent_Declare(gpud
    GIT_REPOSITORY https://github.com/NivDeveloper/gpud
    # v0.5.0, pinned by hash; examples/xy-gpu's tensor pin must agree
    GIT_TAG        8011d0216cae4ed8b24a120760edcf71cbb98c12)
FetchContent_MakeAvailable(gpud)

add_library(simview STATIC
    src/impl/App.cpp
    src/engine/Draw.cpp
    src/sync/Sync.cpp)
add_library(simview::simview ALIAS simview)
target_include_directories(simview PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_link_libraries(simview PUBLIC gpud::gpud PRIVATE gpud::sdl SDL3::SDL3)
target_compile_definitions(simview PRIVATE
    SIMVIEW_VERSION="${PROJECT_VERSION}")
set_target_properties(simview PROPERTIES
    CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
if(NOT MSVC)
    target_compile_options(simview PRIVATE -Wall -Wextra)
endif()

install(TARGETS simview ARCHIVE DESTINATION lib)
install(DIRECTORY include/simview DESTINATION include)

if(SIMVIEW_BUILD_EXAMPLES)
    add_subdirectory(examples/hello)
    add_subdirectory(examples/hello-window)
    add_subdirectory(examples/ising-cpu)
endif()
if(SIMVIEW_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests/installed_surface)
    add_subdirectory(tests/headless)
endif()
