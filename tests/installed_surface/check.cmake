# The clean-surface gate, as a generated consumer project: install the
# library + headers into a scratch prefix, then configure and compile
# (not link) a consumer whose ONLY include directory is that prefix —
# whatever compiler this platform defaults to, no SDL anywhere. What it
# certifies: the seam compiles bare, and the sugar is pure inline
# convenience over it.
set(PREFIX ${BUILD}/install-check-prefix)
set(CONSUMER ${BUILD}/install-check-consumer)
file(REMOVE_RECURSE ${PREFIX} ${CONSUMER})

execute_process(COMMAND ${CMAKE_COMMAND} --install ${BUILD} --prefix ${PREFIX}
                --config Release RESULT_VARIABLE r)
if(NOT r EQUAL 0)
    message(FATAL_ERROR "install failed")
endif()

file(WRITE ${CONSUMER}/CMakeLists.txt "
cmake_minimum_required(VERSION 3.24)
project(consumer CXX)
add_library(consumer OBJECT ${SRC}/examples/hello/hello.cpp)
target_include_directories(consumer PRIVATE ${PREFIX}/include)
set_target_properties(consumer PROPERTIES
    CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
")
execute_process(COMMAND ${CMAKE_COMMAND} -S ${CONSUMER} -B ${CONSUMER}/build
                RESULT_VARIABLE r OUTPUT_QUIET)
if(NOT r EQUAL 0)
    message(FATAL_ERROR "consumer configure failed")
endif()
execute_process(COMMAND ${CMAKE_COMMAND} --build ${CONSUMER}/build
                --config Release RESULT_VARIABLE r)
if(NOT r EQUAL 0)
    message(FATAL_ERROR "the installed surface does not compile bare — "
                        "a dependency leaked into include/simview")
endif()
message(STATUS "installed surface compiles with no SDL on any path")
