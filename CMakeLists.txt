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

# The warning set every simview target uses. -Wreorder-init-list is
# an ERROR on clang because clang ACCEPTS out-of-order designators as
# an extension while GCC and MSVC refuse them: a designator written
# out of declaration order passed the macOS gate and broke CI on both
# other platforms. Promoting it makes the local gate refuse what CI
# refuses.
set(SIMVIEW_WARNINGS -Wall -Wextra)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    list(APPEND SIMVIEW_WARNINGS -Werror=reorder-init-list)
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
# The vulkan backend is the compute half of the NVRHI renderer plan:
# one app-owned VkDevice, gpud adopting its compute queue. The sdl
# backend stays on until the swap commit removes its last caller.
set(GPUD_BACKEND_VULKAN ON)
set(GPUD_BUILD_TESTS OFF)
FetchContent_Declare(gpud
    GIT_REPOSITORY https://github.com/NivDeveloper/gpud
    # v0.8.0, pinned by hash. The tensor the GPU examples pin declares
    # gpud too, at the same hash by convention only: FetchContent is
    # first-declare-wins, and in those subprojects THIS declaration is
    # the one that counts.
    GIT_TAG        91da5eabd4fe2b3f15b5921505c0740c2f378bd6)
FetchContent_MakeAvailable(gpud)

# NVRHI: the renderer layer over the app-owned Vulkan device. Static,
# same tree, ONE build type on purpose — mixing NDEBUG between app TUs
# and NVRHI's libs skews a vulkan-hpp dispatch slot and crashes in
# Queue::Queue (measured in the adoption spike); FetchContent in this
# tree is what makes the flags agree. DX backends and install are off
# everywhere; Vulkan-Headers arrive via NVRHI's own pinned fetch.
set(NVRHI_WITH_VULKAN ON CACHE BOOL "" FORCE)
set(NVRHI_WITH_DX11 OFF CACHE BOOL "" FORCE)
set(NVRHI_WITH_DX12 OFF CACHE BOOL "" FORCE)
set(NVRHI_INSTALL OFF CACHE BOOL "" FORCE)
set(NVRHI_BUILD_SHARED OFF CACHE BOOL "" FORCE)
FetchContent_Declare(nvrhi
    GIT_REPOSITORY https://github.com/NVIDIA-RTX/NVRHI
    # cca66ae..., the spike-proven commit: adopted an app device with
    # two queue families, wrapped a foreign VkBuffer, waited and
    # signalled an app-owned timeline semaphore — zero patches.
    GIT_TAG        cca66aeb084429c880d205e508f5276b6ac13c38)
FetchContent_MakeAvailable(nvrhi)
# NVRHI never declares that its Vulkan backend depends on its core, so
# CMake cannot order the two archives for a consumer that names either
# directly (the checks link nvrhi for its headers, and GNU ld then saw
# the core before the backend that references it). Declaring the edge
# HERE makes every link line in this tree come out right; CMP0079 is
# what lets a target from a fetched directory be linked from this one.
target_link_libraries(nvrhi_vk PUBLIC nvrhi)

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
    ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp)
