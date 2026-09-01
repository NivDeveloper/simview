# water — a dam break, and the sloshing after it

A column of water is released into a closed tank. The surge runs the
length of the floor, climbs the far wall, falls back and sloshes; a
slider drives the tank sideways to keep it going.

```sh
make          # configures and builds; needs g++-16 for -freflection
./build/water
```

Space toggles, R restarts, Esc quits. Drag to orbit, wheel to zoom.

## The method

FLIP/PIC (Brackbill & Ruppel; Zhu & Bridson), the standard hybrid for
liquids, on a 48³ staggered grid with 138240 particles.

The split is the whole idea. **Particles carry the velocity and do the
advection**, because advecting on a grid smears a free surface into a
haze within a few steps. **A grid carries the pressure**, because
incompressibility is a global constraint — every parcel of water must
know at once about every other — and that is a linear solve, not
something a particle can work out from its neighbours.

A step is: transfer velocity to the grid, add gravity, close the walls,
solve for the pressure that makes the velocity divergence-free, subtract
its gradient, read the grid's change back onto the particles, move them.

## Three decisions that are not free

**The velocity grid is staggered.** Component *c* lives on the cell faces
normal to axis *c*. Store all three at cell centres instead and the
discrete gradient has a null space — a pressure that alternates cell to
cell is invisible to it, so the solve happily returns one and the water
shivers in place.

**The pressure solve is red-black SOR, not Jacobi.** The pressure in a
tank is a nearly hydrostatic column, which is the slowest mode there is,
and Jacobi carries information one cell per sweep. Measured on the first
step of this scene, where the converged bottom-cell pressure is 5.02:

| sweeps | Jacobi | red-black SOR, ω = 1.85 |
| ---: | ---: | ---: |
| 10 | 0.5 | 3.64 |
| 32–40 | 1.40 | **5.00** |
| 200 | 3.38 | 5.02 |
| 4000 | 5.02 | — |

Jacobi at an affordable sweep count reaches a quarter of the answer, and
a quarter of the pressure means three quarters of gravity is never
cancelled. Same cost per sweep, same expression, one extra tensor.

**Deposits go through `ops::Fixed`.** Each contribution is quantized once
into a 64-bit fixed-point carrier and every merge after that is an
integer add, so the grid does not depend on the order particles arrive
in — and the device can use an integer atomic, where a float scatter has
none and privatizes into a single workgroup instead.

## The bug worth knowing about

FLIP updates a particle from the grid's **change**, not its value:
`v += (U_after − U_before)`. Gravity has to be inside that difference. Fold
it into `U_before` and the arithmetic still type-checks, still runs, and
still looks like water — but only the `(1 − flip)` PIC share of gravity
ever reaches a particle. At the usual blend of 0.95 that is a twentieth
of *g*, and the dam collapses in slow motion for no visible reason.

It was found by deleting the pressure solve entirely and checking free
fall against `g·t`: 0.15 m/s where 2.94 was due. With the pressure on,
every number still looked plausible.

## What it shows about the engine

The three velocity components are **one tensor and one dispatch**. Each
particle splats onto eight nodes in all three staggerings at once: the
component index is the one free index the scatter does not consume, so it
survives as the result's last axis. The gather back is the same eight
weights read the other way round, one fold.

`inside` — one tensor, 1 on the real cells and 0 on a padding slice — is
the entire tank. Every wall condition is a multiplication by it.

Measured here: **2.27 ms per step** (48³ grid, 138240 particles, 40 SOR
sweeps, warm cache, Apple GPU via gpud), so the default two substeps cost
4.5 ms of a frame. The pressure sweeps are 80 of the ~100 dispatches a
step; halving them roughly halves the step. A 32³ grid with 63232
particles runs at 1.09 ms, a 64³ grid with 327680 at 5.04 ms.

## Two things worth knowing

**The first step takes a few seconds.** It compiles its kernels once.

**Every constant the solver reads lives in `Tank`, built after the
Device.** They are all leaves of device expressions, so each parks a
buffer; a file-scope tensor would be destroyed after `main` returns, when
the Device is already gone, and the process dies inside a destructor that
no stack trace usefully points at.
