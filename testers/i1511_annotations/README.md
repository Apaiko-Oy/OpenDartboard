# #1511: independently hand-measured shaft lines and entry points

The reference the axis census (`testers/i1511_census.py`) judges accuracy against.
Nothing here came out of the detector, and that is the point (#1504: aligning or
annotating by detection order is what the acceptance forbids): every line was measured
**by eye**, anchored to **visit + dart-in-visit identity from the fixtures' ground
truth** (`mocks/rig-20260918/GROUND-TRUTH.md`; rig-20260922's one-line table) and to
the **frame index** it was measured on.

## How each line was measured

With `testers/i1511_frame_tool.cpp`, compiled in the tester container — three modes,
used in this order:

1. **Which frame.** Contact sheets (`sheet`, one thumbnail per second) locate each
   dart's landing window; step blends (`frame <idx> <prev>`, which paints what changed
   between two frames red on the current frame) pin which dart is the NEW one in a
   window, so identity comes from arrival time against the ground-truth order, never
   from where the detector says a dart is. The blend is an eye aid only — every
   coordinate is read off raw pixels.
2. **Which pixels.** `check <idx> <x1> <y1> <x2> <y2>` draws a candidate line over an
   upscaled crop. The candidate is adjusted until it lies along the dart's visible
   barrel/shaft centreline; a misread coordinate is obvious as a line beside a shaft.
   Every row in these CSVs is a line that was ACCEPTED through that mode, and any row
   can be re-audited by rerunning exactly:

       ftool check mocks/<fixture>/cam_<camera>.mp4 out.jpg <frame> <x1> <y1> <x2> <y2>

3. **The rule for the points.** `x1,y1`–`x2,y2` are two points on the dart's visible
   barrel/shaft centreline, as far apart as is cleanly visible. `tip_x,tip_y` is the
   visible board-contact point; it is EMPTY where the entry is occluded or does not
   exist (a resting miss), never invented. Notes name occlusion, overlap and
   out-of-plane conditions.

## Measurement uncertainty

Endpoints are read by eye at 3-4x zoom: about ±2 px per endpoint, worse where a note
says so. Over the 70–150 px baselines used, that bounds the annotation's own angle
uncertainty at roughly ±1.5–4°; an axis-accuracy figure below that floor is
unmeasurable with this reference.

## What is deliberately absent

- **rig-20260918 visit 6 dart 1** — thrown outside the board entirely (confirmed by
  the maintainer, #1504); it never rests in any camera's view. Unannotatable.
- **rig-20260918 visit 7 dart 3** — the footage ends before it lands.
- **rig-20260922 visit 1 dart 3 on camera 2** — fully occluded behind darts 1–2 from
  that camera; camera 3 carries it.
- **rig-20260922 camera 1** — that camera does not admit on this box (wire coherence
  0.578 against the 0.60 gate, the #1510 watch item), so it produces no axis
  observations to judge; annotating it would measure nothing.
- **rig-20260922 visits 4–8** — scope bound, recorded not hidden: visits 1–3 carry
  the fixture's distinctive hazards (the parked-dart reference window, the #1535
  re-report dart, adjacent-wedge overlap pairs, a double). The coverage/refusal
  census still runs over the WHOLE clip; only per-dart accuracy is limited to the
  annotated subset.
- **rig-20260918 visit 4's miss** IS annotated (it rests in view, off the board
  plane) so the census can hold "no event expected" against a real observation.

## Rows that are lines but not arrivals (#1554)

Two rig-20260922 rows annotate REAL resting lines of darts that never *arrive* on the
recording, so nothing the detector could ever emit should be scored against them:

- **v1.1 (the 8)** is the parked dart the maintainer's ground-truth correction
  (e53891a) places on the board before frame one.
- **v1.3 (the 7)** was **placed by hand**: verified against the frames for #1554 --
  the annotated spot is empty at f490, a hand blur covers it at f520 (the row's own
  frame), and the dart stands on the annotated line from f540 on. #1512's corrected
  truth ("never hit the board") and this row are BOTH right: there was no throw, and
  there is a dart resting there.

The rows stay, because a resting line is what later windows' scenes really contain --
but every census run passes `--no-arrival 1.1,1.3`, which keeps them out of the
matching pool entirely (a later throw landed 2 px from v1.3's line, and letting the
row compete dragged the whole monotone assignment a dart late) and reports the
nearest unmatched detection as a SUSPECT-MATCH instead.

The barred `mocks/cam_*.mp4` are not annotated and must not be
(`mocks/DO-NOT-USE-cam_1-cam_2-cam_3.md`).
