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

Two testers pin `OD_BOARD_COUNT=scoring`, because what they measure is another rule under
the counts it was measured with:
- `1648-takeout`. Counted to the rim, rig-20260922's visit-1 takeout reconciles CLEAN
  without #1648's rule: cameras 1 and 3 read CLEAN, 2 of 3. The pinned-off arm would no
  longer show what the rule is for.
- `1535-rereport`. Its wall-clock probes then carry rig-20260918 v7.3, a real 20 beside
  v7.2's 20. Camera 2's tip for it is 2 px from the tip it reported for v7.2, with its
  fresh figure 130 px away. #1535's rule therefore rules it a re-report, no camera keeps a
  tip, and it publishes MISS@0.5 where `OD_TIP_IDENTITY=off` publishes S20@0.7. That is
  #1535's rule misreading an adjacent dart, which #1689 made reachable. It was measured
  twice, and the capture-clock bakeoff does not reach v7.3.

`1358-window`'s falsification (`OD_DART_WINDOW=settle`) reads 3-4 of 6 windows wholly
empty where it asserts 5. It reads 3 with the counts pinned to the scoring area too, and
its window cycles move between runs (a wall-clock replay), so it is not this change.

**A held arrival and the working background (#1690), opt-in.** When the vote holds a board
that is not CLEAN while a camera moved up, nothing re-bases, so the refused figure stays in
that camera's next fresh figure. Live on 2026-09-30 (v0.1.13) visit 13, the miss was held
1-2 (camera 1 19,263 px, cameras 2 and 3 574 and 648), and camera 1 then read the miss and
the D7 as one figure (397 x 197 px, 31 px RMS) and published S15. `OD_HELD_REBASE=on`
re-bases every camera's working background to the held window, as an advance does (#1495),
and logs `I1690 HELD REBASE`. A hand or shadow that one camera alone saw in the settled
window is absorbed too; its leaving is then one camera's fresh figure, which the quorum
holds again, and that held window re-bases back.

Measured 2026-10-01 on the capture-clock bakeoff, one binary for every column (the
`issue-1691` tree, which carries both switches):

| | default | default, both switches | scoring pin | scoring pin + `OD_HELD_REBASE=on` |
|---|---|---|---|---|
| rig-20260918 (2 windows) | 38/40, 0 phantoms | 38/40, 0 phantoms | 38/40, 0 phantoms | 38/40, 0 phantoms |
| rig-20260922 (2 windows) | 44/46, 0 phantoms | 44/46, 0 phantoms | 44/46, 0 phantoms | 44/46, 0 phantoms |
| rig-20260929 dev / opening | 26..30 / 27..31 of 36 | 26..30 / 27..31 | 20..28 / 21..29 | 20..29 / 21..30 |
| pooled | 135..143/158 | 135..143/158 | 123..139/158 | 123..141/158 |
| held votes with a camera moving up, per r18 / r22 / r29 run | 0 / 0 / 0 | 0 / 0 / 0 | 1 / 0 / 2 | 1 / 0 / 2 |

- **On the default count it never fires.** No vote on any fixture holds an arrival, and the
  default with both switches publishes exactly what the default does, in all seven replays.
- **On the scoring pin it fires once per r18 run and once per r29 run.** r18's is v4.3's
  miss, held at DART_2; the next window is the takeout, so nothing publishes differently.
  r29's is v6.2's D5, held at DART_1 (camera 2 22,233 px, cameras 1 and 3 595 and 360). The
  next window then publishes S1 (geometric, SOLVED, 2 constraints) where the pin published
  MISS for D5 and S1 together, so v6.3 is exact in both windows. r29's other held vote is
  at CLEAN (#1691's case), which this switch does not touch.
- **Live on #1689.** Runs 1-3 of 2026-09-30 (the #1689 build, 48 called throws plus an
  unannotated run) hold one arrival, at CLEAN (run 2 visit 7's miss). None is this switch's
  case. On v0.1.13 the same day, five of 39 throws were held at DART_n, one of which (visit
  13) cost the next dart its score.

It stays opt-in: on the default count, which #1689 made, it has nothing to act on in the
fixtures or the live runs. Its case is an arrival the rim count still cannot carry (a miss
a second camera does not see even to the rim), and on the scoring pin, where that case
exists, it is strictly better by one dart.

**A held arrival and the clean reference (#1691), opt-in.** When the vote reconciles CLEAN,
every camera re-bases its clean reference to the window (#1518), including a camera that
voted a dart the quorum refused. Live on 2026-09-30 (v0.1.13) five arrivals were held at
CLEAN that way (visits 9-11's misses, camera 2 or 3 at 5,508-17,658 px, the others 0-11),
and each camera adopted the miss as clean. `OD_CLEAN_ADOPT=agreed` lets only a camera that
voted CLEAN adopt: a CLEAN candidate, a reversion (#1518) or #1552's memory. A voter that
voted a dart keeps its reference, its working background takes the window (so its next
fresh figure is only what arrives next), and it logs `I1691 CLEAN REFERENCE KEPT`. It adopts
at the next CLEAN it votes itself. The issue's wording, "above the CLEAN ceiling", is not
the test: at #1514's takeout every camera is above the ceiling on the old reference and
votes CLEAN by reversion, and a ceiling test would wedge the board at DART_3 again.

Measured 2026-10-01 on the same binary and bakeoff as #1690 above:

| | default | default, both switches | scoring pin | scoring pin + `OD_CLEAN_ADOPT=agreed` |
|---|---|---|---|---|
| rig-20260918 (2 windows) | 38/40, 0 phantoms | 38/40, 0 phantoms | 38/40, 0 phantoms | 38/40, 0 phantoms |
| rig-20260922 (2 windows) | 44/46, 0 phantoms | 44/46, 0 phantoms | 44/46, 0 phantoms | 44/46, 0 phantoms |
| rig-20260929 dev / opening | 26..30 / 27..31 of 36 | 26..30 / 27..31 | 20..28 / 21..29 | 20..28 / 21..29 |
| pooled | 135..143/158 | 135..143/158 | 123..139/158 | 123..139/158 |
| `I1691` kept, per r18 / r22 dev / r22 opening / r29 run | 0 | 0 / 0 / 0 / 0 | 0 | 0 / 0 / 1 / 1 |

- **On the default count it never fires**, and no replay publishes differently.
- **On the scoring pin it fires once per r29 run and once on r22's opening window, and no
  publication moves.** r29's is v7.1's miss, held at CLEAN (camera 2 12,569 px); v7 then
  publishes S20 S12 as without it, and the takeout adopts on all three cameras.
- **#1514 holds.** r22's firing is #1514's own takeout, visit 1's, where the parked dart's
  hole stays on the reference: cameras 1 and 2 vote CLEAN by reversion and adopt, camera 3
  (1,084 px, over its 182 px ceiling) votes DART_2 and keeps the calibration reference. The
  board reconciles CLEAN and publishes END, and at the next takeout camera 3 reverts
  (3,201 to 1,075 px) and adopts. Every r22 run publishes 7 (dev) and 8 (opening) ENDs, as
  without it. `testers/run_all.sh 1514-stall` passes under `OD_BOARD_COUNT=scoring
  OD_CLEAN_ADOPT=agreed` (28 windows, 9 with a CLEAN camera, 7 ENDs); it replays the dev
  window, where the switch does not fire.
- **Live on #1689.** Runs 1-3 hold one arrival at CLEAN (run 2 visit 7's miss, camera 2
  2,412 px, cameras 1 and 3 none). Every camera adopted, and the visit's T5 and 14 then
  published correctly and the next takeout reconciled normally. On v0.1.13 none of the five
  adoptions cost a later dart its score either: an adopted miss stays in "clean" only until
  the next reconciled CLEAN, which is the takeout that removes it, not for the evening.

It stays opt-in. It changes no publication on any fixture or on either count, and no live
run shows the adoption doing harm; it rests on reasoning, not a measured loss.

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

**A doubles span the ring identity calls TREBLE (#1748), default.** Live on 2026-10-08
(68 visits, `opendartboard-main-0fb2ca3-windows-x64`, log `od-live-20261008`), thrown 19s
published as 3s: of the 262 `SCORE:` lines, eight S3/T3 publications sat at board angles
180.1-188.2° where the 19 is 189-207° (radii 0.47-0.74), the six flagged against a wedge
wire at claimed sigmas of 12.2-16.5 mm where the 20-wedge solves of the same session
claim 5-6 mm, and one unflagged at 0.9 ("S3 clears its nearest wedge wire -- 17.9 mm away
across a 16.5 mm one-sigma", radius 0.741, angle 180.8°). Every geometric score in the
session was "from 2 intersecting constraint(s) of 3 cameras, read through camera 1" --
and that `1` was a 0-based constraint index printed raw (rig-20260929's clean run says
`camera 0` on 23 of its 24 solves; every other sentence in a log numbers cameras from 1),
so it names camera 2. Camera 1 was not in any solve.

Camera 1's calibration block, read together, says why:

- `bull at (646,232) ... board radius 334.9 px`; `rings: every ring is where the board
  puts it -- the bullseye 0.0379 ... the 25 ring 0.0920 ... treble inner 0.5467 ... treble
  outer 0.6173 ... doubles inner 0.9311` (each against the traced ring at x1.0);
- `wire model: the fitted conic is 330 px where 532 px is this board's radius by its ring
  identity, so the ring that was traced is the TREBLE ring; the plane is built at
  1.588785 of it` (197 times, once per fit);
- `board model: scale through #1423's ring identity ... 2.46 px/mm at the bull ...
  residuals the bullseye [held-out] -2.4 mm, the 25 ring [held-out] -6.3 mm, the treble
  ring's inner edge [held-out] -41.6 mm, the treble ring's outer edge [held-out] -42.5 mm,
  the doubles ring's inner edge [fitted] -66.4 mm, the doubles ring [fitted] -65.3 mm`,
  and -- past the 700th character, where the issue stopped reading -- `REJECTED: the
  bullseye sits at 4.0 mm in model space where its band is 4.0..10.0 mm; the 25 ring sits
  at 9.6 mm where its band is 10.0..39.7 mm; the treble ring's outer edge sits at 64.5 mm
  where its band is 102.9..131.7 mm`;
- `SCORING: 3 of 3 cameras can be scored from` all the same, because that census read
  the ring and the wire count and not the fit; and in every TOO-FEW story camera 1's slot
  reads `cam 1: no accepted board fit, so nothing this camera saw can be placed on the
  board` (39 of them).

A bull at 0.0379 of the traced ring puts that ring at 6.35/0.0379 = 168 mm: the trace was
the doubles ring, and so was the span (335 vs 330 px). `ring_identity::identify` called
the span TREBLE because colour reached past 1.26 spans on a tenth of its rays -- the room
(#1731's red), which no fixture frame has -- and `conicOfDoublesFor` believed the identity
over the trace, so the plane's 170 mm sat at 524 px and every ring read at 1/1.589 of
itself: 170 x (1/1.589 - 1) = -63 mm on the fitted ring, which is the -65 the fit printed.
The fit REFUSED it, by #1485's held-out bands: the treble edges neighbour each other, so
their bands are narrow and the outer treble at 1/1.589 of itself (64.5 mm) is 38 mm
below its band; the bull and 25 ring were refused by a hair (4.0 vs 4.0, 9.6 vs 10.0),
their bands being 2.5x and 4x wide. So the issue's premise -- an accepted fit read
through on every dart -- is wrong, and so is the brief's "a uniform shrink stays in
band": it does for the bull and the 25 ring, and not for a treble pair. What the wrong
plane cost live was camera 1 as a constraint, on all 154 solves and the 39 TOO-FEW
windows: the 3/19 publications are two-camera solves of cameras 2 and 3, whose sigma on
that wedge's wire is 12-16 mm. The same wrong plane is what `ANCHOR: camera 1 is REFUSED
an anchor: two darts placed its wedge 20 at different wires` saw from the anchor's side
(the derived anchor reads the wire ring, which the vote path still scores from).

**Which camera it is.** The live log's camera 1 is `mocks/rig-20260929/cam_2.mp4`'s
camera, not cam_1's: on the 29th camera 2 read `bull at (646,232) ... board radius
332.4 px` and camera 1 `bull at (648,300) ... 199.7 px, 1.51 px/mm, wedge 16 at its image
south`, and on the 8th those are camera 1 and camera 2 respectively (bull (650,302),
199 px, 1.51 px/mm, wedge 16 south). The enumeration order swapped between the two days;
the physical camera did not move. On the 29th that camera calibrated with the doubles
identity, de-biased by 0.9825, residuals -3.5..+3.0 mm, numbers read. So the hypothesis
in the issue -- that the 335 px span IS the doubles ring -- is not an inference from the
residual arithmetic alone: the same camera at the same span is on a fixture, and it is
the doubles ring there. #1359's 0.59 is 1/1.589 on this camera; whether #1359's earlier
sightings were this mechanism the log cannot say, because they printed no identity line.

Two rules, both default, neither with a rig number in it:

1. **The fit refuses a scale its own fitted ring contradicts**
   (`board_model::fitBoardToCamera`, `fittedRingToleranceMm`). The fitted rings' residual
   was printed and never judged. It is 170 x (1/(conic x deBias) - 1) by construction --
   zero when the trace is the ring the plane says it is -- and is now held to the width of
   its own ring, 170 - 162 = 8 mm: a de-biased trace further than a ring-width from the
   wire is not on that ring. The refusal names the ring, where it sits, the wire, the
   width, the conic factor and the de-bias, and cites #1748. Cameras 2 and 3 of the same
   session read +3.0..+4.1 mm on the fitted outer ring and -1.5..-4.4 on the inner;
   rig-20260929's three cameras +2.9..+3.5 and -1.9..-3.3. What it adds to the held-out
   bands is the camera that has LOST its treble pair (rig-20260922 had them refused on
   every camera before #1499; live camera 2's first look traced no ring at all): with
   only the bull and the 25 ring held out, the same shrink is in band -- rig-20260929
   camera 3's rings read 0.0284 and 0.0736 of the board at x1.589, both inside -- and
   this rule is the one that refuses it. `SCORING:` now reads the fit and APPENDS to a
   scorable camera's clause, with the fit's own reason: "its board fit is REJECTED (...),
   so the solver places nothing it saw on the board and a dart is scored from it only by
   the vote". The count and the leading words are #1451's unchanged (live it would still
   say 3 of 3): the string vote, the DEGRADED fallback, does still score from such a
   camera; only the solver refuses it (`entry_intersection::constraintFrom`). And "read
   through camera N" prints N from 1 like every other sentence.
2. **The identity is cross-checked where it is spent** (`wire_processing::conicOfDoublesFor`,
   `ellipse_processing::trebleRingInsideTheTrace`). The reach is a reading about what lies
   OUTSIDE the span; the ellipse stage has one about what lies INSIDE the trace. A doubles
   ring has a treble ring at 0.58..0.63 of it; a treble ring has nothing coloured between
   its 25 ring (0.15) and itself, and what the tracer fills into the treble slots of a
   107 mm ring sits at 58..67 mm, outside the band either way (the treble mask is the
   colour inside the doubles area, `mask_processing`). So when the ratio puts the trace in
   the treble band AND both treble edges are observed in band at x1.0 of the trace, the
   trace is the doubles ring whatever lies outside it, the plane is built at x1.0, and the
   WARN names both readings and which won. Rule 1 stays underneath it.

**Measured (`testers/run_all.sh 1748`, 718 s on the 4-core box at load 6.8).** The frame
is rig-20260929 cam_2 at frame 90 (the live camera 1). Unpainted: identity DOUBLES at a
reach of 0.954 spans, span 332.4 px, trace 328.1 px, de-bias 0.9822, residuals +0.8 /
+0.4 / -3.5 / +1.2 / -2.6* / +3.1* mm (* fitted), accepted. With a 60° arc of board red
painted at 1.30..1.45 spans: identity TREBLE at a reach of 1.450, the span unchanged at
332.4 px, the plane built at x1.0 with the WARN naming both readings, and the same
residuals, accepted. With the cross-check planted out (mutation A) the same frame builds
at x1.5888, the fitted ring sits 65.6 mm from its wire, the held-out trebles 42.2, and the
fit is REJECTED naming the 25 ring (9.7 vs 10.0..39.7), the outer treble (64.8 vs
102.9..131.7) and the fitted ring. With both rules out (mutation B, which is 0fb2ca3) it
is the same refusal without the fitted-ring sentence -- so on 0fb2ca3 the tester is red
on section 2 (built at x1.589, refused) and on section 3 (the check does not compile
without `fittedRingToleranceMm`), not on an accepted fit. The pure check rebuilds the live
camera 1 at x1.589 as -2.5 / -6.4 / -42.3 / -43.0 / -65.3* / -63.7* mm (the log's -2.4 /
-6.3 / -41.6 / -42.5 / -66.4 / -65.3) and at x1.0 as +0.1 / -0.2 / -5.9 / -1.8 / -3.4* /
+4.3*, accepted; the six honest cameras' worst fitted residual is 4.9 mm (live camera 3).
rig-20260929 camera 3 with the treble pair zeroed, at x1.589: -1.8 / -4.0 on the bull and
25 ring, both in band, -66.8* / -65.7* on the fitted pair, refused by rule 1 alone.

Bakeoff before, on 0fb2ca3's binary (capture clock): rig-20260918 19/20 and 19/20,
rig-20260922 21/23 and 23/23, the vote pin 17/20, rig-20260929 26..30/36 and 27..31/36;
pooled 135..143/158 (85.4%), r18+r22 82/86 (95.3%), 0 phantoms -- the documented
baseline. No fixture camera has a fitted residual past 4.9 mm or a trace in the treble
band with trebles inside it, so no fixture publication can move; the after run was not
reached in this slice's budget and is for the merged-tree gate.

**Two-line solves across the 3/19 wire (#1766), measured; no default moves.** The same
live log: of its 154 geometric scores 152 were "from 2 intersecting constraint(s)"
(camera 1 refused, above), and eight thrown 19s published as 3s. `testers/i1766_census.py
--live` lists every two-line publication with its UNCERTAINTY, BOARD and LONE-WIRE
lines; the eight, by SCORE line (the LONE-WIRE line is the vote's lone reading of the
SAME event, and its `camera N` is a 0-based index: `camera 1` is the log's camera 2,
physically rig-20260929's cam_1; `camera 2` is the log's camera 3):

| line | score | angle | r mm | wedge wire | sigma across it | the vote's reading |
|------|-------|-------|------|-----------|-----------------|--------------------|
| 752  | T3 (flag T3/S3, ring 4.4/5.0) | 182.4 | 104 | 12.0 mm | <13.6 (ring won) | camera 2 S3 2.2 mm inside its wire, camera 3 T3 clear: S3 |
| 822  | S3 (flag S3/S19) | 186.8 | 95 | 3.7 mm | 12.2 | camera 2 T19, 7.8 mm clear: T19 |
| 947  | T3 (flag T3/T19) | 188.2 | 101 | 1.3 mm | 13.7 | camera 2 T19, 7.9 mm clear: T19 |
| 1667 | T3 (flag T3/T19) | 186.8 | 104 | 3.9 mm | 14.8 | no LONE-WIRE line: unknown |
| 1821 | S3 (flag S3/T3, ring 0.6/5.0) | 184.0 | 109 | 9.5 mm | unprinted | unknown |
| 2332 | S3 (flag S3/S19) | 180.1 | 79 | 12.3 mm | 12.9 | unknown |
| 2367 | S3 clears, 0.9 | 180.8 | 126 | 17.9 mm | 16.5 | unknown |
| 2444 | T3 (flag T3/T19) | 185.4 | 104 | 6.6 mm | 13.0 | unknown |

So "the cameras' own readings said 19" is shown for two of the eight (822, 947), refuted
on one (752: both cameras read 3) and unprinted on five -- a LONE-WIRE line prints only
when the vote had exactly one measured reading, and the live log prints nothing else about
the vote on a geometric publish. The sixteen 19s the same solves published sit at
194.7-206.2 deg; the eight 3s at 180.1-188.2; nothing in 189-194.

**What the sigma is.** A two-line solve is a 2x2 system: its covariance is
A^-1 diag(s1^2, s2^2) A^-T with A the two unit normals, and with equal line sigmas s the
major axis is s / (sqrt(2) sin(pair/2)). Nothing about the dart is in it beyond the two
lines' own sigmas (a floored 3 deg over the lever plus 4.5 px lateral: 4.0-7.5 mm on every
fixture row). `i1766_census.py` recomputes it from each solve's I1512CAM sigmaPerps and
I1512ENTRY pair angle, and on the bakeoff's 30 two-line solves at 8406446 (six logs, 17 darts) it
reproduces the claimed major axis to 0.2% on every row: pair angles 33.5-88.6 deg,
across-wire sigmas 5.0-9.3 mm. The widest is rig-20260929 window 11, cameras 1+3 at
33.5 deg on a dart at phi 150 (13.9 x 4.1 mm) -- the same physical pair as the live log's
2+3, and the live 12.2-16.5 mm is that pair crossing at 25-37 deg at the bottom of the
board, where the 3/19 wire is. The across-wire sigma of a two-line solve is the crossing
angle of the two cameras' lines on that wedge: the rig's figure for that place on the
board, and the hypothesis in the issue is confirmed. `i1766_twoline_check.cpp` holds it
on planted boards: the same dart from the same two cameras reads 2.46x the sigma at
27 deg that it reads at 70, each within 2% of the arithmetic.

**OD_SOLVE_CONTROL=on is not the mechanism and stays opt-in.** Its refusal is "uncontrolled
AND no placed tip within 15 mm", and every one of the 30 fixture two-line solves has one
(nearest tip 1.6-9.7 mm): `refused=0` on all 30, so the switch would publish exactly what
the default does on every fixture. The live eight print no tip distance, and the same
physical cameras with accepted fits placed tips on every fixture two-line solve, so the
switch cannot be shown to have moved any of them; hypothesis (a) in the issue is
unsupported. Measured on the bakeoff, switch-first on one binary (below): identical
figures with it off and on.

**No sigma figure sends a two-line solve to the vote.** The fixtures have no two-line
solve past 9.3 mm across its wire and the live eight sit at 12.2-16.5, so any figure
between would be fitted to those eight and nothing on disk could measure it -- #1322's
rule, and #1556's own refusal of k=0.5 one issue earlier. The measurable version, every
two-line solve to the vote, is refused by its numbers: the vote differs from the geometry
on 6 of the 30 (rig-20260922 v8.2 S4 the vote reads S18, v8.3 BULL it reads OUTER, both
windows; opening v1.2 S16 it reads S8; rig-20260929 dev v9.3 S19 it reads T19) and the
geometry is right on five of the six. r18+r22 would drop by three darts.

**The figure or the sigma (crossingSigmas 1.0 stays).** The sigma is honest -- it is its
two lines' crossing, to 0.2% -- and 17.9 mm across 16.5 mm is 1.08 sigma, past #1556's
one-sigma line by eight hundredths; the check rebuilds that dart on planted boards and it
reads SOLVED under 1.0 and WIRE-UNCERTAIN under 1.25. Moving the line to catch one live
dart is the fit #1556 refused. What was wrong with the sentence is that it said "clears"
across a 16.5 mm sigma without saying what a 16.5 mm sigma IS, so the reader could not
tell it from a dart's scatter. Now `decideBoundaryCall` appends the provenance
(`entry_intersection::sigmaProvenance`) to the UNCERTAINTY sentence, flagged or clear:
"(a two-line solve of cameras 2 and 3 crossing at 27.0 deg: the sigma is that crossing's,
and no third line checks it (#1766))". A controlled solve's sentence is byte-for-byte
#1556's and a vote publish stays silent. What takes the eight out of the two-line regime
is the third camera (#1748, landed) and the axis loss on stacked darts (#1684, parked);
a two-line solve on this wedge of this rig cannot resolve the 3/19 wire better than
+-12-16 mm, and now says so.

**Bakeoff, switch-first on one binary (2026-10-10, the dev build of 8406446 -- built
before the sentence above landed, which changes no score; the sentence is held by the
pure check and has not yet been seen in a replay -- capture clock,
`testers/i1555_run.sh`).** Before (the default): rig-20260918 19/20 and 19/20,
rig-20260922 21/23 and 23/23, the vote pin 17/20, rig-20260929 26..30/36 and 27..31/36;
pooled 135..143/158 (85.4%), r18+r22 82/86 (95.3%), 0 phantoms, rc 0. After
(`OD_SOLVE_CONTROL=on`): every figure the same, and `I1681CONTROL ... refused=1` on 0
solves of the six logs -- the one refusal #1681 measured on 2026-09-29 (rig-20260929
window 22, MISS -> D5) is gone, because after #1748 that window is a two-line solve of
cameras 1+3 with two placed tips within 15 mm. So on this tree the switch is a no-op on
every fixture, both ways, and the documented baseline holds with it off and on.
`i1766_census.py` over both runs reads the same 30 two-line rows: pair 33.5-88.6 deg,
across-wire sigma 5.0-9.3 mm, formula residual 0.2%, refused 0, vote differs on 6.

**A vote reading within its sigma of a ring wire (#1773), default.** Live on 2026-10-10
(build 2b56b48, 56 darts, `OD_SPIKE_THRESHOLD=0.006 OD_LONE_CAMERA=on`) three vote
publishes went out unflagged with a ring wire inside one sigma of the reading, and two of
them were the session's only unflagged wrong darts besides #1707's:

| time | thrown | published | the vote | BOARD line | ring wire | margin |
|---|---|---|---|---|---|---|
| 15:51:49 | S19 | T19 at 0.7 | lone, camera 1 | `ring=triple segment=19 radius=0.600382` | treble inner, 99 mm | 3.1 mm inside (102.1 mm from the bull) |
| 16:06:25 | S1 | OUTER at 0.9 | lone, camera 0 | `ring=outer radius=0.050671` | the bull's, 6.35 mm | 2.3 mm outside (8.6 mm) |
| 16:47:22 | BULL | OUTER at 0.9 | 2 cameras agreeing | `ring=outer radius=0.040479` | the bull's, 6.35 mm | 0.5 mm outside (6.9 mm) |

The first printed `LONE-WIRE: camera 1's T19 is 14.4 mm from a wedge wire, clear of the
5 mm sigma` -- true, and about the wrong wire: #1628's `wedgeWireMarginMm` measures the
wedge wires at 9 + 18k degrees and nothing else. The second printed no LONE-WIRE line
at all, because a ring-only reading has no wedge and the function returns -1 before the
check says anything. The third printed none because `agreeing == 2`, and nothing on the
vote path measured a ring wire for any reading: the only ring-wire awareness there was
#1707's rim fallback, the DOUBLE only and only under `OD_RIM_FALLBACK=on`.

Two rules, both default, both pure in `score_processing.hpp`:

1. **`ringWireMarginMm`** measures the reading's distance to the nearer of its own
   ring's two wires, by its own ruler: radius x 170 mm against the spec's 6.35 / 15.9 /
   99 / 107 / 162 / 170 (static_asserted against `DartboardSpec` beside
   `kScoringRadiusMm`), and names the ring across that wire. The wires are the published
   RING's edges, not the two nearest the radius: the ring is the ellipses' call and the
   radius the ruler's, and when they disagree (a treble by the ellipses at a ruler
   radius of 95 mm) the margin is to the edge the ruler has crossed and the alternative
   is the ring on its far side. The LONE-WIRE sentence now names both margins and the
   nearer: `camera 1's T19 is 14.4 mm from a wedge wire and 3.1 mm from the treble's
   inner wire (the ring wire is nearer), clear of the 5 mm sigma` -- its tail is still
   the wedge check's, and `I1628LONE` is byte-for-byte what it was.
2. **`checkVoteReadingAgainstRingWires`** applies #1628's 5 mm sigma (the floor on what
   one camera's ruler can say; reused, not fitted) to every vote reading that measured a
   radius, lone or consensus -- two cameras agreeing on the string "OUTER" is not a
   second measurement of the radius, the published radius is one camera's -- and not to
   a wedge-by-default reading, which already publishes at 0.5 as "nobody measured the
   wedge". Inside the sigma the ring across the wire is offered in #1556's shape
   (`alternative`, `boundary_kind` "ring", the confidence demoted to 0.7) and the score
   string does not move: `RING-WIRE: camera 1's T19 (alone) sits 3.1 mm from the
   treble's inner wire by its own ruler, 102.1 mm from the bull, inside the 5 mm sigma,
   so T19 publishes flagged with S19 across that wire at 0.7 (#1773)`. Clear, it says
   so (`clears ... 12.0 mm from the treble's inner wire ... clear of the 5 mm sigma`),
   on every vote publish (#1556 rule 3). `boundary` and `uncertainty` stay null on a
   vote publish, as docs/api.md says; the margin is in the log and in `I1773RING`.

Three decisions a reader may want to reverse:

- **A ring-only reading near the 25 ring's wire publishes unflagged, and says why.** An
  OUTER 0.9 mm inside the 25 ring's wire has a single across it whose segment nobody
  measured: `scorePoint` fills a bull's angle from slot 0 when the camera is not
  anchored -- an asserted 20 that `wedge_asserted` does not mark, because #1346 marks
  only asserted SCORES -- so naming "S20" from it would be naming the default. #1556
  rule 2: a flag that cannot name its second candidate is not a flag. Live case (2)
  flags because the bull's wire is the nearer (2.3 against 7.3 mm), not the 25's.
- **When both a wedge wire and a ring wire are inside the sigma, the ring across is
  offered and the sentence says the wedge wire is there, and nearer if it is.** A dart
  carries one `alternative`; this issue adds the ring one. The wedge neighbour is what
  #1628 measured acting on and refused, and the same question on the geometric path is
  #1782's. The pure check holds a T19 0.9 mm past the 3/19 wire and 3.1 mm inside the
  treble wire: S19 offered, `its nearest wedge wire is inside the sigma too, at 0.9 mm
  and nearer, said by LONE-WIRE and not offered (#1628)`.
- **The rim fallback's own flag stands where it is on and fired.** It is the same ring
  flag with its own sentence; by default it is off and this rule covers the double.

**Measured (`testers/run_all.sh 1773-ringwire`, 23 s).** The check rebuilds the three live
darts from their BOARD lines: 102.06 / 3.06 / 14.42 mm (the log's 14.4), 8.61 / 2.26 mm,
6.88 / 0.53 mm, flagged T19/S19 at 0.7, OUTER/BULL at 0.7, OUTER/BULL demoted 0.9 to
0.7. Mutation A (a scratch copy of the header reading every radius 50 mm further out)
fails exactly the 23 `ring:` and 2 `both:` assertions the check counts, the three live
shapes by name, and not one `pure:`; mutation B (the guard read as `agreeing != 1`)
fails exactly the 2 `both:` assertions, live case (3). The first tree run was red on
four assertions: a single at 95.2 mm was measured against the double's inner wire,
because the single's two bands were split at the 25-ring/treble midpoint; the split is
the treble's middle, 103 mm.

**The fixture count (`testers/i1773_census.py` over the bakeoff's seven logs, 2026-10-10).**
Of 154 matched darts the vote published 53; 48 of those measured a radius (the five
without one are the no-winner MISSes). **20 are inside the 5 mm sigma of a ring wire,
and the rule flags all 20** (none unnameable: no fixture OUTER sits near the 25 ring's
wire). 19 of the 20 are right today and keep their score with the ring across as the
alternative at 0.7; the one wrong is the vote pin's v1.1 (rig-20260918, `OD_SCORE_PATH=vote`),
S13 for a thrown T13 at 96.5 mm, 2.5 mm inside the treble's inner wire, whose alternative
T13 IS the throw. On the six default runs every near reading is right (15 of 30 checked,
half). `score_moved=0` on all seven runs: the published score equals the vote's string on
every vote dart. Five vote darts are wrong and clear of every ring wire -- all five are
wedge errors (the pin's v2.1 and v4.1, rig-20260922 dev v7.2 at 0.8 mm from the 3/19
wire, rig-20260929 v10.3 in both windows at 1.8 mm from a wedge wire), #1628's ground
and not this issue's. The twenty, with margins (per window; both windows read alike):
rig-20260918 v6.2 S7 at 4.6-4.8 mm inside the double's inner wire (alt D7);
rig-20260922 v3.2 D20 at 3.5 mm from the outer wire (alt MISS) and v4.1 S7 at 4.9 mm
from the double's inner wire (alt D7, its wedge wire 2.0-2.5 mm and nearer); rig-20260929
v2.2 T14 at 2.8 mm from the treble's inner wire (alt S14, wedge wire 1.1-1.3 mm and
nearer), v2.3 T11 at 3.9 mm from its outer wire (alt S11), v6.2 D5 at 1.1 mm from the
outer wire (alt MISS), v7.3 S12 from two cameras agreeing at 4.7 mm from the double's inner
wire (alt D12, 0.9 -> 0.7) and opening v9.3 T19 at 4.0 mm from the treble's outer wire
(alt S19); the pin adds v1.1 above, v2.2 T14 at 0.5 mm, v3.3 T20 from two cameras at
2.9 mm (wedge wire 0.9 mm and nearer) and v5.1 S15 at 1.0 mm. The two darts #1707's
opt-in flagged (rig-20260922 v3.2's D20, rig-20260929 v6.2's D5, each with MISS) are
now flagged by default by this rule, and the rest of #1707 is unchanged.

**Bakeoff on the new binary (capture clock, `testers/i1555_run.sh`, 2026-10-10).**
rig-20260918 19/20 and 19/20, rig-20260922 21/23 and 23/23, the vote pin 17/20,
rig-20260929 26..30/36 and 27..31/36; pooled 135..143/158 (85.4%), r18+r22 82/86
(95.3%), 0 phantoms -- every figure the documented baseline's. `testers/run_all.sh
1628-lonefix` passes in 400 s on the same binary (dev v7.2: near=5, reselected 0 by
default and 1 under the pin, as recorded), and its LONE-WIRE line for v7.2 now reads
`camera 0's S19 is 0.8 mm from a wedge wire and 5.7 mm from the 25 ring's wire (the
wedge wire is nearer), inside the sigma; ...the reselection is off..., so it stands`,
with `RING-WIRE: camera 0's S19 (alone) sits 5.7 mm from the 25 ring's wire by its own
ruler, 21.6 mm from the bull, clear of the 5 mm sigma` beside it.

## A corner offers the corner (turnaus#1782), default

**The live dart.** 2026-10-10 16:38:53, build 2b56b48 (`OD_SPIKE_THRESHOLD=0.006
OD_LONE_CAMERA=on`): a thrown **S19** published **T3**, flagged `T3 or S3`. A two-line
solve crossing at 48.3 deg, 5.3 mm sigma, at 188.03 deg and radius 0.6346: on the fitted
model 0.6 mm inside the treble's outer wire, and 0.6346 x 170 x sin(0.97 deg) = **1.8 mm**
on the 3 side of the 3/19 wire (189 deg). Both wires inside one sigma: the four cells are
T3, S3 (across the ring wire), T19 (across the wedge wire) and S19 (across both).
`decideBoundaryCall` measured the crossing to the NEAREST wire in sigmas and named the
one cell across it; Turnaus received `"alternative":"S3","candidates":["S3","S19","T19"]`,
the S19 there because a camera read it (#1721's runner-up), not because the flag offered it.

**The rule** (`score_processing::decideCornerCall`, pure, after `decideBoundaryCall`):
where the solve is within #1556's threshold (`Params::crossingSigmas`, 1.0) of a ring wire
AND a wedge wire and every cell can be named (the solver re-scores each side and the
diagonal through the same fit, `alternativeAcross` and `diagonalAcross`):

1. the three other cells are ranked by the solve's own covariance -- a bivariate normal
   over the offsets toward the two wires, with the floored across-wire sigmas
   (`sigmaRadialMm`, `sigmaTangentMm`) and the unfloored correlation between them; two
   cells naming one score (a single beside the 25 ring: the diagonal is OUTER again) are
   merged and their probabilities summed;
2. where a camera the solve did NOT use read one of those cells clear of every wire (its
   #1628 wedge margin and #1773 ring margin both at least 5 mm), that cell goes first.
   A reading inside its own sigma is another coin (#1782's 16:49 counter-case: 1 for 2);
3. the first is the flag's `alternative`; the rest are `BoundaryCall::others`, which
   `dart_candidates` ranks straight after it -- a corner cell some voting camera read
   first, then the rest in the flag's order, then the other readings and neighbours.

The published score never moves and the flagged set is #1556's (a corner is inside the
nearest wire's sigma by construction). The sentence keeps #1556's opening:
`UNCERTAINTY: T3 or S3, T19, S19 -- a corner, a ring wire 0.6 mm across a 5.3 mm one-sigma
and a wedge wire 1.8 mm across a 5.3 mm one-sigma, so T3 publishes now as the most
probable cell (34%) and is flagged; by the solve's covariance S3 29%, T19 20%, S19 17%; a
tap affirms it or appends another`. `OD_CORNER_FLAG=nearest` restores the one alternative
on the same binary and appends what the corner would have offered. Census line:
`I1782CORNER` (under `OD_GEO_SCORE=on`), counted by `testers/i1782_census.py`.

**What the live dart now offers depends on a camera number the issue text cannot settle.**
`LONE-WIRE: camera 1's S19 is 5.5 mm from a wedge wire` numbers cameras from 0 (it prints
`choice.camera`), while the solve's cameras "2 and 3" are 1-based since #1748. Read that way
the camera that read S19 is the solve's camera 2 -- USED -- and rule 2 does not fire: the
flag offers **S3, T19, S19** (alternative S3, as before; S19 now in the flag, and
candidates S3, S19, T19 as before because a camera read S19). If instead the S19 camera was
the one the solve left out, rule 2 fires and the flag offers **S19, S3, T19** with S19 as
Turnaus's `alternative`. The live log is on the maintainer's box; its `I1512ENTRY` story
(or, from #1787 on, `cameras_used`) says which.

**Measured (`testers/run_all.sh 1782-corner`, 50 s at load ~15).** The pure check holds the
live shape at every wedge sigma 5.0-9.3 mm (#1766's fixture two-line range) and rho
-0.6..0.6: S19 is offered in all 25; by covariance alone at rho 0 the order is S3, T19,
S19 (T3 34%, 29/20/17%); an unused camera's clear S19 goes first; a used camera's, one
2.9 mm from its wedge wire or 1.2 mm from its ring wire, does not; the same T3 8 mm from the
wedge wire is not a corner and its flag is #1556's byte for byte. Predictions stated first
and met exactly: doubling the wedge half of the threshold fails the 3 `reach:` assertions
only; disabling the promotion fails the 3 `clear:` only; dropping the diagonal fails the 6
`diag:` and the 3 `clear:` (the promoted S19 was the diagonal) and no `reach:` or `pure:`.

**The fixture count (`i1782_census.py` over the bakeoff, capture clock, `OD_WINDOW_UNIT=cycles`,
2026-10-11).** Every ACCURACY figure is the documented baseline's: rig-20260918 19/20 and
19/20, rig-20260922 21/23 and 23/23, the vote pin 17/20, rig-20260929 26..30/36 and
27..31/36; pooled 135..143/158, r18+r22 82/86; `score_moved=0` on all seven runs. Of 135
matched darts on the six default runs, **71 are flagged geometric publishes, 10 of them
corners** -- five darts, each a corner in both windows:

| dart | published | thrown | #1556 offered | the corner offers |
|---|---|---|---|---|
| r18 v1.1 | T13 | T13 | T6 | T6, S13, S6 |
| r22 v2.3 | T8 | T8 | S8 (dev) / T11 (opening) | S8, T11, S11 / T11, S8, S11 |
| r22 v5.1 | S4 | S4 | S13 | OUTER, S13 (OUTER is ring + diagonal merged) |
| r22 v8.1 | T1 | T1 | S1 | S1, T18, S18 |
| r29 v3.1 | S6 | S13 | S13 | S13, T6, T13 |

8 of the 10 publish right; the 2 wrong (r29 v3.1) have the truth across the wire #1556
named, and the corner keeps it first. **None has the truth across the wire the flag did
not name**, so on the fixtures the corner moves no right answer out of first place and
gains none; the first alternative changes on one dart (r22 v5.1, S13 -> OUTER, published
right). **A camera the solve did not use read nothing on any corner**: in all 10 every
voting camera's line was in the solve (r29 v3.1's two cameras both read S13 clear of every
wire, and were used). Over all 71 flagged darts the unused cameras read 8 times, 2 of them
clear (r18 v2.3's S5, the published score and the truth), so rule 2 is unexercised by the
fixtures and stands on the live argument alone. The live corner is the rig's 3/19 wire
under a two-line solve, which no fixture reproduces.
Run twice: on 78faa2f (the cells named only inside both sigmas) and on 7be8f4a (named
wherever both wires can flip the call, the threshold in the pure rule; 1238 s and 1665 s
wall at load 8-22). Every ACCURACY figure, every `I1721CANDIDATES` line and every
`I1782CORNER` decision field (flagged, corner, nearest, offered, cameras) is identical
between the two; only the measurement fields a non-corner now carries differ.

## Body-sized windows: the takeout's arm (turnaus#1781), opt-in

**Live on 2b56b48** (`OD_SPIKE_THRESHOLD=0.006 OD_LONE_CAMERA=on`, 2026-10-10 16:24) the
thrower walked up for a takeout and the board published two darts nobody threw: a D11 at
16:24:28.362 (cameras 2 and 3 `not straight`, camera 1's lone tip), the END at 16:24:29.105
(`CLEAN BY REVERSION` on all three, camera 2 falling **69,193** -> 36,506 px), and an S2 at
16:24:30.081 (cameras 1 and 3 `not straight`, camera 2's lone tip). What let each through:
the state vote counts any fresh figure over the 0.10% floor as an arrival and nothing bounds
it from above, so the side-on cameras' arm met the quorum; #1707's rim count passed it
because the arm is in the scoring area; #1518's reversion reads only a fall and the arm's
arrival is a rise; and the scorer, with both side-on axes refused, published the end-on
camera's lone tip (`No consensus, using single camera score`). The figure that says
"body" was in the D11 window (camera 2's cumulative 69,193 px), but INFO prints it only
one window later, as the END's "fell from". The S2 window opened against a clean
reference adopted while the arm was still in frame (#1691's shape: all three cameras voted
CLEAN, so `OD_CLEAN_ADOPT=agreed` would not have kept any reference), and its own figures
are not in the log.

`OD_BODY_WINDOW` holds such an advance (`dart_processing.cpp`; pure predicates in
`dart_processing.hpp`):
- `size`: a camera that voted the arrival brought a fresh figure of at least **15% of its
  board** (`bodySizedFreshSharePercent`);
- `after`: a first dart's vote comes within **1,500 ms** of a takeout END that had a
  reversion vote in it (`arrivalAfterTakeoutHorizonMs`);
- `on`: both; `off` (default): neither. `OD_BODY_CENSUS=1` prints an `I1781BODY` line
  for every window any camera voted up, with each up-voter's fresh share and the time
  since the last reversion END, whatever the switch.

**Where 15% comes from.** The census over the seven bakeoff replays (capture clock,
`OD_WINDOW_UNIT=cycles`, the live switches): the largest fresh figure any up-voter brought
to a called dart is 9.96% of a board (rig-20260922 opening, window 1, camera 1, 21,508 of
215,925 px: v1.2's S16 read against the calibration picture holding the parked dart); the
next is 7.67% (rig-20260922 dev, camera 2), and every rig-20260918 and rig-20260929 dart is
under 6.7%. The live arm was 69,193 px cumulative on camera 2, 32.0% of the same rig's
camera-2 board on rig-20260929 (215,989 px); less two of that camera's largest fixture
darts (12,692 px each) for the darts already on the board, its fresh figure was at least
20.3%. 15% is 1.5x the largest fixture dart. **Where 1,500 ms comes from.** The S2's vote
came ~976 ms after the END's; the soonest fixture first dart after a reversion END is
4,433 ms (rig-20260922 opening; no other fixture window reconciles a takeout by reversion).

**The S2 question: hold a first dart within a second of the END?** On the fixtures it costs
nothing (the clause never fires: 7 first darts follow a reversion END, 4.4..97.7 s after it, all on
rig-20260922 opening). It is a refusal, not a deferral: after a CLEAN every working
background is empty, so a real dart refused there is still in the next window's fresh
figure and publishes with the next motion -- but a lone real dart with no later motion
would wait for the next dart and be read in one figure with it. No fixture dart is that
quick, and a thrower who has just pulled three darts is not back at the line in 1.5 s.

**Bakeoff (capture clock, `OD_WINDOW_UNIT=cycles`, `OD_SPIKE_THRESHOLD=0.006
OD_LONE_CAMERA=on OD_BODY_CENSUS=1`, 2026-10-11).** Off is commit 6a2e35e's binary, on is
0970113's; they differ only in the body constant (10% -> 15%), which only the switch reads,
and no fixture window reaches 10% either.

| | `OD_BODY_WINDOW` off | `OD_BODY_WINDOW=on` |
|---|---|---|
| rig-20260918 (2 windows) | 38/40, phantoms 2 (MISS, 0 scoring) | 38/40, phantoms 2 (MISS, 0 scoring) |
| rig-20260922 (2 windows) | 44/46, 0 phantoms | 44/46, 0 phantoms |
| rig-20260929 (2 windows) | 61..67/72 | 61..67/72 |
| pooled | 143..149/158 | 143..149/158 |
| r18+r22 | 82/86 | 82/86 |

No window is held on any run (no `I1781 BODY WINDOW HELD` line; the switch's startup line is
in all seven), and every `I1555 PAIR` and phantom row of all seven censuses is identical:
**no changed row**. rig-20260918's two phantoms are its v6 MISS window, present with the
switch off; they are the live switches', not this rule's.

`testers/run_all.sh 1781-body` (33 s) holds both predicates at their boundaries, on the
live figures and on the fixtures' nearest darts, and mutates each constant both ways:
30% and 3%, 900 ms and 5,000 ms each turn exactly one named assertion red, as predicted.
It stays opt-in: it rests on one live takeout whose log is the only evidence, and no
fixture holds an arm-sized window.

## A dart past three (turnaus#1793), opt-in

**What the code did** (read, then confirmed on the replays). The board counts arrivals itself,
CLEAN -> DART_1 -> DART_2 -> DART_3. A camera at DART_3 whose fresh figure cleared the floor
gave the candidate DART_3 (`Stay in DART_3`, `dart_processing.cpp`), which equals the board's
state, so the vote counted it as a stay; and `score_processing` publishes only on a state
change. So a board that had counted a dart nobody threw had no state for the visit's real
third dart: when the player removed the phantom in Turnaus ("There was no dart there",
turnaus#1723), the round had room again and nothing was pushed for the dart that filled it.
The client already treated `DROPPED` as ordinary: a `202`, settled, not retried, counted as
delivered, and logged once per push.

`OD_PAST_THREE=on` (`dart_processing.cpp`; pure decisions in `dart_processing.hpp`):
- a camera at DART_3 whose fresh figure clears the floor, or the rim floor (#1689), votes an
  arrival past three (`votesArrivalPastThree`), and the vote counts it as an up vote
  (`votesUp`) under the same quorum, the lone-camera rule (#1678) and the body hold (#1781);
- a quorum calls a dart and the board stays DART_3 (`reconcileVote`); everything that
  follows a called dart reads `windowCalledADart` instead of `final > previous`: the working
  backgrounds move (#1495), the tips are recorded (#1535), the reversion memory is cleared
  (#1552), the frames are kept (#1787), and a held past-three vote re-bases under
  `OD_HELD_REBASE=on` (#1690) exactly as a held vote does at DART_1/2;
- `score_processing` publishes it (`windowPublishes`) through the DART_3 case, and the line
  `I1793 PAST THREE` says so;
- the client says a `DROPPED` answer once per round at INFO and the rest at DEBUG
  (`push_answer.hpp`); a delivered takeout ends the round. Off: every answer is said, as before.

Turnaus decides whether the round has room: it counts the dart when a withdrawal left room
and answers `DROPPED` to a fourth dart into a full round (turnaus#1281). The takeout is
unchanged: CLEAN wins at quorum before any up vote is counted, from DART_3 as before.

**Bakeoff (capture clock, `OD_WINDOW_UNIT=cycles`, `OD_SPIKE_THRESHOLD=0.006
OD_LONE_CAMERA=on`, 2026-10-11, one binary, switch off then on).**

| | off | `OD_PAST_THREE=on` |
|---|---|---|
| rig-20260918 (2 windows) | 38/40, phantoms 2 (MISS, 0 scoring) | 38/40, phantoms 2 (MISS, 0 scoring) |
| rig-20260922 (2 windows) | 44/46, 0 phantoms | 44/46, 0 phantoms |
| rig-20260929 (2 windows) | 61..67/72 | 61..67/72 |
| pooled | 143..149/158 | 143..149/158 |
| r18+r22 | 82/86 | 82/86 |

**No changed row** in any of the seven censuses, and **no arrival past three pushed** on any
fixture: no `I1793 PAST THREE` line in any run (the switch's startup line is in all seven).
The detector logs are identical line for line once the timings and the capture anchors are
taken out, so no window was even held on a past-three vote (a held one would print a
`STATE VOTE` line with its up count). No fixture holds a fourth arrival: every window opened
on a board at DART_3 is the takeout. **No written turn changes** on any fixture.
rig-20260918's v6 is the phantom's shape (a MISS window where v6.1's unseen miss was) but not
#1793's: the round holds three windows and nothing arrives after them.

What is not measured: a past-three vote cast by a camera in a window the vote reconciled
CLEAN prints nothing, so how often the takeout's own motion votes past three on one camera
before CLEAN wins is not counted. `testers/run_all.sh 1793-pastthree` holds the decisions on
a replayed round (a phantom the player removes, then three real darts: S1 pushed and written
with the switch on, not pushed with it off; a fourth arrival into a full round pushed and
DROPPED with the written turn unchanged; a lone camera held; the takeout after it an END)
and mutates each decision with its prediction stated first. It stays opt-in: the fixtures
cannot show the case it is for, and only a live session can.

## A takeout of surround darts (turnaus#1783), opt-in

**Live on 2b56b48** (`OD_SPIKE_THRESHOLD=0.006 OD_LONE_CAMERA=on`, 2026-10-10 16:51) a visit of
three misses on the surround published three MISSes (`I1707 RIM CARRIED`, cameras `rim only`),
and the takeout that pulled them opened nothing: the log is silent from the post-dart window at
16:51:56 (`stays DART_3`, 9,343 / 32,153 / 5,894 px) to a second takeout's `CLEAN BY REVERSION`
END at 16:52:12, two further off-board darts later. The live log is not on this machine, so what
follows is read from the code and the figures the issue records.

**What the code says.** An event opens on one camera's frame-to-frame motion figure crossing the
entry (`processMotion`, IDLE and the cooldown's arm), and that figure is counted inside the
double's outer ellipse (`BoardExtent::edge`), while since #1689 the dart counts reach the rim.
The board was not stuck in an event: an event in `SPIKE_DETECTED` past 10 s logs a warning, a
settle that cannot finish is broken only by a quiet cycle (and the default, `OD_MOTION_FIX`
unset, finishes on the first one), and the 16 s include two throws from the oche, when nothing
is between a camera and the board. So the board was IDLE and **no camera's figure inside the
double crossed 0.006 while the darts were pulled**: a hand gripping darts between the double and
the rim is outside every figure that opens an event. 32,153 px on camera 2 needs no thrower: it
is three side-on darts at the size of that camera's largest fixture darts (12,692 px, #1781).

**The fixtures show the same thing.** `OD_MOTION_REGION_CENSUS=1` prints `I1783REGION`, both
counts, on every IDLE or cooldown cycle where either crosses the entry. rig-20260929's visit 12
(`miss d14 miss`) has its two misses as figures counted to the rim and not to the double: cycle
5039 (camera 1 0.0111 to the rim, 0.0001 to the double) and 5154 (camera 1 0.0335 against
0.0001). Neither opened an event, so those misses never published (#1537's shape, and why that
visit is AMBIGUOUS in the census). That visit's takeout opened because the hand reached the D14
inside the double (cycle 5233, 0.0631), one cycle after the hand first crossed the entry in the
surround (5232: 0.0304 to the rim, 0.0000 to the double).

`OD_MOTION_REGION=rim` (`motion_processing.cpp`; the masks and the share are pure in
`motion_processing.hpp`, `motionMasks` and `boardShare`) counts the motion figure out to the
rim, the ellipse `Region::tip_mask` is (the double's x 225.5/170), and keeps it a share of the
**double's** area, so the entry, the settle and every other ratio keep their units (#1689's
choice for the dart counts). Unset or anything else: counted inside the double, as before. The
board level the exposure hold reads (#1646) stays on the double. `I1783 OD_MOTION_REGION=rim`
says the switch at start-up.

**Bakeoff (capture clock, `OD_WINDOW_UNIT=cycles`, `OD_SPIKE_THRESHOLD=0.006
OD_LONE_CAMERA=on OD_MOTION_REGION_CENSUS=1 OD_WINDOW_CENSUS=1`, 2026-10-11, one binary,
switch off then on).**

| | off | `OD_MOTION_REGION=rim` |
|---|---|---|
| rig-20260918 (2 windows) | 38/40, phantoms 2 (MISS, 0 scoring) | 38/40, phantoms 2 (MISS, 0 scoring) |
| rig-20260922 (2 windows) | 44/46, 0 phantoms | 44/46, 0 phantoms |
| rig-20260929 (2 windows) | 61..67/72 | **67/72** |
| pooled | 143..149/158 | **149/158** |
| r18+r22 | 82/86 | 82/86 |

- **Every END closes on the same cycle** in all seven replays (`WINDOW CENSUS` closed cycles,
  6 / 6 / 7 / 8 / 6 / 12 / 12 ENDs), and every rig-20260918 and rig-20260922 census row is
  identical. No window is added there.
- **rig-20260929 gains two windows per run**, visit 12's misses: `CLEAN -> DART_1` (camera 1
  1,961 px, 3 up) and `DART_2 -> DART_3` (3 up), both published MISS, and the visit reads
  `MISS D14 MISS` exactly in both windows where it published D14 alone. Its END now closes
  from DART_3, on the same cycle.
- **No new phantom**, and no visit publishes more than was thrown.
- Of the rim-only crossings the census prints with the switch off (7 / 7 / 8 / 9 / 7 / 30 / 31
  per run), all but one per rig-20260918 run and three per rig-20260929 run are the cycle
  before a crossing of the double: the same event, one cycle earlier.

**Beside the other takeout switches.** `OD_PAST_THREE` (#1793) acts on a window at DART_3 that
does not reconcile CLEAN; neither run has one (every `DART_3 ->` window is an END), so it has
nothing to act on here. Live, it is the switch to watch with this one: a takeout of surround
darts now opens windows, and one read while the hand is still in the surround, counted to the
rim, could vote an arrival past three; Turnaus answers `DROPPED` into a full round. `OD_BODY_WINDOW`
(#1781) does not contradict it: this switch opens windows and that one refuses arrivals in them,
and the END this one restores is what its `after` clause counts from. The two new windows bring
fresh figures under 1% of a board, 5 s after the END.

`testers/run_all.sh 1783-region` (45 s) holds the masks on rig-20260929's camera-2 ellipse with
a patch in the surround, one past the rim and one on the board, and mutates the rim scale both
ways, the default's mask and the denominator, each predicted to turn exactly one named
assertion red. It stays opt-in: the fixtures hold no takeout of surround darts only, and the
live visit's own motion figures are not in its log.

## Kept frames, the account per dart and the log upload (turnaus#1787)

**What a kept dart is.** The window that calls a dart averages each camera's frames while
the board settles (`dart_processing.cpp`, `window_frames`): one grey 8-bit picture per
camera at the camera's own size, the pictures the vote and the entry solve were read from.
Under `OD_KEEP_FRAMES=on` the vote that advances the board deposits them
(`src/detector/geometry/detection/frame_keep.hpp`), and the Turnaus client commits the
deposit under the dart's `reference` when it mints one -- only if the window ordinal the
published result names is the deposit's, so a window whose dart never published is never
served under the next dart's name. The `I1512`/`I1681`/`I1773` census lines printed about
that window ride beside the pictures, taken off the logger as they are written
(`logging::lineTap`, one pointer read per log line on every board that keeps no frames).

**What it costs, measured.** A kept dart is held RAW; it is PNG-encoded only when Turnaus
asks for it, on the client's push thread. At the rig's 1280x720 and three cameras:

| figure | bytes | how |
|---|---|---|
| one camera's settled frame, grey | 921,600 | 1280 x 720 x 1 |
| one kept dart, raw (what the buffer holds) | 2,764,800 | 3 x 921,600; `i1787_frames_check.cpp` reads it off the buffer |
| one kept dart as three PNGs (what leaves) | 1,158,466 (42% of raw) | MEASURED 2026-10-10: first frame of `mocks/rig-20260918/cam_1.mp4`, grey, and two transforms of it, 385,984 + 386,256 + 386,226; the check prints `MEASURED png_bytes_per_dart` on every run |
| the ring of ten (the default N) | 27,648,000 (26.4 MB) | ten darts; the eleventh pushes the first out |

So the default buffer is 26.4 MB on a Pi 4 with 4 GB and the same on the maintainer's
Windows box; `OD_KEEP_FRAMES=25` would be 66 MB, still under the 100 MB the issue asked
the buffer to stay under, and the pin is off unless set, at which point `deposit()` reads
one static bool and returns. The scoring thread pays one `clone()` per camera per published
dart and never an encode. The hypothesis in the brief -- about 8 MB raw per dart -- assumed
colour frames; the settled frames are grey, so it is a third of that. **Not yet measured on
the Pi itself**: the figures above are the buffer's own arithmetic, read off it in the
check on the x86 container; the Pi holds the same pictures at the same size.

**The account per dart.** `TurnausClient::detectionBody` now posts, beside #1366's bytes,
the numbers the log already prints about the dart (docs/api.md, "The rest of the board's
account"): `path`, `flagged`, `confidence`, `degraded`, `cameras_used` (numbered from 1 as
the log numbers cameras), a two-line solve's `crossing_deg` (#1766), `sigma_mm`,
`margin_mm` and `wire_kind` on a geometric dart, `agreeing` and `lone_wire_mm` on a vote
dart. They are filled in `score_processing.cpp` beside the sentences that print them and
carried on `ScoreResult` and `DetectorResult`; `ring_wire_mm` is filled beside #1773's
RING-WIRE sentence (`ring_wire.ring.marginMm`) on every vote publish that measured a
radius, -1 (absent from the body) where that sentence did not print. A result whose `path`
is empty (every hand-built dart in every tester) posts exactly #1366's bytes, which is how
`i1366_position_check.cpp`'s literal comparison still holds.

**The log upload** reads the file `--log-file` appends to, posts the lines since its last
acknowledged offset at every END on the push thread once the takeout is delivered, and the
rest at shutdown; the server (turnaus#1786) is the judge of the offset (a `409` names the
length it holds and the board moves there), so a lost answer or a restart never stores the
same bytes twice. `LogUploadLedger` in `turnaus_client.hpp` is the arithmetic;
`i1787_upload_check.cpp` walks a model of the route through a lost answer, a restart and a
server that lost its copy, and shows the count model it replaces losing the log on the
last of those.

## The scheduled restart at 06:00 (turnaus#1797)

**Why a working board stops.** ADR-0077 §7 lets the launcher look for an update only
before a start that follows a clean stop at least fifteen minutes old
(`src/launcher/update_moment.hpp`, `kQuietSeconds`). A board that is online around the
clock never starts, so a release on its channel reaches it only when somebody restarts it
by hand. The maintainer's decision of 2026-10-10 -- "no one is playing at 6am" -- is that
a board still running at 06:00 local stops itself then, and the restart is what lets the
launcher look. No push from Turnaus (the beat answer stays the interval and the silence),
no idle heuristics beyond the one guard below.

**The rule** (`src/utils/scheduled_stop.hpp`, pure, driven with a fake clock by
`testers/i1797_schedule_check.cpp`). Local time by the machine's zone, through the same
`std::localtime` the log's timestamps use. A detector started before 06:00 is due to stop
at 06:00:00 that day; one started at or after 06:00 waits for the next day's 06:00, so the
board the launcher starts again at 06:00:xx does not stop twice. The log says when the stop
is due as the loop starts (`I1797 SCHEDULED RESTART armed: due at 2026-10-12 06:00 local`).
**The one guard**, said in the log each time it fires: a dart published in the last ten
minutes, or a round the board has not yet seen taken out (a `SCORE: <x>` with no
`SCORE: END` after it), postpones the stop to the next ten-minute mark --
`I1797 SCHEDULED RESTART postponed to 06:10: a dart was published 240 s ago, inside the
10-minute guard` -- and again to 06:20 if the board is still busy then. The two facts are
written in `Scorer::sendResult` beside the `SCORE:` line itself. When the board is quiet
the log says `I1797 SCHEDULED RESTART at 06:00: the launcher looks for an update and starts
the board again` and the loop leaves by the flag SIGINT and SIGTERM set (#825), so the
shutdown is the ordinary one -- the announcement withdrawn, the score socket joined,
#1787's final log post and `TURNAUS: client stopped` -- and the only difference is the
exit status.

**Exit code 60** (`scheduled_stop::kExitCode`, `Scorer::kScheduledStop`, the launcher's
`kScheduledStop`: one constant, one header, both programs). Above 0 and below 128; away
from 75 (`kCouldNotSee`) and 78 which the detector already returns, from the sysexits
range 64-78 generally, from the launcher's own 40-42 so a Task Scheduler reader is never
looking at the child's number, and from 124-127 which `timeout` and the shell use. The
launcher writes it into `update\state.txt` as `last_ending=scheduled`; `endedBadly()`
does not name it, it counts against nothing, and `decideMoment()` answers
`Moment::Scheduled` -- MAY check -- at any gap, two seconds included, with its own
sentence in both languages. A `cleanly` stop two seconds old is still `QuickRestart`.

**The launcher starts it again.** `carry()` (`src/launcher/launcher.hpp`) goes round once
more after a `scheduled` ending and only after that one: it says the ending in the window,
reads the state, looks (installing or rolling back exactly as #1306), and starts the
detector with the same arguments. A detector that comes back `scheduled` again inside a
minute of the start that followed a scheduled stop is a bug in the rule, not a day: the
launcher refuses to follow it, says so, and exits 43 (`kScheduledNotFollowed`).
`testers/i1797_carry_check.cpp` measures this against a real child (`i1797_stub`, exit 60
once and 0 the second time): two starts, identical `argv`, one manifest request between
them while the state file says `scheduled`, the sentence said; a clean ending from the
same state is one start and no look, byte for byte #1303's. Under systemd the unit's
`Restart=always` would do the carrying (#1796, the Pi; not this issue).

**The pin.** `OD_SCHEDULED_RESTART=off` is the only way to keep a board from stopping at
six: it is read once at start like the other `OD_*` pins and says so in the log
(`OD_SCHEDULED_RESTART=off is set: this board does not stop itself at 06:00 ...`). It is
for a rig under a replay and for a tester with a long clock: `testers/i1555_run.sh`
defaults to it and forwards it, because the bakeoff runs unbounded in a container whose
zone is UTC and a gate crossing 06:00 UTC (09:00 in Helsinki) would otherwise measure a
replay that stopped at a ten-minute mark. Any other tester that runs the detector unbounded
past six in the container's zone wants the same line. `OD_SCHEDULED_CLOCK=<unix seconds>`
is test-only and is an injected clock for the rule alone: the schedule reads that instant
when it is armed (the start of scoring) and advances with the steady clock from there, the
log's own timestamps stay the machine's, and nothing deployed sets it.

**Measured on this box, 2026-10-11** (`testers/i1797_replay.sh`, `mocks/rig-20260918`, the
dev binary built with `-j2` in a 3 GB container in 339 s, `--cpus=2`, the container at
23:xx UTC). The pure checks first: `1797-scheduled` PASS in 30 s (40 checks on the rule and
the launcher's side, 24 on the re-carry, 0 failed); `--mutate-clock` (`kStopHour` 7) turns
16 of the 40 red, each naming the hour (`06:00:00 stops (answered NotYet, due 07:00:00)`);
`--mutate-ending` (the `kScheduledStop` branch of `endingOf()` deleted) turns 2 of 40 and
11 of 24 red (`exit code 60 is Ending::Scheduled (got faulted)`, `fetch_manifest called 0
times`, `kScheduledNotFollowed (41)`). Beside them, unedited: `1303-launcher` PASS 60 s,
`1306-install` PASS 35 s, `1305-manifest` PASS 35 s, `1383-blind-end` PASS 223 s.
Then the replays. **Control** (pin unset, the real clock): the schedule armed with `due at
2026-10-11 06:00 local`, no stop, the replay ran to `END OF FOOTAGE` and exited 0 with
24 `SCORE:` lines (25, 25 and 25 on three earlier runs of the same binary under a heavier
host load; the replay pace moves with the box, #1683). **Forced** (`OD_SCHEDULED_CLOCK`
= 05:59:58 UTC, paired to a closed loopback port so the client runs): the schedule armed
at `23:18:50.594` with `due at 2026-10-10 06:00`, and at `23:18:52.611` -- two seconds
into scoring, before any dart -- the log reads, in this order: `I1797 SCHEDULED RESTART
at 06:00: the launcher looks for an update and starts the board again`, `Scorer stopped`,
`TURNAUS: log upload: 0 bytes of /run1797/forced.log acknowledged by the server this run`,
`TURNAUS: client stopped. queued=0 delivered=0 ...`, `WebSocket service stopped`; exit
code 60. Two earlier forced runs with the clock set 40 s and 20 s after the arming met the
guard instead -- the first dart had landed 3 s and 1 s before -- and said `I1797 SCHEDULED
RESTART postponed to 06:10: a dart was published 3 s ago, inside the 10-minute guard`, then
ran to the end of the footage and exited 0: the guard measured live, the stop not.

**The overnight measurement on the rig is the maintainer's**: a board left running
overnight restarts at 06:00, the log shows the sentence, `update\state.txt` shows
`scheduled`, and the new run's first lines name the version Turnaus offered. Not yet done.

## A session read back: the truth line, the log and the fault classes (turnaus#1789)

What the board posts (#1787) Turnaus keeps per dart beside the thrower's corrections and
exports per board-day as a **truth line** (turnaus#1786), with the day's uploaded log
beside it. `testers/i1789_truth_census.py` reads both back on this side: it recounts the
four counts the desk shows, prints every corrected dart with its log window, and names
each one's fault class. The server's counts are the authority; this is the independent
recount, so the two can be compared.

**Fetching a session.** A Platform Operator issues an operator token on the Turnaus
operator desk (turnaus#1790; it holds the one ability `operator:boards.read`). With it,
for a board's `kind` (`device` or `casual`), its id and the Finnish calendar day:

    TOKEN=...   # the operator token, shown once when it is issued
    BASE=https://<turnaus host>/api/v1/operator/boards/device/<id>/2026-10-10
    curl -fsS -H "Authorization: Bearer $TOKEN" "$BASE/truth-line" > truth-line.txt
    curl -fsS -H "Authorization: Bearer $TOKEN" "$BASE/log"        > log.txt
    curl -fsS -H "Authorization: Bearer $TOKEN" "$BASE"            > day.json   # the server's own counts
    curl -fsS -H "Authorization: Bearer $TOKEN" "$BASE/frames/<reference>/<camera>" > frame.png

Any credential but an operator's answers 404 there and a revoked token 401. The script
reads files, never the network; run it in the image (no host python3 on the box):

    docker run --rm --network none -v "$PWD":/app:ro -w /app od-amd64:bullseye \
      python3 testers/i1789_truth_census.py --truth truth-line.txt --log log.txt

**The format it reads is Turnaus main's, and it reads it by name.** The header is
`# turnaus truth line v1 board=<kind>/<id> day=<day> zone=Europe/Helsinki`, then
`# <columns>`, then one space-separated line per dart, `-` for anything not said. The
columns are `BoardSession::COLUMNS`: `reference published alternative corrected picked
path flagged degraded candidates cameras crossing_deg sigma_mm margin_mm wire_kind agreeing
lone_wire_mm ring_wire_mm radius angle bounced outcome corrections posted_at`. `degraded`
was added after `flagged` while the header still said v1, so the field order is taken from
the export's own columns line and never by position; a column the script needs that is not
named, a dart line whose field count is not the columns line's, a `picked` that is not one
of `App\Autoscoring\Pick`'s (`alternative published candidate typed removed turn`) or a
`path` that is not `geometry`/`vote`/`-` is refused with exit 3, naming the line, and
nothing is counted. Sectors are #821's grammar (`None` is a miss, `25` and `Bull`).

**What each count means** (`I1789 COUNT path=<geometry|vote|unknown|total>`). A dart is
counted by its last correction, which is what `picked` carries (Turnaus's
`BoardSession::countOf`):

| count | the dart | what it says |
|---|---|---|
| `alternative` | flagged, corrected to the board's alternative | the flag worked |
| `candidate` | flagged, corrected to another of its candidates | the flag offered the wrong coin, the payload held the right one (#1782's shape) |
| `typed` | flagged, corrected to something offered nowhere | the flag offered the wrong coins |
| `unflagged` | not flagged, corrected to anything | nothing warned |
| `removed` | the dart was never there | a phantom (#1781); in none of the four |
| `undone` | the last correction put back what was published | no correction at all |
| `turn` | a Casual turn's total was corrected | names no dart; in none of the four |
| `corrected` | any correction at all | the desk's figure |
| `false_flag` | flagged and its published score stood | `false_flag_rate` is it over `flagged` |

**The join.** The client prints every accepted push as `TURNAUS: <OUTCOME> <body>`, and the
body carries the dart's `reference`. The dart's window is its `SCORE:` line (the nearest
before the push whose sector is the published one) back to the previous `SCORE:` line, and
the script prints its `SCORE`, `UNCERTAINTY`, `Geometric score`, `Consensus score` /
`No consensus`, `LONE-WIRE`, `RING-WIRE`, `BOARD` and `I1707 RIM CARRIED` lines and the push
line. A corrected dart whose reference is not in the log is printed `I1789 ABSENT`, never
dropped. `I1789 PICK-DISAGREES` names a dart whose `picked` is not what Turnaus's
`DartCorrections::picked` would decide from the sectors.

**The fault classes**, from the account, the first that holds winning:

| class | issue | holds when |
|---|---|---|
| `turn-total` | #1786 | `picked` is `turn`: a Casual turn's total was corrected, which names no dart, so no fault class is claimed; the `why` says what the account alone would have called it |
| `phantom-takeout` | #1781 | the dart's SCORE line is within 2.0 s of a `SCORE: END` -- needs the log, because the truth line carries no END |
| `reread-after-takeout` | #1820 | a removed dart whose SCORE `Position` is within 12 px of a dart of the visit the END before it closed, no END between -- needs the log |
| `phantom-unplaced` | -- | any other removed dart: never there, and nothing here says why; a removed dart is never put in a wire class |
| `miss-for-a-dart` | #1821 | published a miss (`None`) and corrected to a sector |
| `rim-one-tip` | #1707 | a lone vote reading (`agreeing` 1) published a double, or was corrected to a miss |
| `corner` | #1782 | a geometric solve within its own `sigma_mm` of a ring wire AND a wedge wire (the account's margin for one, the other from `radius`/`angle` on the 170 mm model) |
| `two-line-wire` | #1766 | a two-camera geometric solve within its own `sigma_mm` of the wire it names |
| `three-line-wire` | -- | the same with three or more cameras: the flag's own case, not a fault |
| `lone-beyond-sigma` | #1822 | a lone vote reading corrected across a wire it was more than 5 mm clear of |
| `ring-wire` | #1773 | a vote reading, lone or consensus, within 5 mm of a ring wire |
| `lone-wedge-wire` | #1628 | a lone vote reading within 5 mm of a wedge wire; the nearer margin decides against `ring-wire` |
| `unflagged-geometric` | #1556 | a geometric solve published unflagged and corrected |
| `unclassified` | -- | the honest default |

The vote classes read `ring_wire_mm` / `lone_wire_mm` where the board posted them, and
otherwise the same margins from `radius`/`angle` on the 170 mm model (no wedge wire inside
the 25's wire), marked `(model)` in the `why`: casual/17's build posted neither for any of
its 526 darts. Which wire a correction crossed is read from the two sectors -- the number
changed is a wedge wire, the ring letter a ring wire.

The 2.0 s is #1789's own figure; #1781's phantoms were 0.74 s before and 0.98 s after their
END. The 5 mm is #1628's lone-reading sigma, which #1773 reused for ring wires.
The 12 px is #1819's: casual/20's two re-reads sat 0.0 and 9.2 px from the darts they
repeated.

**The first two real sessions (#1819).** Before #1819 the census left 33 darts
`unclassified` (24 on casual/17, 9 on casual/20). Twenty-two were darts of a corrected
Casual turn -- a correction that names no dart -- and the census had also put 34 more such
darts into fault classes, 14 of them in `unflagged-geometric`. Four were removed darts
(and five more removed darts sat in wire classes). Seven were real corrections: two
`miss-for-a-dart`, two `lone-beyond-sigma`, one `three-line-wire`, and one each for
`ring-wire` and `lone-wedge-wire` that casual/17's missing margins had hidden. After it:

| board | turn-total | phantom-takeout | reread | phantom-unplaced | miss | rim-one-tip | corner | two-line | three-line | lone-beyond | ring-wire | lone-wedge | unflagged-geo | unclassified |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| casual/17 | 31 | 0 | 0 | 6 | 1 | 2 | 3 | 4 | 0 | 2 | 1 | 1 | 0 | 0 |
| casual/20 | 25 | 0 | 2 | 1 | 1 | 2 | 2 | 4 | 1 | 0 | 1 | 2 | 1 | 0 |


**The reference set: 2026-10-10 (build 2b56b48).** `testers/fixtures/i1789/` holds the
session as a hand-written truth line of 25 darts and a log excerpt written from the lines
the issues quote: the live logs are not on this machine, and 2b56b48 predates the account,
so the fixture is what a board posting the account would have exported for the darts
#1707, #1773, #1781, #1782 and #1783 describe; the header of each file says what is
recorded, what is computed and what is invented. Its census:

| path | posted | corrected | alternative | candidate | typed | unflagged | removed | flagged | false flags |
|---|---|---|---|---|---|---|---|---|---|
| geometry | 3 | 2 | 1 | 1 | 0 | 0 | 0 | 3 | 1 |
| vote | 22 | 11 | 0 | 0 | 0 | 9 | 2 | 0 | 0 |
| total | 25 | 13 | 1 | 1 | 0 | 9 | 2 | 3 | 1 |

| time | thrown | published | class |
|---|---|---|---|
| 15:39:23 | miss | S20, lone, radius 0.562 (absent from the excerpt on purpose) | rim-one-tip (#1707) |
| 15:51:49 | S19 | T19, lone, 3.1 mm from the treble's inner wire | ring-wire (#1773) |
| 16:06:25 | S1 | OUTER, lone, 2.3 mm from the bull's wire | ring-wire (#1773) |
| 16:20:06 | S16 | D16, lone, radius 0.9615 | rim-one-tip (#1707) |
| 16:21:11 | miss | D3, lone, radius 0.998 | rim-one-tip (#1707) |
| 16:24:24 | S3 | MISS, no reading | unclassified (#1781 notes it, one instance) |
| 16:24:28 | -- | D11, removed, 0.74 s before the END | phantom-takeout (#1781) |
| 16:24:30 | -- | S2, removed, 0.98 s after the END | phantom-takeout (#1781) |
| 16:28:35, 16:28:37 | miss, miss | D18, D18, lone, radius 0.999 / 0.990 | rim-one-tip (#1707) |
| 16:38:53 | S19 | T3 flagged S3, two lines, 0.6 / 1.8 mm across 5.3 mm | corner (#1782) |
| 16:47:22 | BULL | OUTER, two cameras, 0.5 mm from the bull's wire | ring-wire (#1773) |
| 16:49:15 | S20 | S1 flagged S20, two lines, 2.1 mm across 5.1 mm | two-line-wire (#1766) |

The session has no instance of `lone-wedge-wire` or `unflagged-geometric` (#1773 records
every unflagged geometric solve of the day right); the self-test holds those two rules on
hand-built accounts, not on session data.

**Measured (`testers/run_all.sh 1789-truth`, 8.4 s on 2026-10-11 with #1819).** The
self-test (35 cases); the fixture, now with ten more real rows from the two sessions and
twenty more real log lines, against its header's 36 expectations; and five mutations, each
predicted first (two more are #1817's, below, and two #1819's: casual/20's 00:57:20.921 END
struck moves exactly the two re-reads to `phantom-unplaced`, and the first re-read's
Position moved 13 px moves exactly that one):
the 16:49:15 dart's `picked` moved from `alternative` to `typed` turns exactly
`geometry.alternative`, `geometry.typed`, `total.alternative` and `total.typed` red and
`PICK-DISAGREES` names it; `degraded` struck from the columns line is refused on the first
dart line (23 fields where the columns line names 22) with no count printed; the 16:24:29
END struck from the log moves exactly the two phantoms, to `phantom-unplaced` (before
#1819, to `rim-one-tip` and `ring-wire`: a removed dart is no longer put in a wire class).

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
