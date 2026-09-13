# cloth — a hanging sheet you can cut

A square of cloth hangs by its top edge, or by its two corners, with a
ball in front of it. In the cut mode, drag across the cloth and every
spring the stroke crosses is gone. In the drag mode, drag the ball and
it goes where the pointer takes it, through the cloth if you like. A
spring stretched past its limit tears on its own, so a slit runs on
under the cloth's weight.

```sh
make          # configures and builds; needs g++-16 for -freflection
./build/cloth
```

2 and 3 enter the cut and drag modes, Esc leaves them; Space toggles,
R restarts, Esc quits; Tab flies, Ctrl+Tab aims a crosshair, F1 opens
the page that rebinds any of it. The bar at the top of the picture
lists what the device in your hands does. Drag to orbit, wheel to
zoom; in a mode the drag strokes and the right button orbits instead.
A gamepad does the same: X and Y enter the modes, B leaves, RB
toggles, LB restarts, Back flies, R3 aims, Start opens the page; the
right stick moves the cursor, A with it strokes, and in the drag mode
the triggers pull the ball toward you and push it away. The sim runs
at sixty ticks a second, real time.

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
hanging cloth within a few percent. The share each particle takes of a
pull and the tear test are once a tick, since the springs only change
between ticks: a hundred particles a side runs at fifteen milliseconds
a tick, forty at one.

The picture is two `Wire` items over one positions Sync, one per
structural family, each masked by that family's own tensor. The cut and
the drag are two modes the example declares: the cut mode's stroke
callback tests every spring's segment against the stroke through the
frame's own copy of the positions and queues the crossings for the
sim's next tick; the drag mode's carry callback moves the ball. The sim
runs on the host, which is what makes a cut exact and immediate.
