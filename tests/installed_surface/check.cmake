# The clean-surface gate, as a generated consumer project: install the
# library + headers into a scratch prefix, then configure and compile
# (not link) a consumer whose ONLY include directory is that prefix —
# whatever compiler this platform defaults to, no SDL anywhere. What it
# certifies: the impl compiles bare, and the sugar is pure inline
# convenience over it.
set(PREFIX ${BUILD}/install-check-prefix)
set(CONSUMER ${BUILD}/install-check-consumer)
file(REMOVE_RECURSE ${PREFIX} ${CONSUMER})

execute_process(COMMAND ${CMAKE_COMMAND} --install ${BUILD} --prefix ${PREFIX}
                --config Release RESULT_VARIABLE r)
if(NOT r EQUAL 0)
    message(FATAL_ERROR "install failed")
endif()

# Test-only code must be ABSENT from a release build, not merely
# undeclared. sv::probe lives in its own archive that install(TARGETS)
# never names, so the question is a file, not a symbol table.
if(EXISTS ${PREFIX}/lib/libsimview_probe.a OR
   EXISTS ${PREFIX}/lib/simview_probe.lib)
    message(FATAL_ERROR "the probe archive was installed — test-only "
                        "code must never reach a consumer's prefix")
endif()

# Belt and braces, where the tool exists: the shipped library itself
# must carry no sv::probe symbol. 5probe is the Itanium mangling of
# the namespace. A missing nm is a NAMED skip — a skip and a pass must
# not read alike.
find_program(NM nm)
if(NM AND EXISTS ${PREFIX}/lib/libsimview.a)
    execute_process(COMMAND ${NM} ${PREFIX}/lib/libsimview.a
                    OUTPUT_VARIABLE syms ERROR_QUIET)
    string(FIND "${syms}" "5probe" found)
    if(NOT found EQUAL -1)
        message(FATAL_ERROR "libsimview.a exports an sv::probe symbol — "
                            "test-only code compiled into the library")
    endif()
    message(STATUS "no sv::probe symbol in the installed library")
else()
    message(STATUS "SKIP: nm not found — the symbol half of the probe "
                   "check did not run (the archive check did)")
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
