# The two fixtures, and which one proves what

There are two rigs in here and they are **not** interchangeable. Read this before
using either as evidence.

| | `cam_{1,2,3}.mp4` | `rig-20260918/cam_{1,2,3}.mp4` |
| --- | --- | --- |
| whose | inherited from upstream | the maintainer's, the intended production setup |
| red/green disc radius | **247 / 242 / 250 px** | **151 / 153 / 151 px** |
| fitted board radius | 242 / 235 / 236 px | **250 / 253 / 249 px** |
| the disc, as a fraction of the board | **~1.02** | **~0.60** |
| board area (fitted ellipse) | 183,859 px | **197,117 px** |

## The coincidence that matters

On the upstream fixture the **red/green mask happens to cover the whole board**, so
"the area the colour encloses" and "the board" are the same number within about 5%.
On a second rig they are not: the same physical board, *larger* in frame, reads as
0.60 of itself because less of it survives into the colour mask — a duller board, a
flatter light, a different camera.

**Every constant ever fitted against the upstream fixture inherited that coincidence**
without anybody choosing it. `bull_processing`'s old `minBoardAreaPercent = 0.04` was
believed to be a floor on board size; it was really a floor on *colour coverage*, and
it refused a rig whose board is bigger than the fixture's. #1340 is that, and it was
only findable by having two rigs.

## What each one is good for

**`rig-20260918/` is the fixture new detector work must satisfy.** It is a real
production setup with the ordinary imperfections — a board that does not fully survive
its own colour mask, three cameras that disagree, ~30 cm mounting on a fixed frame. Its
first fifteen seconds are a clean board for calibration; the rest is darts thrown and
retrieved.

**`cam_*.mp4` is a regression control and nothing more.** It is named by 56 places
across `testers/` and the `Makefile` and it must keep passing — a change that breaks it
has broken something. But **a change that passes on it has proved very little**, because
it is the rig every existing constant was fitted to. Passing there is the absence of a
regression, not the presence of a fix.

So an acceptance criterion that only names `cam_*.mp4` is under-specified. Say which
fixture is the evidence and which is the control, and quote numbers from both.

## The mistake this file exists to prevent

On 2026-09-18 the two disc radii — 247.8 and 151.0 — were read as *two board sizes*,
and the conclusion drawn was that the maintainer's board was too small and the cameras
should be moved closer. The whole rig was dismantled and rebuilt on that advice. The
number did not move, because it was never a board size: it was one measurement working
on one rig and failing on another, and the failing rig's board was the larger of the two.

Two rigs are what made that visible. One rig would have hidden it indefinitely.
