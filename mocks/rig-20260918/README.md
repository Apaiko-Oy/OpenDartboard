# Rig fixture, 2026-09-18

Three cameras on the maintainer's rig — the intended production setup — recorded on
2026-09-18. A second rig beside `mocks/cam_{1,2,3}.mp4`, not a replacement for it: those
three are the control input named by 25 places across `testers/` and the `Makefile`, and
nothing here touches them.

## What is in it

Sixty seconds per camera, started together. The first fifteen seconds are a **clean board
with nobody in shot** — that half is the calibration input. The rest is darts being thrown
and retrieved, so a takeout is in there as well as arrivals.

## Why a second rig is worth carrying

The shipped mocks are one board, one room, one set of camera positions, and every
calibration constant in this repository was fitted against them — `roi_processing.cpp`
still carries `horizontalScale = 0.95f  // (was 1.1f - too wide!)` and two more like it.
A second rig is the only way to tell a constant that is right from a constant that happens
to fit. That is #1322's question, and this is the evidence it needs.

It is also a rig that **exposed real faults** on the day it was recorded: a laptop webcam
calibrating as a board camera (#1318), bull detection choosing a 205-pixel blob over the
bullseye (#1320), and a wire stage reporting twenty endpoints having found five (#1317).

## How it was recorded

Three USB cameras, OV9732-class modules (`vid_0bda&pid_5844`), all three on one host, each
captured in its own `ffmpeg` process started in the same shell:

```
ffmpeg -f dshow -vcodec mjpeg -video_size 1280x720 -framerate 30 \
       -video_device_number <0|1|2> -i video="USB Camera" -t 60 -c copy raw_cam_<n>.mp4
```

They share a friendly name, so `-video_device_number` is the only thing that tells them
apart. Captured as **MJPG at 30 fps** deliberately: the detector asks these same cameras for
MJPG and is given YUY2 at 10 fps instead (#1319), and a fixture recorded through that fault
would bake it in. Three simultaneous MJPG streams recorded with no dropped frames, which is
also what establishes that the bus is not the constraint — the fallback is.

Transcoded to h264 CRF 20, audio dropped, to match the shape of `mocks/cam_*.mp4`
(h264, 1280x720, 30 fps). 87 MB for the three against 132 MB for the shipped set.

## What it does today

All three cameras calibrate, on the build tagged `vcalibration-gate-20260917`:

```
Calibrating camera 1 ... SUCCESS - Fitted outer double ellipse from 61 boundary points
Calibrating camera 2 ... SUCCESS - Fitted outer double ellipse from 108 boundary points
Calibrating camera 3 ... SUCCESS - Fitted outer double ellipse from 81 boundary points
Initial calibration completed successfully
```

Scoring reaches `DART_PROCESSING` six times in sixty seconds. Those lines log empty — and
so do the shipped mocks' twenty-five, on the same build, which is why that is recorded here
as a property of the build rather than of this footage. Whether it should be is not this
file's question.

## What it does on `detector-integration-w128` (#1437)

The section above is true on the build it names and is still true of an **ordinary
detector run** on the integration branch: both fixtures calibrate 3 of 3, and
`1394-windows` asserts exactly that and is green. What moved is what this fixture answers
when a measurement **holds a frame** instead of seeking to one, and it moved enough that a
fixture-wide figure taken that way is not trustworthy without saying which frame it is of.

`#1378` moved `roi_processing`'s margin from 1.25 to 2.107 of the measured board, for a
reason this fixture is itself the evidence for: on this rig the colour stage measures the
**treble** ring, so at 1.25 the region cut the doubles ring and the fitted board collapsed
36.6%. The region on this rig therefore grew from about 243 px of radius to about 410 px,
and the wire stage -- which reads whatever the region kept -- now sees the number ring,
the wire ends and the wall beyond them. Its angular grouping returns a **spread** where it
used to return twenty.

Measured with `testers/i1437_wire_census.cpp`, one calibration per frame, at frames
30..450 in steps of 30 -- the fifteen clean seconds this file calls the calibration input
-- as the count of frames at which the wire stage refused the camera:

    clip            before the branch   on the branch
    cam_1.mp4                1 of 15         3 of 15
    cam_2.mp4                1 of 15         8 of 15
    cam_3.mp4                4 of 15         3 of 15

and, for the control, `mocks/cam_{1,2,3}.mp4` read 0, 3 and 6 of 15 on **both** trees. The
shipped mocks do not move because their boards fill their frames, so the widened region
runs off the frame edge and is clipped; this rig's does not.

The spread is **two-sided** -- 17, 18 and 19 appear, and so do 21 and 22 -- which is worth
knowing before reading any refusal here as a camera being marginal. `kWiresRequired` is a
one-sided threshold: more than twenty is truncated with `keeping the first 20` and fewer
than twenty refuses the camera, so the same instability is silent in one direction and
fatal in the other.

**So a measurement over this fixture states which frame it held**, and a held-frame figure
taken on this branch is a figure about that frame. #1416's ring-identity census read
`rig doubles 27/27, --, 35/35` here and the dash is this. `testers/i1437_run.sh` is what
now refuses a measurement that leaves a clip out.
