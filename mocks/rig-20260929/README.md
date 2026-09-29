# Rig fixture, 2026-09-29

Three cameras on the maintainer's rig, same positions as `mocks/rig-20260922` (each
`cam_N` shows the same view as that fixture's `cam_N`), recorded 2026-09-29 17:14.

## Why it was recorded

A live session on the same day registered only a fraction of ~20 thrown darts, and the
live log showed two ways darts were lost: a first dart after a takeout where camera 2
saw a large change but cameras 1 and 3 stayed under the CLEAN ceiling, so the vote held
CLEAN and the clean reference was re-based over the window (#1518); and a second dart
that only camera 2 moved up on. A thrown 1 near the 18 wire also published S18 (flagged,
alternative S1). This fixture is the replayable copy of that failure.

## What is in it

180 seconds per camera, started together. The opening seconds are a clean board with
nobody in shot; after that, darts thrown and retrieved in visits.

Ground truth is not written yet — `GROUND-TRUTH.md` comes from the thrower's notes.

## How it was recorded

As `mocks/rig-20260918`: one `ffmpeg` per camera, started together, MJPG 1280x720 at
30 fps copied without re-encoding:

```
ffmpeg -f dshow -vcodec mjpeg -video_size 1280x720 -framerate 30 -rtbufsize 256M \
       -video_device_number <0|1|2> -i video="USB Camera" -t 180 -c copy raw_dev<n>.mkv
```

Device 0/1/2 became `cam_1/2/3`. Every camera delivered 5401 frames at a constant 30 fps
and ffmpeg reported no dropped frames. Transcoded to h264 CRF 20, yuv420p, no audio.
