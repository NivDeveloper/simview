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

## The knobs, and what they cost

The `bgk` panel carries the physics, split by when it takes effect:
relaxation time and time step act on the next step; beam speed, disc
radius, impact offset and thermal spread are what a restart is built
from, so they wait for R. Beside them is the momentum spread along
each axis, which is the one number that says whether the gas has
thermalized — the beams start at 0.8 along x and 0.15 across it, and
equal on all three is done.

The particle count, the cell count and the bin count are extents of a
tensor type, so they are the one kind of knob a panel cannot carry:
changing them is a recompile. The panel prints them FROM the
constants, because a hand-written line there said 4^3 while the build
said 8^3.

### What the deposits cost

The five per-cell deposits go into `ops::Fixed`, an integer carrier,
and not a float. A float scatter cannot be an atomic — Slang emits one
but it lowers under a capability the device does not enable — so every
accumulator is owned privately by a thread and the contributions are
scanned redundantly. An integer one can be, which is how the
hand-written versions of this method reach a million particles.

Measured here, ms per step, best of three runs of 300, with the viewer
attached and the spread diagnostic running (so these are the whole
frame's cost, not the sim's alone):

| deposit | 4^3 cells | 8^3 cells |
| --- | ---: | ---: |
| float | 31.24 | 3.99 |
| `ops::Fixed` | **1.38** | **0.83** |

Two things fall out of that table, and the second is the surprise.
Fixed is worth 22.6x at 4^3 and 4.8x at 8^3. And the float path is
nearly EIGHT TIMES FASTER with more cells, which is the opposite of
what a cost model would say: the float scatter slices its output
across workgroups, so 64 cells is one group doing all the work and 512
cells is eight. Few cells at many particles is the shape it handles
worst. With Fixed the cell count barely matters, which is the point.

The bound on the carrier is the largest magnitude one cell's total may
reach, derived from N and vmax rather than guessed — past it the
carrier wraps rather than saturating.

**The 4^3 grid is coarse, and it shows.** The collision operator acts
per cell, so with cells a quarter of the box wide the outgoing gas
carries their shape — it comes apart into lobes on the cell
boundaries. It also thermalizes too FAST, because a coarse cell mixes
particles that are nowhere near each other: at step 260 the spreads
read 0.489 / 0.481 / 0.486 at 4^3 and 0.575 / 0.440 / 0.428 at 8^3,
and the second is the more honest number. Raising C fixes both, and
costs a proportional rise in N to keep the per-cell statistics — 8^3
at 8192 particles is sixteen a cell, which is thin for a 24-bin
histogram per axis.

## It is paced on purpose

Uncapped, this sim runs about two thousand steps a second and the two
discs are gone into a thermalized gas before the window has settled —
which reads as "the colours are random", and they are: a thermalized
gas HAS uncorrelated speeds, and the colour is speed. Two hundred
steps a second puts the collision over several seconds, where the
beams are two colours and the mixing is the thing to look at. The
transport panel's rate box overrides it.

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
