# xy-gpu — the stack on one device

The 2-D XY model computed by the [tensor] library on the GPU and
drawn zero-copy: simview owns the gpud device, tensor evaluates on it
through `SlotDevice`, and the field pulls the freshest resident
buffer at every draw through gpud's `source_of` protocol — three
libraries, one device, no copies, and no per-frame glue.

Standalone subproject: tensor needs `g++-16 -freflection`, so this
configures apart (`make` here; network on first configure) and builds
everything — simview included — with that one compiler. One toolchain,
one C++ runtime: gpud's virtual interface is not a stable ABI across
standard libraries.

[tensor]: https://github.com/NivDeveloper/tensor
