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
