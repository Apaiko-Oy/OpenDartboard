# #1511: independently hand-measured shaft lines and entry points

The reference the axis census (`testers/i1511_census.py`) and the scoring bake-off
(`testers/i1555_census.py`) judge accuracy against. Nothing here came out of the
detector, and that is the point (#1504: aligning or annotating by detection order is
what the acceptance forbids): every line is measured off **raw pixels**, anchored to
**visit + dart-in-visit identity from the fixtures' ground truth**
(`mocks/rig-20260918/GROUND-TRUTH.md`; `mocks/rig-20260922/GROUND-TRUTH.md`) and to the
**frame index** it was measured on.

## How each line was measured

Two instruments, and every row in these CSVs can be re-audited by one command with
either.

**rig-20260918, and rig-20260922's visits 2-4 up to #1585** — by eye, with
`testers/i1511_frame_tool.cpp` compiled in the tester container, three modes in this
order:

1. **Which frame.** Contact sheets (`sheet`, one thumbnail per second) locate each
   dart's landing window; step blends (`frame <idx> <prev>`, which paints what changed
   between two frames red on the current frame) pin which dart is the NEW one in a
   window, so identity comes from arrival time against the ground-truth order, never
   from where the detector says a dart is. The blend is an eye aid only — every
   coordinate is read off raw pixels.
2. **Which pixels.** `check <idx> <x1> <y1> <x2> <y2>` draws a candidate line over an
   upscaled crop. The candidate is adjusted until it lies along the dart's visible
   barrel/shaft centreline; a misread coordinate is obvious as a line beside a shaft.
3. **The rule for the points.** `x1,y1`–`x2,y2` are two points on the dart's visible
   barrel/shaft centreline, as far apart as is cleanly visible. `tip_x,tip_y` is the
   visible board-contact point; it is EMPTY where the entry is occluded or does not
   exist (a resting miss), never invented. Notes name occlusion, overlap and
   out-of-plane conditions.

**rig-20260922 since #1585** — fitted to the same pixels the step blend paints, because
by eye was the limiting error. For a dart that arrives at frame `a`, the mask
`|frame(a+29) - frame(a-12)| > 26` is that dart and nothing else on the board; per image
row the longest contiguous run of mask inside a column window is taken, runs too wide to
be a shaft (a flight) or too narrow to be one are dropped, and `x = m*y + c` is
least-squares fitted to the run centres with three rounds of 4 px outlier rejection. Every
row here has a max residual of **4.5 px or better** over 39–157 image rows. `tip_x,tip_y`
is the fitted line at the **lowest** row the mask reaches — the visible entry. The parked
dart (v1.1), which never arrives, is masked against **f260** instead, where the board is
empty.

Both instruments produce the same kind of row and the same audit command, and a row is
still ACCEPTED by looking:

    ftool check mocks/<fixture>/cam_<camera>.mp4 out.jpg <frame> <x1> <y1> <x2> <y2>

## Measurement uncertainty

By eye, endpoints are read at 3-4x zoom: about ±2 px per endpoint, worse where a note
says so. Over the 70–150 px baselines used, that bounds the annotation's own angle
uncertainty at roughly ±1.5–4°.

The fit is tighter and the tightening is measured rather than claimed: on rig-20260922
the residual bound above puts each fitted endpoint inside ~±1 px, so the angle floor is
about ±0.5–1°, and the axis census now reports median angle errors of 0.23–0.51° against
it. **That is why the by-eye rows for visits 2-4 were re-measured rather than kept**: a
census whose reference has two floors reports the worse one. The move is small and was
checked before it was made — truth v2.1 on cam2 was 7 px off at its upper endpoint (~3°),
inside the by-eye floor and obvious at 7x zoom.

## rig-20260922's visit indices were wrong until #1585, and the fixture's zero was the reference

The committed annotation filed every row **one visit late**, and #1555's bake-off read
0 correct on this fixture in both calibration windows because of it.