# Every include directory PRIVATE. The archives link PUBLIC — a static
# consumer needs their symbols — but no public header names ImGui, so
# nothing outside this build should be able to reach its headers
# either. A PUBLIC include dir here would hand every in-tree consumer
# an #include <imgui.h> that works.
target_include_directories(simview_imgui PRIVATE
    ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
# NO_PROTOTYPES: no Vulkan loader is linked anywhere in this build —
# the backend resolves entry points through ImGui_ImplVulkan_
# LoadFunctions (ui_init hands it the app's vkGetInstanceProcAddr).
target_compile_definitions(simview_imgui PUBLIC IMGUI_IMPL_VULKAN_NO_PROTOTYPES)
target_link_libraries(simview_imgui PRIVATE SDL3::SDL3 Vulkan::Headers)
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
# implot_internal.h includes imgui_internal.h, so imgui's source dir
# has to reach these TUs — named here rather than inherited, so the
# dependency is where it is used.
target_include_directories(simview_implot PRIVATE
    ${implot_SOURCE_DIR} ${imgui_SOURCE_DIR})
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

# ImPlot3D is a SEPARATE library — ImGui only, not ImPlot — with its
# own context and its own BeginPlot. It is wired exactly as ImPlot is:
# same pin-by-hash, same private include dirs, same numeric-type
# narrowing (its macro mirrors ImPlot's). The demo is NOT built for
# the same reason ImPlot's is not: it plots integer arrays that are
# undefined references under the narrowed list. The meshes TU is,
# because PlotMesh's built-in cube and sphere live there.
FetchContent_Declare(implot3d
    GIT_REPOSITORY https://github.com/brenocq/implot3d
    # v0.4, pinned by hash; latest tag as of 2026-08
    GIT_TAG        41ae3e447c0de20ecab95d38a4b4dc0835a3efc2)
FetchContent_MakeAvailable(implot3d)

add_library(simview_implot3d STATIC
    ${implot3d_SOURCE_DIR}/implot3d.cpp
    ${implot3d_SOURCE_DIR}/implot3d_items.cpp
    ${implot3d_SOURCE_DIR}/implot3d_meshes.cpp)
target_include_directories(simview_implot3d PRIVATE
    ${implot3d_SOURCE_DIR} ${imgui_SOURCE_DIR})
target_link_libraries(simview_implot3d PUBLIC simview_imgui)
target_compile_definitions(simview_implot3d PRIVATE
    "IMPLOT3D_CUSTOM_NUMERIC_TYPES=(float)(double)")
set_target_properties(simview_implot3d PROPERTIES
    CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON)
if(MSVC)
    target_compile_options(simview_implot3d PRIVATE /w /utf-8)
else()
    target_compile_options(simview_implot3d PRIVATE -w)
endif()
if(MSVC)
    target_compile_options(simview_implot PRIVATE /w /utf-8)
else()
    target_compile_options(simview_implot PRIVATE -w)
endif()

add_library(simview STATIC
    src/core/Error.cpp
    src/platform/Device.cpp
    src/platform/Swapchain.cpp
    src/platform/Vk.cpp
    src/platform/Frame.cpp
    src/platform/Input.cpp
    src/scene/Field.cpp
    src/scene/Lines.cpp
    src/scene/Particles.cpp
    src/scene/Pipelines.cpp
    src/scene/Scene.cpp
    src/ui/Context.cpp
    src/ui/Plot.cpp
    src/ui/PlotDraw.cpp
    src/ui/View.cpp
    src/door/Gpud.cpp
    src/sync/Sync.cpp)
add_library(simview::simview ALIAS simview)
target_include_directories(simview PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
# nvrhi_vk BEFORE nvrhi: GNU ld resolves static archives in one pass,
# and the backend's objects reference the core (CI's Linux leg linked
# 40 undefined CommandListResourceStateTracker symbols the other way).
target_link_libraries(simview PUBLIC gpud::gpud simview_imgui simview_implot
                              simview_implot3d
                              PRIVATE gpud::vulkan SDL3::SDL3 Vulkan::Headers
                                      nvrhi_vk nvrhi)
# NVRHI's Vulkan target propagates VK_USE_PLATFORM_WIN32_KHR, which drags
# windows.h — and its min/max macros — into every TU that sees vulkan.h.
# NVRHI compiles itself with NOMINMAX; so do we.
if(WIN32)
    target_compile_definitions(simview PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_definitions(simview_imgui PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()
target_include_directories(simview PRIVATE
    ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends ${implot_SOURCE_DIR}
    ${implot3d_SOURCE_DIR})
target_compile_definitions(simview PRIVATE
    SIMVIEW_VERSION="${PROJECT_VERSION}")
set_target_properties(simview PROPERTIES
    CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
if(NOT MSVC)
    target_compile_options(simview PRIVATE ${SIMVIEW_WARNINGS})
endif()

# Both archives: without simview_imgui the installed library has
# undefined ImGui symbols and nobody can link it.
install(TARGETS simview simview_imgui simview_implot simview_implot3d
        ARCHIVE DESTINATION lib)
install(DIRECTORY include/simview DESTINATION include)

if(SIMVIEW_BUILD_EXAMPLES)
    add_subdirectory(examples/hello)
    add_subdirectory(examples/hello-window)
    add_subdirectory(examples/gas)
    add_subdirectory(examples/ising-cpu)
    add_subdirectory(examples/plots)
endif()
if(SIMVIEW_BUILD_TESTS)
    enable_testing()

    # The test surface, in its OWN archive and deliberately absent
    # from install(TARGETS) above. That makes the boundary a LINK
    # boundary rather than a naming convention: a release build cannot
    # contain sv::probe because this target is never built and never
    # shipped, and tests/installed_surface proves the absence by
    # looking for the file.
    # It READS App's layout from core/App.h — the composed struct, the
    # union of every layer's state — and calls nothing, so it links no
    # library, only the include paths that header needs. Reordering
    # App's members changes what this TU compiles against.
    add_library(simview_probe STATIC src/testing/Probe.cpp)
    target_include_directories(simview_probe PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include ${imgui_SOURCE_DIR}
        ${implot_SOURCE_DIR} ${implot3d_SOURCE_DIR})
    target_link_libraries(simview_probe PRIVATE SDL3::SDL3 gpud::gpud nvrhi
        Vulkan::Headers)
    if(WIN32)
        target_compile_definitions(simview_probe PRIVATE NOMINMAX
            WIN32_LEAN_AND_MEAN)
    endif()
    set_target_properties(simview_probe PROPERTIES
        CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)

    add_subdirectory(tests)
endif()
