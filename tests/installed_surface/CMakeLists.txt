# The clean-surface gate: install the library + headers into a scratch
# prefix, then compile and run the hello consumer against ONLY that
# prefix — a bare compiler, no SDL or ImGui on any include path. What
# it certifies: the seam compiles bare and links, and the sugar is
# pure inline convenience over it.
add_test(NAME installed_surface
    COMMAND ${CMAKE_COMMAND}
        -DSRC=${CMAKE_SOURCE_DIR}
        -DBUILD=${CMAKE_BINARY_DIR}
        -DCXX=${CMAKE_CXX_COMPILER}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/check.cmake)
