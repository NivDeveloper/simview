cmake_minimum_required(VERSION 3.24)
project(simview VERSION 0.1.0 LANGUAGES CXX
        DESCRIPTION "A window on a running numerical simulation")

option(SIMVIEW_BUILD_EXAMPLES "Build simview examples" ${PROJECT_IS_TOP_LEVEL})
option(SIMVIEW_BUILD_TESTS "Build simview tests" ${PROJECT_IS_TOP_LEVEL})

# The one library dependency, linked PRIVATE: consumers of the core
# surface never see an SDL header (CLAUDE.md's include graph; the
# install-check test proves it on every run).
find_package(SDL3 REQUIRED CONFIG)

add_library(simview STATIC
    src/seam/App.cpp
    src/engine/Draw.cpp
    src/sync/Sync.cpp)
add_library(simview::simview ALIAS simview)
target_include_directories(simview PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_link_libraries(simview PRIVATE SDL3::SDL3)
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
endif()
if(SIMVIEW_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests/installed_surface)
    add_subdirectory(tests/headless)
endif()
