# xy-gpu — Mode B, every boundary named

The 2-D XY model computed by the [tensor] library on the GPU (gpud's
SDL backend, adopted onto simview's own device) and drawn zero-copy:
the fragment shader reads the very buffer the compute wrote. Three
libraries meet only here; none includes another.

Standalone subproject: tensor needs `g++-16 -freflection`, simview
builds with the system compiler — so this configures apart (build the
repo root first, then `make` here; network on first configure). Both
C++ runtimes coexist in the binary — libsimview's internals are
libc++, this TU is libstdc++ — which is safe precisely because the
impl layer is POD-only.

[tensor]: https://github.com/NivDeveloper/tensor
