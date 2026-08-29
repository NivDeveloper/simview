cmake_minimum_required(VERSION 3.24)
project(simview VERSION 0.1.0 LANGUAGES CXX
        DESCRIPTION "A window on a running numerical simulation")

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# A sanitizer list ("address,undefined" / "thread"), applied to the
# library and everything built beside it. Empty = a normal build.
set(SIMVIEW_SANITIZE "" CACHE STRING "Sanitizers to build with")
if(SIMVIEW_SANITIZE)
    add_compile_options(-fsanitize=${SIMVIEW_SANITIZE} -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=${SIMVIEW_SANITIZE})
endif()

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

# Dear ImGui is the UI layer: the dockspace, the panels, and — through
# the official SDL3 + SDL_GPU backends — docking and multi-viewport,
# which the predecessor hand-built ~600 lines to replace because its
# custom renderer could not host them. Pinned by hash after SDL3 is
# found, since the backends need it.
FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui
    # v1.92.7-docking (IMGUI_HAS_DOCK + IMGUI_HAS_VIEWPORT)
    GIT_TAG        7d2273672596f2a2fc4d5f20c52ec1d3cb6cd2c3)
FetchContent_MakeAvailable(imgui) # upstream ships no CMakeLists: populate only

# Its own target so the warning flags are silenced HERE, deliberately
# and visibly, rather than by editing upstream's sources. imgui_demo is
# built on purpose: ShowDemoWindow is the rig that makes docking and
# tear-out verifiable by hand, and an unreferenced object never enters
# a link.
add_library(simview_imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlgpu3.cpp)
# PUBLIC imgui.h (the door speaks it), PRIVATE the backends: a consumer
# never sees an SDL type through us.
target_include_directories(simview_imgui PUBLIC ${imgui_SOURCE_DIR})
target_include_directories(simview_imgui PRIVATE ${imgui_SOURCE_DIR}/backends)
target_link_libraries(simview_imgui PRIVATE SDL3::SDL3)
set_target_properties(simview_imgui PROPERTIES
    CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON)
if(MSVC)
    # /utf-8: imgui's sources carry UTF-8 literals MSVC otherwise
    # mis-decodes, and /w would have hidden the warning that says so.
    target_compile_options(simview_imgui PRIVATE /w /utf-8)
else()
    target_compile_options(simview_imgui PRIVATE -w)
endif()

# ImPlot rides ImGui's frame and draws into its draw lists — no device
# of its own. Its own target so the numeric-type define stays scoped to
# the two TUs that need it.
FetchContent_Declare(implot
    GIT_REPOSITORY https://github.com/epezent/implot
    # v1.0, pinned by hash; the tag proven against imgui 1.92.7-docking
    GIT_TAG        524f9fcd48d76c13fdf94c5ffbba8787a1ff7e39)
FetchContent_MakeAvailable(implot)

add_library(simview_implot STATIC
    ${implot_SOURCE_DIR}/implot.cpp
    ${implot_SOURCE_DIR}/implot_items.cpp)
target_include_directories(simview_implot PUBLIC ${implot_SOURCE_DIR})
# PUBLIC: implot_internal.h includes imgui_internal.h, so imgui's source
# dir must reach every TU that includes implot's internals.
target_link_libraries(simview_implot PUBLIC simview_imgui)
# Two instantiations instead of ten: less to compile, a smaller archive,
# and the f32/f64 surface decision made physical. This is also why
# implot_demo.cpp is NOT built — it plots an ImS8 array, which under
# this list is an undefined reference that would only fire the day
# somebody linked ShowDemoWindow.
target_compile_definitions(simview_implot PRIVATE
    "IMPLOT_CUSTOM_NUMERIC_TYPES=(float)(double)")
set_target_properties(simview_implot PROPERTIES
    CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON)
if(MSVC)
    target_compile_options(simview_implot PRIVATE /w /utf-8)
else()
    target_compile_options(simview_implot PRIVATE -w)
endif()

add_library(simview STATIC
    src/impl/App.cpp
    src/engine/Draw.cpp
    src/engine/Ui.cpp
    src/sync/Sync.cpp)
add_library(simview::simview ALIAS simview)
target_include_directories(simview PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_link_libraries(simview PUBLIC gpud::gpud simview_imgui simview_implot
                              PRIVATE gpud::sdl SDL3::SDL3)
target_include_directories(simview PRIVATE ${imgui_SOURCE_DIR}/backends)
target_compile_definitions(simview PRIVATE
    SIMVIEW_VERSION="${PROJECT_VERSION}")
set_target_properties(simview PROPERTIES
    CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
if(NOT MSVC)
    target_compile_options(simview PRIVATE -Wall -Wextra)
endif()

# Both archives: without simview_imgui the installed library has
# undefined ImGui symbols and nobody can link it.
install(TARGETS simview simview_imgui simview_implot ARCHIVE DESTINATION lib)
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