`GROUND-TRUTH.md` says truth visit 1 is `8 16 miss`, that the whole of it is thrown before
the recording's clean frame, and that the 8 is the parked dart #1514 found on the board
from frame one. That is the **pre-roll**, not the f455 visit the old rows annotated: cam_2
shows one dart at f0, a second arriving at f64, no third, a hand at f203-241 and an
**empty board at f260 and at f390**. The darts the old "visit 1" rows measured arrive at
f401, f463 and f518 on that emptied board, so they are truth visit **2**; the old visits 2
and 3 are truth visits 3 and 4.

Three independent measurements say so, and any one of them is enough:

- **The scores.** The f796/f857/f918 visit was filed as `12 T9 T8`. Its second dart's
  entry stands in the **bottom double ring** and its third sits in the **1** wedge — which
  is truth visit 3's `20 d20 1` exactly, and is nothing like a T9 or a T8. The same check
  run over every visit agrees with the corrected mapping on all 21 arrivals and disagrees
  with the old one everywhere.
- **The arrival census.** A per-frame changed-pixel trace over cam_2 and cam_3
  (`ffmpeg … tblend=all_mode=difference,lutyuv,signalstats`) agrees on **7 hand events**
  and **21 arrivals in 7 groups of three** after the first takeout, plus the parked dart
  and the single arrival at f64 before it. 1 + 1 + 21 = **23** darts, which is 24 thrown
  minus visit 1's miss. The accounting closes exactly, and only under this mapping.
- **The census itself.** Re-indexing the nine old rows and changing nothing else takes the
  dev window from `vote 0/6` to `vote 5/6` and the opening window from `0/5` to `5/6`. The
  zero was the answer key.

**#1554's hand-placed dart is the same error one layer down, and the row is no longer a
`--no-arrival`.** #1554 read "empty at f490, a hand blur over it at f520, the dart standing
at f540" as a dart placed by hand. Frame by frame it is a **thrown dart arriving**: f510,
f515 and f517 hold two darts, f518 shows a faint streak entering at the upper left, f519 is
a clean motion-blurred flight in mid-air, and f520 has three darts with the new one still
ringing. The changed-pixel magnitude is that of the other two arrivals in its visit and a
fifth of any real hand event in this clip. It is truth **v2.3, the T8**, and it arrives.
`--no-arrival` on this fixture is therefore **`1.1` alone**, not `1.1,1.3`.

## What is deliberately absent

- **rig-20260918 visit 6 dart 1** — thrown outside the board entirely (confirmed by
  the maintainer, #1504); it never rests in any camera's view. Unannotatable.
- **rig-20260918 visit 7 dart 3** — the footage ends before it lands.
- **rig-20260922 visit 1 dart 3 (the miss)** — the only one of the fixture's 24 throws
  that never reaches the board. There is nothing to annotate and nothing a detector could
  score; 23 of 24 is the ceiling on this recording.
- **rig-20260922 visit 2 dart 3 on camera 2** — the arrival mask is empty there: the dart
  is fully occluded behind darts 1–2 from that camera. Cameras 3 carries it.
- **rig-20260922 camera 1** — **only the dev window's exclusion, and it is not a property
  of the camera.** With the registry build's 3 s seek that camera never admits on this box
  (wire coherence 0.578 against the 0.60 gate, the #1510 watch item) and produces **0 valid
  axes in 18 windows**, so annotating it would measure nothing there. With
  `OD_SEEK_VIDEO=off` the same camera admits and produces **14 valid axes in 22 windows**
  (#1551), where an annotation WOULD measure something. It is left unannotated as a scope
  bound of #1585, recorded rather than hidden.
- **rig-20260918 visit 4's miss** IS annotated (it rests in view, off the board
  plane) so the census can hold "no event expected" against a real observation.

## Rows that are lines but not arrivals

**rig-20260922 v1.1 (the 8)** is the parked dart the maintainer's ground-truth correction
(e53891a) places on the board before frame one. It is a real resting line — the scene
later windows contain really does hold it until the f203-241 takeout — but nothing the
detector could ever emit should be scored against it, so every census run passes
`--no-arrival 1.1`. Excluded from the matching pool entirely, its annotation still earns a
SUSPECT-MATCH line for any unmatched event within the cap of it, so the diagnostic
survives.

The barred `mocks/cam_*.mp4` are not annotated and must not be
(`mocks/DO-NOT-USE-cam_1-cam_2-cam_3.md`).
