add_executable(hello-window hello-window.cpp)
target_link_libraries(hello-window PRIVATE simview::simview)
set_target_properties(hello-window PROPERTIES
    CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
if(NOT MSVC)
    target_compile_options(hello-window PRIVATE -Wall -Wextra)
endif()
