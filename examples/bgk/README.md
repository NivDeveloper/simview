# bgk — a tensor sim, drawn from the buffers it evaluated into

Two discs of test particles collide off-axis in a periodic box and
thermalize. 8192 particles, thirty-odd fused expressions a step, every
one of them evaluated on the GPU, and the positions the vertex shader
reads ARE the tensors tensor wrote.

```sh
make          # configures and builds; needs g++-16 for -freflection
./build/bgk
```

Space toggles, Up/Down move the relaxation time, R restarts, Esc
quits.

## What it is showing

The seam. simview owns the device, tensor evaluates on it, and a frame
copies nothing: `sv::Sync<Vecs>` carries `Tensor<f32, N, 3>` from the
sim's thread to the frame's, and the gpud door resolves it to the
native buffer at every use. The sim runs on the Executor's thread and
the frame on the main one; the Sync is what makes "no copy" also mean
"no lock and no torn read".

The relaxation time is the physical knob: large and the discs pass
through each other, small and they thermalize on contact.

## The physics is included, not copied

`bgk.cpp` here is the VIEW. The sim is tensor's own
`examples/bgk/bgk.cpp`, included as a source — that example defines
`BGK_NO_MAIN` for exactly this purpose, and tensor's benchmark
includes it the same way. So the step this draws cannot drift from the
one tensor tests.

CMake passes the path as `BGK_SOURCE` rather than adding an include
directory, because this file has the same name as the one it includes.

## Two things worth knowing

**The first step takes seconds.** It compiles thirty-odd kernels, once.
The window opens on the initial state and the counter sits at zero
until it finishes.

**The initial state needs one const read to appear.** The compute
backend batches dispatches eagerly, and the two kernels behind the
first publish do not fill a batch — so without a read to sync them the
box is empty for as long as that first step takes. One element is
enough, and a const read keeps the device parking, so it costs one
download and nothing after it.
