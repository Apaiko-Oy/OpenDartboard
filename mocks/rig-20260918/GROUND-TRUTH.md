# What was actually thrown in this footage

Recorded by the maintainer on 2026-09-20, **read from all three cameras** rather than from
one, and the first ground truth this project has ever had for any footage.

**Everything else in this repository measures the detector against itself** — whether three
cameras agree, whether a count comes out at twenty, whether a fit is self-consistent. This
file is the only thing that can say whether a score is **right**.

## The darts

Seven visits, twenty-one throws, two of them misses.

| visit | 1 | 2 | 3 |
| --- | --- | --- | --- |
| 1 | **T13** | 13 | 19 |
| 2 | 19 | **T14** | 5 |
| 3 | 10 | 7 | **T20** |
| 4 | 19 | 20 | *miss* |
| 5 | 15 | 4 | 18 |
| 6 | *miss* | 7 | 2 |
| 7 | 5 | 20 | 20 |

Every dart here was confirmed against more than one camera. An earlier draft of this file
flagged the 2 in visit 6 as uncertain because it had been read from `cam_1` alone; the
maintainer then checked all three and it stands. **There is no unconfirmed dart in this
table** — a check may turn on any row of it.

## How to use it

**Order is per visit, not global.** The detector publishes darts in the order it detects
them and separates visits with an `END` line; align visit-to-visit rather than assuming a
single sequence. A visit where the detector found fewer darts than were thrown is a
**detection** failure and is a different finding from a wrong score.

**Align from the first visit.** A run truncated by `OD_MAX_CYCLES` covers the *opening* of
the clip, not an arbitrary window — a comparison that starts anywhere else is measuring
nothing. This is not hypothetical: the first comparison written into this file matched the
detector's four visits against visits 3–6 and had to be redone.

**A miss is not nothing.** A dart outside the scoring area publishes as `MISS`, so the two
misses above should appear as `MISS` and their absence is as much a finding as a wrong
wedge.

**`OUTER` is not that, and this file said it was until #1485.** `OUTER` is the OUTER BULL --
the 25 ring, the green ring around the bullseye. `score_processing::scorePoint` sets it for
a point inside `outerBullEllipse` and outside `innerBullEllipse`, and
`turnaus_client::postableSector` posts it to Turnaus as a score of **25**. The vocabulary is
in `docs/api.md`: `S1`-`D20`, `BULL`, `OUTER`, `MISS`, `END`. Nobody threw at the 25 ring in
this footage, so every `OUTER` in the table below is a dart on the board published as a 25 --
which is a much worse reading than "off the board" and points at the opposite fault. The
sentence that was here sent #1485 looking for a radial scale that reads a dart FURTHER out
than it is; the measurement is that they are read NEARER the bull, by up to 5.9x.

**Three trebles are in here** — T13, T14 and T20 — and they are the only evidence in this
repository about whether a treble can be read at all. A treble published as a single is a
*ring* error and belongs to a different fault from a wrong wedge.

## Measured against it, on merged `main` at `77710ca`

`OD_MAX_CYCLES=1200`, all three cameras, 2026-09-20. **The run was truncated by that cap
after four visits**, so this compares visits 1–4 — twelve throws, one of them a miss —
and never reached visits 5, 6 or 7. A later run must raise the cap or it will keep
measuring three sevenths of the footage and calling it all of it.

| visit | detector | thrown |
| --- | --- | --- |
| 1 | `OUTER` (569,251) · `S20` (758,267) · `OUTER` (614,332) | T13 · 13 · 19 |
| 2 | `S20` (760,266) · `OUTER` (731,227) · `S20` (543,440) | 19 · T14 · 5 |
| 3 | `OUTER` (616,102) · `OUTER` (895,139) · `OUTER` (809,338) | 10 · 7 · T20 |
| 4 | `OUTER` (895,139) · `S20` (484,368) | 19 · 20 · *miss* |

**Eight of eleven detected darts published `OUTER`, and only one miss was thrown in those
four visits.** So at least seven real darts were published as a score of **25** — an
entire visit of three among them.

**#1485 found the cause and it is not the one this paragraph guessed.** The guess was a
radial scale reading a dart at *r* × 170/107 = 1.589r and off the edge, after #1423's
finding that the colour stage measures the treble ring on this rig. Measured instead: the
three cameras' ray-traced doubles ellipses here are **316.05, 317.98 and 315.29 px**, the
real doubles ring, agreeing within 0.85%. What is wrong is the ellipse named the **25
ring**, fitted by `ellipse_processing`'s contour stage at **0.9733, 0.6112 and 0.3401** of
the board where the millimetres put it at 15.9/170 = **0.0935**. `scorePoint` tests the bull
ellipses first, so a dart anywhere inside that contour is a 25.

The three `S20`s are all at confidence 0.5 — `by_default`, meaning **no camera measured a
wedge** and #1346's fallback asserted the 20. None of those throws was a 20.

**Not one score is correct.** Two distinct faults are visible at once and must not be
conflated: a **ring** error publishing darts on the board as a 25, and an **anchor**
failure meaning the wedge is asserted rather than read.

## Measured again on the whole of the footage, at `OD_MAX_CYCLES=6000` (#1485)

The cap that stopped the run above at four visits is not a property of the footage. Raised,
the same binary reaches the end of all three clips and publishes **19 darts over seven
visits** — every dart thrown that hit the board, and neither miss. Before #1485's repair and
after it, on one binary, the fix selected at run time with `OD_RINGS`:

| | darts | published `OUTER` (a 25) | wedge measured |
| --- | --- | --- | --- |
| `OD_RINGS=asfitted` (before) | 19 | **8** | 0 of 19 |
| this tree | 19 | **0** | 0 of 19 |

The same darts, in the same order, with the same tip positions: only the **ring** moved. The
three trebles are still published as singles — the treble ellipses on this rig sit at
0.533/0.607 and 0.529/0.610 of the board where the millimetres put them at 0.582/0.629, so a
treble reads about 8% short — and **every** dart is still the asserted 20, which is the
anchor failure and a different issue. So this footage still scores 0 of 19 correct; what
changed is that the ring is now a near miss rather than a 25.

## What this file is not

It is not a check. Nothing fails because of it. It is the reference a check can be written
against, and the first slice of that work is the confidence census — which separates *wrong
score* from *no anchor* without needing this file at all.

The shipped mocks (`mocks/cam_*.mp4`) have no ground truth and **must not be used to judge
accuracy**: they are upstream footage that every calibration constant in this repository was
fitted against (#1478), so a good result there is circular.
