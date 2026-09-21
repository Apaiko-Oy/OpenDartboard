# The shipped mocks are not ours. Do not use them. Do not compare anything to them.

`mocks/cam_1.mp4`, `mocks/cam_2.mp4` and `mocks/cam_3.mp4` are **upstream OpenDartboard
footage**. A different board (a Unicorn, with a bent-wire number ring), different cameras, a
different room, a different mounting. **They are not this product's hardware and never were.**

**Maintainer's decision, 2026-09-22: they are to be deleted.** Until they are, this file
stands in their place.

## The rule

**Nothing may be judged against these clips. Not geometry, not scoring, not accuracy, not a
constant, not a regression.** A result here says nothing about whether the detector works,
and a *good* result here is worse than a bad one because it is reassuring and meaningless.

If you are about to quote a figure from this footage: **do not**.

## Why, in one line each

- **Every calibration constant in this repository was fitted against them** (#1322, #1478).
  `roi_processing.cpp` still carries `horizontalScale = 0.95f  // (was 1.1f - too wide!)`.
  Measuring the constants against the footage they were fitted to is circular.
- **They cannot exhibit the faults that matter.** A camera anchors on them, so they scored
  9 of 9 while the maintainer's rig scored **2 of 19 by coincidence** and had never once
  measured a wedge. That is how six weeks of work sat on top of a stage nobody could see was
  broken.
- **They have misled measurement repeatedly**, most recently on 2026-09-21: figures taken
  from **23.6% of the clip** were quoted as the clip's, and their "visit 1" was read as the
  rig's.

## What to use instead

**`mocks/rig-20260918/`** — the maintainer's own hardware, and the only footage in this
repository with recorded ground truth (`GROUND-TRUTH.md`: seven visits, twenty-one throws,
read from all three cameras).

## Why they are still here

**Fifty-five tester files still read them** (#1480's census). Deleting the clips while those
files name them is a broken suite, not a cleanup. The re-pointing is #1478 and it is the only
thing standing between this file and `git rm`.

Two of the 55 **cannot** simply re-point and need a decision rather than a slice:

- **`1445-looks`** keys its falsification on the literal string `mocks` — these clips must
  answer 3 of 3 with the retry disabled while every other fixture must not. The asymmetry
  *is* the falsification.
- **`1339-denominator`** makes the board smaller in frame; the rig's largest region is
  already 1.07x from not calibrating at all, so there is nothing smaller to make from it.
