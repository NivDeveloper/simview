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

## The physics is reproduced here

The sim is tensor's own `examples/bgk`, written out in this file
rather than included from it, so the example reads as one file and
builds from one: the expressions, the transport and the drawing in the
order they happen.

That is a copy, with the copy's one cost — tensor's version can change
without this one noticing. The parts that were dropped are the ones
that only make sense over there: the CPU/GPU switch (this build is
always on a device), the benchmark's size and fixed-point knobs, and
the reporting `main`.

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
