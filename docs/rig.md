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
  the same camera calibrates on every look (R=0.87 at the opening). The look budget
  now outlasts that dart (`geometry_detector.cpp`, `kFurtherLooks`).

## Board

Winmau Blade 6. Scoring radii are `DartboardSpec`/`BoardProfile`
(`board_model.hpp`, versioned); the 450 mm the manufacturer lists is the overall
diameter, not the scoring diameter.

## Fixtures

- `mocks/rig-20260918/` — evidence, with `GROUND-TRUTH.md` (21 throws, 2 misses).
- `mocks/rig-20260922/` — the deployment recording; starts with a parked dart
  (#1514), clean frame ≈ index 270 (~9 s), visit 1 is `8 16 miss` (corrected
  2026-09-24), footage ends mid-visit 8. Its `GROUND-TRUTH.md` carries the details.
- `mocks/cam_*.mp4` — upstream footage, **never evidence** (#1478); see
  `mocks/DO-NOT-USE-cam_1-cam_2-cam_3.md`.
- A lit, clean, complete re-recording is wanted: turnaus#1558.
