# flow — chaotic mixing, a quarter of a million tracers

A ball of dye is stretched and folded by a steady three-dimensional
flow, stepped on the GPU and drawn from the buffers tensor evaluated
into.

```sh
make          # configures and builds; needs g++-16 for -freflection
./build/flow
```

Space toggles, R restarts, Esc quits. Drag to orbit, wheel to zoom.

## The flow

The ABC flow (Arnold–Beltrami–Childress), the standard analytic example
of a steady 3-D flow with chaotic streamlines:

    u = A sin z + C cos y
    v = B sin x + A cos z
    w = C sin y + B cos x

Two properties make it worth drawing.

**It is exactly divergence-free.** `du/dx + dv/dy + dw/dz` cancels term
by term — not to round-off, identically — so tracers neither pile up nor
thin out however long it runs. What you see is mixing, never an artifact
of a leaky integrator.

**Its streamlines are chaotic.** Neighbouring tracers separate
exponentially, so a smooth blob is drawn out into sheets and filaments.
At A:B:C = √3 : √2 : 1 the chaotic region fills most of the box.

## Why a ball and not a full box

Seeding the whole periodic box gives a solid wall of tracers with
nothing to see into — the structure is all interior. A compact ball of
dye leaves the folding standing in empty space, which is the entire
picture. The ball is uniform by VOLUME (cube root of a uniform, not a
uniform radius) or its middle would be far denser than its shell.

The colour is a **material tag**, not a state: each tracer keeps the
colour of where it started, so lamination is visible directly as the
sheets fold through each other. Colouring by speed would be smooth here
too, but it says nothing about which parcel came from where.

## What it shows about the engine

The velocity field is **one fused expression** — the three components
differ only in which sines and cosines they pick, so each is multiplied
by a basis vector and summed, rather than branching on the component
index. One expression is one pass and one dispatch for the whole field,
where evaluating the six trig terms separately would be ten.

A step is four of those (classical RK4) plus the combine and the
periodic wrap.

Measured here: **80% compute busy**, 21223 dispatches a second, with the
frame drawing 262144 impostors in 3.24 ms of opaque pass.

The positions the vertex shader reads are the tensors tensor wrote —
`sv::Sync<Vecs>` carries them from the Executor's thread to the frame's
and the gpud door resolves them to the native buffer at every use. No
copy, no lock.

## Two things worth knowing

**The first step takes a few seconds.** It compiles its kernels once.

**The initial state needs one const read to appear.** The compute
backend batches dispatches eagerly, and the two kernels behind the first
publish do not fill a batch — so without a read to sync them the box is
empty for as long as that first step takes.
