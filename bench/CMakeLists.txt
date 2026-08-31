add_executable(bench_instances instances.cpp)
target_include_directories(bench_instances PRIVATE ${CMAKE_SOURCE_DIR}/tests)
target_link_libraries(bench_instances PRIVATE simview::simview simview_probe
    SDL3::SDL3 nvrhi Vulkan::Headers)
set_target_properties(bench_instances PROPERTIES
    CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
if(NOT MSVC)
    target_compile_options(bench_instances PRIVATE ${SIMVIEW_WARNINGS})
endif()
