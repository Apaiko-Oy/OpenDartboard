# The deployment rig, as measured

The hardware facts constants depend on, each with its source and date. A constant in
code that encodes a rig fact should cite this page; a fact here that stops being true
gets corrected in place with a note, the way the ground-truth files do.

## Cameras

**Three OmniVision OV9732 modules** — stated by the maintainer, 2026-09-24 (recorded
on turnaus#1513). Datasheet facts that follow from the model, not yet verified on
these units:

- 1/4-inch CMOS, native **1280x720**, 1.0 MP. The rig captures at native resolution,
  so there is no crop or scale between sensor and image: pixel (u,v) is photosite
  (u,v), and the square-pixel assumption is datasheet-backed (**3.0 um** pitch,
  f_px = f_mm / 0.003).
- **Rolling shutter.** Irrelevant to static wire/ring measurement; relevant to any
  future claim about a dart in flight.
- Streams **MJPG 1280x720 @ 30 fps**; three at once on one bus, which is only
  reachable compressed — measured by #1319/#1336 (55.3 MB/s would be needed raw for
  a single camera).

**The lens is not identified exactly, but the market and the footage bound it**
(web survey + arithmetic, 2026-09-24, turnaus#1513). OV9732 USB modules ship
overwhelmingly in two lens variants: **72 degrees** (3.6 mm, f/2.4, 2G2P) and
**100 degrees** ("no deformity", low-distortion wide). The 72-degree variant is
excluded by footage already: f = 3.6 mm / 3.0 um = 1200 px would draw the doubles
ring at ~680 px radius from ~300 mm standoff, and the fixtures measure the whole
board at 194–250 px. So the rig's lens is a wide variant — f roughly 400–600 px —
and `perspective_processing.cpp`'s hard-coded 120-degree diagonal (f ≈ 424 px) is
plausibly near rather than wildly wrong. The listed "no deformity" claim, if this
is that variant, also predicts a small k1. turnaus#1560 is the footage census that
measures f and k1 per camera and settles it; a lens-barrel marking, if ever read,
corroborates for free.

**Measured, 2026-09-25 (turnaus#1560):** the radial term is real, small and the same
on every camera — **kappa = −2.45e-7 ± 0.83e-7 px⁻²** pooled over six camera-fixture
pairs at χ² 1.49 on 5 degrees of freedom, which is k1 = −0.044 ± 0.015 at f = 424 px
or −0.117 ± 0.040 at f = 690. The "no deformity" prediction above holds. **The focal
length is measured on one camera only** — rig-20260922 camera 2, the only ring
extraction clean enough to bound it, at both calibration windows: **f = 690 px, a 94°
diagonal**, ring residual 1.6 mm against that camera's own 1.1 mm extraction scatter.
The other eight camera-windows answer "not resolved" by name. So the 400–600 px
expectation above was low and the 120°/424 px in `perspective_processing.cpp` is
further off than "plausibly near" — **but one camera is one camera**, that constant is
deliberately unchanged, and flipping it is its own decision with its own evidence. The
census, the per-camera table and the verdict are in
`src/detector/geometry/calibration/lens_census.hpp`; the instrument is
`testers/i1560_k1_census.py`.

**On Windows/MSMF the FOURCC read-back is 0x00000016** (`MFVideoFormat_RGB32`'s
Data1) on all three cameras — OpenCV's own conversion target, not anything the
camera transmits. A format read there can never refuse a camera (#1336).

## Geometry

- Standoff **~300 mm** from the board, three cameras — `bull_processing.hpp`'s
  #1340 census. **Disputed, 2026-09-25 (turnaus#1560), and the dispute is worth
  understanding before anybody uses either number.** 300 mm is not a tape measure: it
  is the imaged board radius divided by an *assumed* f of 424 px. #1560 measured f on
  one camera at 690 px, and the same imaged board at 690 px stands **388 mm** off.
  The two cannot both be true and neither has been checked against the room. A tape
  measure would settle it in a minute and would also settle #1560's f, because the
  board's millimetres are known: they are the same measurement twice.
- Live orientation anchors as of 2026-09-19: `OD_CAMERA_WEDGES="9,4,3"` in `--cams`
  order. The middle camera sits on the 13/4 boundary; if it disagrees with its
  neighbours by one wedge, flip to 13.
- **The rig changed after `mocks/rig-20260918` was recorded** (maintainer,
  2026-09-19): that fixture stays valid for mechanisms and board-relative
  thresholds, but per-camera facts — anchors, calibration — must be re-read from
  the live setup.
- Camera 1 on the current setup sits near the admission threshold: twenty-fold wire
  coherence measured at 0.578 against the 0.60 gate on `mocks/rig-20260922`
  replays, admitting on some runs and not others (turnaus#1551).
  **Corrected 2026-09-25 (turnaus#1605):** it is not near a threshold. #1551 showed the
  flip was the calibration window, and #1605 found what is in that window: visit 1's
  16 stands with its barrel through camera 1's bull from f64 until the pull at
  f203-241, so the bull stage reads a half-bull 13-14 px off centre. On a clear board
  the same camera calibrates on every look (R=0.87 at the opening). A look budget
  that outlasts that dart (31 looks) was added behind `OD_LOOK_BUDGET=1605` and was not
  the default then: admitting the camera in that window cost accuracy
  (`geometry_detector.cpp`, `kFurtherLooksPastAStandingDart`).
  **Measured 2026-09-25 (turnaus#1618): that cost is the dev replay's, not the rig's.**
  A dev build (`DEBUG_SEEK_VIDEO`) seeks each file camera by 3 − 0.18·i s, frames
  90, 84 and 79 on both fixtures, and nothing re-aligns them, so every dev-window
  replay watches camera 1 six frames ahead of camera 2 and eleven ahead of camera 3.
  Once camera 1 votes, one throw is called twice. `OD_SEEK_ALIGN=1618` aligns the
  files after calibration; a release build and live cameras never seek. The
  `opening` window (`OD_SEEK_VIDEO=off`) has always been in step.
- **Both are the default since turnaus#1631.** The 31-look budget and the dev-replay
  alignment are on unless pinned off. `OD_LOOK_BUDGET=12` restores #1445's twelve looks
  (`geometry_detector.cpp`, `furtherLookBudget`), and `OD_SEEK_ALIGN=off` restores the
  staggered dev replay (`scorer.cpp`, `seekAlignIsOn`). The old opt-in words `1605` and
  `1618` are still accepted and mean the default. Each pin logs a warning when it is
  set. `OD_SEEK_ALIGN` only reaches a dev build that seeks file cameras: a release
  build and live cameras never seek, so it changes what the replays measure and not
  what a board does live. With both on, #1627's census read 68/84 (81.0%) across the
  four windows, against 65..66/84 with both off. The known losses are rig-20260922 dev
  v7.2 (a lone camera-1 reading past the 3/19 wire; #1628 found no safe rule) and
  rig-20260918 dev v5.1 (the marginal dart already recorded as flipping between
  windows).
- **A camera refused on its averaged frame seals its best look (turnaus#1456).** It is
  looked at for all of #1445's twelve looks, and to 31 (the default since #1631;
  `OD_LOOK_BUDGET=12` pins twelve) only if none of the twelve passed. It then seals the look with the highest wire-fit R,
  and a tie goes to the earliest look. `OD_LOOK_SEAL=first` restores the first look
  that passed. `rig-20260918` takes no look in either window. On `rig-20260922`,
  camera 2 seals look 9 in the dev window, as before. At the opening it seals look 4
  instead of look 3. Look 3's clip wires and printed numbers disagreed by five
  wedges; look 4's agree. Because the camera is looked at to the end of the window,
  scoring at the opening starts about 45 frames later.

## Board

Winmau Blade 6. Scoring radii are `DartboardSpec`/`BoardProfile`
(`board_model.hpp`, versioned); the 450 mm the manufacturer lists is the overall
diameter, not the scoring diameter.

**The detector takes the board to be a Blade 6 (turnaus#1676, the default).** The
maintainer's decision, "force winmau6 for now", came after `rig-20260929`. On that
recording, camera 2's clip-wire finder reported four clip wires, which made it a star
camera, and its clips put the 20 at wire 9. Its printed numbers put the 20 at wire 14,
which is also where `rig-20260922` seals the same camera. A Blade 6 has no wire number
ring, so a star measurement on this board is the finder being wrong. Such a star is now
set aside (`starSetAside`), and the reader anchors the camera. A camera whose numbers do
not read is unanchored, like any other unread camera: #1486 derives it, or
`OD_CAMERA_WEDGES` states it. `BOARD RECOGNITION` says the board is "taken as the
Winmau Blade 6" (forced). When a camera's finder still reported four clips, the line is
a WARN naming that camera. A cache written before this change is corrected the same way
at start, from the reading it holds. `OD_BOARD=auto` restores the measured board (#1498,
#1501), where clip wires anchor and the reader only agrees or disagrees.

## Fixtures

- `mocks/rig-20260918/` — evidence, with `GROUND-TRUTH.md` (21 throws, 2 misses).
- `mocks/rig-20260922/` — the deployment recording; starts with a parked dart
  (#1514), clean frame ≈ index 270 (~9 s), visit 1 is `8 16 miss` (corrected
  2026-09-24), footage ends mid-visit 8. Its `GROUND-TRUTH.md` carries the details.
  On the opening window (`OD_SEEK_VIDEO=off`) the calibration picture holds v1.1's 8.
  Until the first reconciled CLEAN, that picture is the clean reference, so the visit-1
  takeout falls by less than a dart on cameras 2 and 3 (#1648). The takeout re-report
  rule (default since turnaus#1662, below) lets a camera whose only new tip is a #1535
  re-report, and whose cumulative figure fell, vote CLEAN.
- `mocks/cam_*.mp4` — upstream footage, **never evidence** (#1478); see
  `mocks/DO-NOT-USE-cam_1-cam_2-cam_3.md`.
- A lit, clean, complete re-recording is wanted: turnaus#1558.

**Frame rate, measured 2026-09-27 (turnaus#1655).** Every fixture video, both rigs and
all three cameras, is constant-rate **30 fps**: the MP4 `stts` table holds a single
frame duration of 512/15360 s = **33.333 ms** for every frame (1800-1801 frames per
60 s on rig-20260918, 3601 per 120 s on rig-20260922). `testers/i1655_fps_probe.py`
reads it off the container; the image has no ffprobe. That is the rate the cameras were
recorded at (`-framerate 30`, MJPG, no dropped frames: `rig-20260918/README.md`), and
the rate the rig asks its devices for (`captureRateFloorDefault()` = 30 on Windows).

So `OD_MOTION_CLOCK=capture`, which runs the motion timers (cooldown, spike window,
safety timeout) on each frame's presentation time, advances them by the rig's real
frame period: a replay on it is the rig at 30 fps with a detector that keeps up with
every frame. Nothing outside `testers/` sets `OD_MOTION_CLOCK`, and `od_clock::mode()`
defaults to `wall`, so a board on live cameras runs its timers on wall time whatever a
tester does. (`frame_period_ms`, the `--fps` default of 15, is a different number: it
converts frame counts only under `OD_MOTION_CLOCK=cycle` and `OD_MOTION_FIX=spikewin`,
neither of which the bakeoff sets, and the capture clock never reads it.)

**Motion defaults since turnaus#1662.** Two measured motion switches are on unless
pinned off; both pins log a warning when set.

- **The exposure hold (#1646).** A settled event waits until every camera's board grey
  level is still (span under 1 grey level over 10 cycles, at most 90 cycles of waiting),
  so a takeout's window is not read while a camera's automatic exposure walks back.
  `OD_SETTLE_EXPOSURE=off` pins the old settle, where quiet motion is settled whatever
  the exposure does (`motion_processing.cpp`, `exposureGateOff`).
- **The takeout re-report (#1648).** A camera whose only new tip re-reports an earlier
  dart of the visit, and whose cumulative board figure fell, votes CLEAN.
  `OD_TAKEOUT_REREPORT=off` pins the old line, where such a camera abstains and the
  board still advances (`dart_processing.cpp`, `takeoutReReportIsDeparture`).

The old opt-in words `hold` and `departure` are still accepted and mean the default.
Any value other than `off` also means the default, so a typo keeps it. On the
capture-clock bakeoff (`testers/run_all.sh 1555-bakeoff`), the default reads 82/86
(95.3%). With both pins it reads 79/86 (91.9%), the default before #1662. The gain is
rig-20260922's opening: v2.1's 12, v3.1's 20 and v5.2's 19 are gained there, and its two
phantoms are gone. The other three windows publish the same darts either way.

`OD_COOLDOWN_EXPIRY=spike` (#1650) and the mask, bull and axis switches
(`OD_MASK_UNSHIFT`, `OD_BULL_SUBPIXEL`, `OD_AXIS_UNSHIFT`) stay opt-in. One risk is
recorded, not measured on the capture clock. On the wall clock, when detector cycles
run at ~37 ms (a whole-clip replay at load 4-6), the hold moves rig-20260918 dev's
cooldown expiry onto v6.3's one-cycle splash, and the 2 is dropped (#1650,
`cooldownExpirySpikes`). At 33.3 ms a cycle, the rig's 30 fps, it is not.

## Real-time replay (turnaus#1683)

A file source hands over the next frame whenever the loop asks, so the bakeoff never
drops a frame, whatever each cycle costs. A camera runs at 30 fps whatever the loop does,
and a read gets the newest frame (OpenCV's Media Foundation reader keeps one sample). A
loop slower than 33.3 ms skips frames on the rig, and never on a file.

`OD_REALTIME_REPLAY=on` (`src/utils/capture_realtime.hpp`) plays each file as a camera.
One thread per file decodes frames and publishes each one at its presentation time on
the wall clock. `read()` takes the newest published frame, and blocks only when it has
already taken that frame. The clock starts at the first scoring read. Calibration reads
one frame per read as the bakeoff does, so both calibrate on the same pictures, and
the player throws once the board is ready, as live. Every read logs `I1683RT` with the
frame each file gave, the frames it skipped, `proc_ms` (loop time since the previous
read) and `wait_ms`. The `DEBUG_VIA_VIDEO_INPUT` sleep is not slept under it.
`testers/i1683_realtime.sh` runs `mocks/rig-20260929` this way, with timers on wall time
and the opening window. `testers/i1683_visits.py` tables the darts against the truth.

It is not a camera in three ways:
- the exposure and noise are the recording's;
- a V4L2 device on Linux queues four buffers and hands over the oldest, not the newest;
- h264 decoding uses the same cores as the loop.

**Measured 2026-09-29 on rig-20260929, `--cpus=2`, host load 3.6-12.5.** Cycles took a
median of 22-50 ms.
- **Main (ac184ca + this):** 29 of 33 landed darts published in each of 3 runs, the same
  four the capture clock loses on main's census (one each in visits 5, 6 and 11, and
  visit 12's D14).
- **#1680's switches** (`OD_SPIKE_THRESHOLD=0.006 OD_LONE_CAMERA=on`): 33 of 33 in each
  of 3 runs.

So at these cycle times, real time loses nothing the capture clock keeps.

The losses come when a cycle gets slow. Two narrowed runs on main:
- `--cpus=1`, host load 7.6-18, 1000 cycles covering the whole clip: median 200 ms a
  cycle, 17 of 33 landed darts published and 4 right.
- `--cpus=0.5`, 700 cycles through visit 6: median 285 ms a cycle, 10 of 18 landed darts
  published.

The windows are counted in **cycles**, not milliseconds:
- motion settle, `stability_frames=15`;
- the exposure hold's stillness, `exposure_frames=10`;
- the dart window, `stability_frames=6`.

The cooldown is counted in wall milliseconds (1000). So one dart's event lasts about
22 cycles: 0.7 s at 33 ms a cycle, and 6 s at 285 ms. On this fixture darts land 1.8-2.1 s
apart. Once a cycle takes more than roughly 85 ms (an estimate from those figures, not
measured as a threshold), the next dart lands inside the
previous dart's event and window. The two darts become one window: one publication, often
with a wrong score, and one dart lost. The `Processing: N ms` on a `SCORE:` line is that
per-cycle time, and a live log can be read against it.
