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
- **Two cameras whose lines cross shallowly on the board leave the third camera
  unchecked (turnaus#1681).** On rig-20260929, cameras 1 and 2 can cross at about 10°
  on the board. Together they place the dart only across their shared direction, so
  camera 3's line alone decides where along it the dart is, and no residual can show
  it is wrong. In window 22 camera 3's line was on another new dart (S1), and the solve
  published MISS 31 mm off the board with a chi-square of 3.3 against 9. The solve
  now states each line's **redundancy number** (`I1681CONTROL`, under
  `OD_GEO_SCORE=on`). It is the share of that line's displacement the other cameras
  can see. Below 0.1 the line is uncontrolled, and every two-line solve is
  uncontrolled by construction. `OD_SOLVE_CONTROL=on` (opt-in) sends a solve with an
  uncontrolled line **and** no placed tip within 15 mm to the vote, labelled
  DEGRADED. Measured 2026-09-29 (1555-bakeoff): 40 of 109 solves are uncontrolled, and
  the refusal fires only on rig-20260929 window 22 in both windows (MISS → D5).
  r18+r22 is unchanged at 82/86. One changed dart is not yet evidence that the refusal
  is right in general.

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
  level is still (span under 1 grey level over 333 ms, at most 3000 ms of waiting; 10 and
  90 cycles before #1685),
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

**The entry threshold (#1677), opt-in.** A quiet board starts a motion event when one
camera's board changes by more than `spike_threshold`, 0.011 of that board, frame to
frame. rig-20260929 throws three darts whose splashes stay under it: v5.3 peaks at
0.0108, v11.1 at 0.0106 and v12's D14 at 0.0074. No event opens, and the next window
is the takeout's, which reconciles CLEAN, so each of those darts is lost.
`OD_SPIKE_THRESHOLD=<ratio>` lowers only that entry, in IDLE and in the two cooldown arms.
A running event keeps 0.011. At 0.006, the capture-clock bakeoff reads rig-20260929 at
18..25/36 per window, up from 16..23, with 0 undetected. r18+r22 stays at 82/86 with 0
phantoms, and every r18 and r22 run publishes the same sequence. r18 gains one empty
window per run, which the vote refuses. It is not a default: #1353 measured rig-20260918's
noise blips at up to 0.0088, above v12's 0.0074, so no threshold separates them on
amplitude alone.

**The lone camera (#1678), opt-in.** The state vote's fresh-change floor, 0.10% of a
camera's board, is counted in the scoring area, while the tip is searched in the physical
board. A double-ring dart seen side-on has its tip inside the scoring area and its shaft
and flight outside it, so the floor sees only the tip. On rig-20260929, v6.2's D5 cleared
the floor only on camera 2. Camera 1 had 179 px in the scoring area against its 215 px
floor, but 1,366 px in the physical board, with a valid axis and the tip it reports for D5
one window later. The vote refused the dart, and the next window published D5 and S1 as
one dart. `OD_LONE_CAMERA=on` lets such a sub-floor camera corroborate another camera's
advance, when no voter reads CLEAN, if all of these hold:
- its physical-board figure clears the same floor;
- that figure fits a valid axis and yields a tip;
- the tip is in the scoring area;
- the tip is not a re-report.

A corroborating camera's tip and axis go to the scorer. The negative control is
rig-20260918's v4.3, a MISS with the same vote shape. Cameras 2 and 3 have 274 and 723 px
in the physical board with valid axes but 0 px in the scoring area, so it stays refused.
`OD_LONE_CENSUS=1` prints the per-camera figures (`I1678LONE`).

On the capture-clock bakeoff the rule fires once per rig-20260929 run and nowhere on
r18 or r22. Every r18 and r22 run publishes the same sequence, and r18+r22 stays at 82/86
with 0 phantoms. rig-20260929 reads 18..24/36 per window, up from 16..23. v6.2 publishes
as its own dart (S5, flagged with D5 as the alternative), and v6.3's S1 is correct. The
pool reads 118..130/158, up from 114..128. It is not a default: it rests on one dart and
one negative control, and the control is separated by the tip-in-scoring-area clause
alone.

**Both together, on the forced Blade 6 (#1680).** This is main 2c33747 merged with both
switches' branch, on the capture-clock bakeoff. With both switches off, the bakeoff
reproduces main row for row: 123..139/158 pooled, 82/86 on r18+r22, and rig-20260929 at
20..28/36 (dev) and 21..29/36 (opening). With `OD_SPIKE_THRESHOLD=0.006 OD_LONE_CAMERA=on`,
the pool reads **137..149/158**. r18+r22 stays at 82/86 with 0 phantoms, and rig-20260929
reads **27..33/36** (dev) and **28..34/36** (opening), with 0 undetected. Visits 5, 6 and
11 publish all three darts, and every recovered dart is exact: v5.3 S3, v6.2 D5, v6.3 S1,
v11.1 S3 and v12.2 D14. On the Blade 6, v6.2 publishes D5 on the vote path, because the
geometry refuses it as NEAR-PARALLEL. Before #1676 it published S5. What remains of the
range is visits 7 and 12, whose misses publish nothing. The visit-order join cannot place
a dart a miss leaves out, so those visits stay short.

The lone-camera rule fires once per rig-20260929 run and never on r18 or r22. The lower
threshold opens one extra window on each rig-20260918 run and none on rig-20260922 or
rig-20260929. In that window all three cameras read CLEAN between visits, and the window
re-bases the clean reference. So r18's v6.2 is still S7, but it is published by a
different path: geometry on the opening window, the vote (NEAR-PARALLEL) on dev and pin.
Every r22 run publishes the same sequence as with both switches off.

**Board counts to the rim (#1689), default.** A camera has two board counts: the
cumulative figure that says the board is occupied, and the fresh figure that says a dart
arrived. Both were taken inside the double's outer edge. On the cameras that do not see
it side-on, a dart in the double or on the surround has only its tip there, or nothing.
Live on 2026-09-30 (v0.1.13, 39 throws), every dart inside the double published and none
at or beyond it did. D16, D15 and eight misses were each held 1-2 in the STATE VOTE.

Since #1689 both counts are taken inside the physical board (`Region::tip_mask`, the
double's ellipse scaled by 225.5/170), still as shares of the scoring area, so every
threshold keeps its units. A camera whose fresh figure clears the 0.10% floor only out to
the rim votes the arrival but offers the scorer no tip and no axis (refusal `rim only`).
Before #1689 that camera stayed, with the same refusal in other words ("no fresh figure").
So the scorer gets the evidence it got before, and the vote gets the arrival.

- `OD_BOARD_COUNT=scoring` is the pin: both counts in the scoring area, as before.
- `OD_BOARD_COUNT=rim` is kept as a measurement: counted to the rim, and a rim-only
  figure is offered to the scorer too.
- Any other value means the default. Both pins log a warning.

Measured 2026-09-30 on the capture-clock bakeoff (`testers/run_all.sh 1555-bakeoff`), one
binary for every column:

| | scoring (pin) | rim | default |
|---|---|---|---|
| rig-20260918 (2 windows) | 38/40, 0 phantoms | 38/40, 0 phantoms | 38/40, 0 phantoms |
| rig-20260922 (2 windows) | 44/46, 0 phantoms | 42/46, 0 phantoms | 44/46, 0 phantoms |
| rig-20260929 dev / opening | 20..28 / 21..29 of 36 | 26..30 / 27..31 | 26..30 / 27..31 |
| pooled | 123..139/158 | 133..141/158 | **135..143/158** |

- **Gained, in every window and in the pin run.** rig-20260929 v6 publishes S20 D5 S1, all
  exact, where the scoring count published S20 MISS (D5 refused, then D5 and S1 read as
  one figure). Its v7.1 miss publishes MISS. rig-20260918's v4.3 miss, #1678's negative
  control, publishes MISS where it published nothing. A thrown miss that publishes nothing
  is also counted correct, so r18's tally does not move.
- **Nothing else moves.** Against the pin, r18 and r22 publish the same sequences apart
  from v4.3's MISS, and no run shows a phantom. On rig-20260929 no visit publishes more
  than was thrown.
- **Why `rim` is not the default.** It loses rig-20260922 v3.2, thrown D20, in both
  windows. Window 8 (cycles 778-783) is the same window on every column. Camera 1's fresh
  figure is 0.042% of the board in the scoring area and clears the floor to the rim. Under
  `rim` its axis (981 px, 1.93 px RMS) and tip reach the scorer. Two axes then solve the
  entry at r=158.3 mm, 3.7 mm (0.74 sigma) inside the ring wire, and S20 publishes flagged
  WIRE-UNCERTAIN with D20 as the alternative. With one usable axis there is no entry, and
  the DEGRADED string vote reads camera 2's D20, which is the default's answer.
- **Not reached.** rig-20260929 v5, v11 and v12 are unchanged. No event opens for those
  darts (#1677's entry threshold), so no count can see them.

The live misses cannot be replayed. In the log, the non-side-on cameras counted 0-11 px in
the scoring area for the five misses thrown at a clean board (visits 9, 10 and 11's first).
They counted 220-648 px for the misses of visits 8, 11 (third) and 13, and those counts
are cumulative, earlier darts included. How much of each lay between the double and the rim
is not in the log. The two fixture misses that now publish had the same shape: 0 px in
the scoring area and hundreds in the physical board. A miss whose silhouette stays
outside the rim on two cameras is still held.

**The lone camera beside a rim-only camera (#1707), opt-in.** Live on 2026-09-30 with the
#1689 build (issue-1689-live, runs 2 and 3, 48 throws), the new publications brought a
new error. Every wrong published score was the DEGRADED single-camera fallback ("No
consensus, using single camera score") in a window where a camera was rim-only. Misses
read S5 (radius 0.47), S18 (0.658), S3 (0.461), D3 (0.994) and D14 (0.981), and an S2 read
D2 (0.954). In the same runs the fallback was right on S20 (0.871), S3 (0.84), S12, S4,
S18 and a real D11 (0.996), so refusing the fallback whenever a camera is rim-only would
lose real darts. `OD_RIM_FALLBACK=on` separates them with two facts the window already
has:

- **Rim-carried.** The board advanced only because rim-only cameras voted: fewer cameras
  than the quorum cleared the floor in the scoring area (`DartStateResult::rim_carried`,
  logged `I1707 RIM CARRIED`). Such a dart is at or beyond the double on the cameras that
  do not see it side-on.
- **No usable line.** The solver had no usable axis at all (its TOO-FEW story reads
  "0 usable constraint(s)").

When both hold and the lone reading lies inside the double (single, treble or either
bull), its tip is the dart's barrel or flight over the board, and MISS publishes. When the
lone reading is in the double and any camera was rim-only, the score publishes flagged,
with the candidate across the nearer wire: the single inside radius 0.977 (the band's
middle, 166/170), MISS outside. Both are logged `I1707 RIM FALLBACK`.

Against the live logs:

| live reading | truth | rim-carried, no usable line | `OD_RIM_FALLBACK=on` |
|---|---|---|---|
| S5 (0.47), run 2 | miss | yes | **MISS** |
| S18 (0.658), run 2 | miss | yes | **MISS** |
| S3 (0.461), run 3 | miss | yes | **MISS** |
| S20 (0.871), run 2 | S20 | no (1 usable line) | S20 |
| S3 (0.84), run 2 | S3 | no (2 cameras in the scoring area) | S3 |
| S18 (0.75), run 3 | S18 | no (2 cameras in the scoring area) | S18 |
| D3 (0.994), run 2 | miss | no (2 cameras in the scoring area) | D3, flagged, alternative MISS |
| D14 (0.981), run 3 | miss | yes, but in the double | D14, flagged, alternative MISS |
| D2 (0.954), run 2 | S2 | no (2 cameras in the scoring area) | D2, flagged, alternative S2 |
| D11 (0.996), run 3 | D11 | no (1 usable line) | D11, flagged, alternative MISS |

These are read from the logs' refusal stories, not replayed. A camera refused "not
straight" or "not a shaft" is counted as clearing the scoring-area floor, because its
figure is only fitted after it does.

On the capture-clock bakeoff, `OD_RIM_FALLBACK=on` publishes the same sequence as the
default in all seven runs: 135..143/158 pooled, 82/86 on r18+r22, 0 phantoms. Rim-carried
windows are rig-20260918 v4.3 (both windows) and rig-20260929 v6.2 and v7.1 (both
windows). No reading turns into MISS, because both fixture misses already publish MISS.
Two readings are flagged, in both windows: rig-20260922 v3.2's D20 (radius 0.979) and
rig-20260929 v6.2's D5 (0.994), each with MISS as the alternative and each still
publishing its correct score. The fixtures cannot measure the MISS rule, because they
hold no miss read deep in the board. It stays opt-in until a live run says it holds.

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

## Detection windows in milliseconds (turnaus#1685)

Since #1685 the windows are lengths of the motion clock (`od_clock::now_ms()`), not
counts of cycles, so a window lasts the same time whatever a cycle costs. Each default is
the old count times the rig's 33.3 ms:

| window | was | now | why this length |
|---|---|---|---|
| motion settle (`motion_processing` `stability_ms`) | 15 cycles | 500 ms | half a second of quiet board, what the settle always asked for on the rig |
| exposure stillness (`exposure_ms`) | 10 cycles | 333 ms | the stretch #1646 measured an exposure walk against |
| exposure hold cap (`exposure_hold_ms`) | 90 cycles | 3000 ms | the "3 s at 30 fps" the hold was written as |
| dart window (`dart_processing` `stability_ms`) | 6 cycles | 200 ms | the settled board averaged over a fifth of a second |

- **How a cycle is counted.** A cycle's span is its `now_ms()` minus the previous cycle's.
  A window is reached on the cycle where what is still missing is less than half of that
  cycle's span (`od_clock::window_reached`: `2*elapsed + span >= 2*window`). So a window
  ends on the cycle nearest its length.
- **Why the capture clock is unchanged.** On the capture clock, N cycles span N x 33.3 ms,
  ±1 ms from the footage's integer positions. N-1 cycles fall 16 ms short. So each window
  is exactly its old count there, by construction.
- **A cycle longer than a window** makes that window one cycle: a window always holds the
  cycle it opened on. The exposure history always keeps at least two levels, because a
  span of one level is 0.
- **On a 200 ms cycle,** the settle is 2-3 cycles and the dart window 1 cycle, so the dart
  window averages one frame rather than six.
- **The pin.** `OD_WINDOW_UNIT=cycles` pins the counts. `testers/i1555_run.sh` forwards it.
- **Testers that replay files on the wall clock pin it (turnaus#1688).** A file replay runs
  as fast as the box allows, so on the wall clock a millisecond window spans however many
  cycles the box ran, and event counts move with load. `1535-rereport`, `1646-exposure` and
  `1339-denominator` set `OD_WINDOW_UNIT=cycles`. `OD_MOTION_CLOCK=capture` is not the pin:
  it also moves every other motion-clock length those testers were measured with, and
  1535's capture-clock probe found no darts. Testers that call `processDartState` directly
  with `stability_frames = 1` (1348, 1355, 1518, 1552) also set `stability_ms = 0`, so one
  call is one window in either unit. The pin does not make a file replay deterministic:
  which frame a cycle reads still depends on scheduling. Over three runs with the pin,
  1339's rig counted 27 events over its board every time and 23 or 24 over the frame.
- **1339-denominator also pins `OD_SETTLE_EXPOSURE=off`** (the motion before #1662). On its
  scaled clip the exposure hold cuts the small board's events from 33 to 19-20 and leaves
  the frame arm at 21-22, which inverts the comparison the tester makes. That is a finding
  about the hold on a small board, reported in #1688.
