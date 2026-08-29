add_executable(hello hello.cpp)
target_link_libraries(hello PRIVATE simview::simview)
set_target_properties(hello PROPERTIES
    CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
target_compile_options(hello PRIVATE -Wall -Wextra)
