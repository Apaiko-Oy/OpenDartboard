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

**A miss is not nothing.** A dart outside the scoring area publishes as `OUTER`, so the two
misses above should appear as `OUTER` and their absence is as much a finding as a wrong
wedge.

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
four visits.** So at least seven real darts are read as off the board entirely — an
entire visit of three among them.

That is the shape a wrong **radial scale** makes, and #1423 measured that cause on this
fixture: the colour stage measuring the **treble** ring while believing it had the board,
so a dart at true radius *r* reads at *r* × 170/107 = **1.589r** and falls off the edge.
The three `S20`s are all at confidence 0.5 — `by_default`, meaning **no camera measured a
wedge** and #1346's fallback asserted the 20. None of those throws was a 20.

**Not one score is correct.** Two distinct faults are visible at once and must not be
conflated: a **scale** error putting darts off the board, and an **anchor** failure meaning
the wedge is asserted rather than read.

## What this file is not

It is not a check. Nothing fails because of it. It is the reference a check can be written
against, and the first slice of that work is the confidence census — which separates *wrong
score* from *no anchor* without needing this file at all.

The shipped mocks (`mocks/cam_*.mp4`) have no ground truth and **must not be used to judge
accuracy**: they are upstream footage that every calibration constant in this repository was
fitted against (#1478), so a good result there is circular.
