# cloth — a hanging sheet you can cut

A square of cloth hangs by its top edge, or by its two corners, over a
ball it can drape on. Choose the cut tool and drag across it: every
spring the stroke crosses is gone, and a spring stretched past its limit
tears on its own, so a slit runs on under the cloth's weight.

```sh
make          # configures and builds; needs g++-16 for -freflection
./build/cloth
```

Space toggles, R restarts, Esc quits. Drag to orbit, wheel to zoom;
with the cut tool on, the right button orbits instead.

## How it is built

The cloth is a grid of particles joined by six spring families:
structural to the four neighbours, shear across the two diagonals, bend
two steps away. Each family is one mask tensor, so cutting a spring is
writing a zero, and tearing is the same mask multiplied by a stretch
test. The step is position-based dynamics: predict under gravity, pull
every spring toward its rest length a few times over with every pull a
stencil over the grid, collide with the floor and the ball, read the
velocity off the move. The pulls are averaged per particle and
over-relaxed, and a tick is split into substeps, which is what holds a
hanging cloth within a few percent at under two milliseconds a tick.

The picture is two `Wire` items over one positions Sync, one per
structural family, each masked by that family's own tensor. The cut
comes from the world's stroke tool: the callback tests every spring's
segment against the stroke through the frame's own copy of the
positions and queues the crossings for the sim's next tick. The sim runs
on the host, which is what makes a cut exact and immediate.
