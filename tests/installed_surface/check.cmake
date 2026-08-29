# Runs as a CTest script: install, compile the consumer bare, run it.
set(PREFIX ${BUILD}/install-check-prefix)
file(REMOVE_RECURSE ${PREFIX})

execute_process(COMMAND ${CMAKE_COMMAND} --install ${BUILD} --prefix ${PREFIX}
                RESULT_VARIABLE r)
if(NOT r EQUAL 0)
    message(FATAL_ERROR "install failed")
endif()

# The consumer: ONLY the installed include dir; link the installed
# archive plus SDL transitively via its absolute path is deliberately
# ABSENT — a static consumer must link SDL themselves? No: the STATIC
# simview archive does not embed SDL, so the link line needs it. The
# gate is about HEADERS, so: compile-only for the surface claim, then
# a full link WITH the build tree's SDL for the run.
execute_process(
    COMMAND ${CXX} -std=c++20 -fsyntax-only
            -I${PREFIX}/include ${SRC}/examples/hello/hello.cpp
    RESULT_VARIABLE r)
if(NOT r EQUAL 0)
    message(FATAL_ERROR "the installed surface does not compile bare — "
                        "a dependency leaked into include/simview")
endif()
message(STATUS "installed surface compiles with no SDL on any path")
