# water — a dam break, and the sloshing after it

A column of water is released into a closed tank. The surge runs the
length of the floor, climbs the far wall, falls back, sloshes, and
settles into a flat pool. A slider drives the tank sideways if you want
to keep it going.

```sh
make          # configures and builds; needs g++-16 for -freflection
./build/water
```

Space toggles, R restarts, Esc quits. Drag to orbit, wheel to zoom.

## The controls

The panel is the point of this example as much as the solver is. It
uses the whole widget vocabulary, and each control is the shape the
quantity actually has:

| control | what it is on | why that control |
| --- | --- | --- |
| choice | start state | four scenarios, all the same volume of water — an index cast out of a slider could name a fifth |
| vector slider | gravity | one vector, not three numbers; tilt it and the surface tilts |
| int sliders | sweeps, substeps | counts are counts, and a float slider reading "31.7" rounds where you cannot see |
| log slider | drift fix, colour ramp | useful across three decades, and a linear slider spends 90% of its travel in the top one |
| typed input | dt | a slider cannot say exactly 0.005, and a run you want to repeat needs it to |
| drag | poke impulse | unbounded on purpose: there is no principled largest poke |
| enabled group | the sway pair | greyed, not hidden — a control that vanishes never explains why |
| help markers | every parameter | a sentence each, on hover, because a panel that spells them out in full is a panel nobody reads |
| icon button | back to the opening view | a view control is a picture, and it belongs in a row rather than on a line of its own; the name it cannot show lives in the tooltip |
| tabs and sections | the three groups | twelve controls in a flat list is a list nobody reads either |
| progress | kinetic energy | a fraction against its whole reads at a glance where a number does not |

Two buttons poke the water directly: **jet** adds an upward impulse
under the middle of the tank, **swirl** adds a rotation about its axis.
Both fall off over a disc, so they are a nudge rather than a teleport.

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

## Where the energy goes

Undriven, the water settles. Kinetic energy per particle, forcing off:

| time | 0.9 s | 1.8 s | 3.0 s | 4.5 s | 6.0 s |
| --- | ---: | ---: | ---: | ---: | ---: |
| KE/particle | 0.89 | 0.70 | 0.18 | 0.10 | 0.05 |

**The FLIP blend is the dissipation dial**, and it is the only one here:
at blend 1 the particles keep everything the grid does not take back and
the water never calms; at blend 0 (pure PIC) the grid re-averages every
particle every step and the water is treacle. 0.90 is the default. There
is no physical viscosity term — at this scale real water dissipates
through turbulence a 48³ grid cannot resolve, so a viscosity here would
be a second numerical dial wearing a physical name.

**The `sway` slider drives the tank at 0.44 Hz, which is its resonant
sloshing frequency** — `2L/√(gd)` for the settled depth. Turn it up and
the wave grows every cycle until it hits the top of the domain. That is
the tank resonating, not the solver failing to dissipate.

## What it does not do

The particles are drawn as particles. Production fluid renders
reconstruct a SURFACE from them — particle depth, bilaterally blurred,
normals from the blurred depth, then refraction and depth-based
absorption — and that, not the solver, is most of what makes rendered
water look like water. A pile of shaded beads has the right shape and
the wrong material.

The transfer kernel is trilinear and the particles carry velocity only.
Production FLIP carries an affine velocity matrix as well (APIC, Jiang
et al. 2015) over a quadratic B-spline kernel, which is the standard fix
for the grainy surface and the stray fast particles visible here: FLIP's
noise lives in exactly the velocity modes the grid cannot see, and the
affine term is what gives the grid a way to see them.

## Two things worth knowing

**The first step takes a few seconds.** It compiles its kernels once.

**Every constant the solver reads lives in `Tank`, built after the
Device.** They are all leaves of device expressions, so each parks a
buffer; a file-scope tensor would be destroyed after `main` returns, when
the Device is already gone, and the process dies inside a destructor that
no stack trace usefully points at.
