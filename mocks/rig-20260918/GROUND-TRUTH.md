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

## Measured against it, on merged `main`

**Measured 2026-09-21 over the whole clip** by `1484-confidence`, with **no cycle budget**:
the detector plays all 60 s in 62 s and ends on `END OF FOOTAGE` after 1694 cycles. An
earlier run here used `OD_MAX_CYCLES=1200`, reached four visits, and its figures were
quoted as if they were the clip's — they were not. **This fixture needs no cap.**

**19 darts detected of 21 thrown. 2 scored correctly.**

| | |
| --- | --- |
| detected | 19 (visits 1–5 complete, visits 6 and 7 two apiece) |
| correct | **2** — both a thrown 20 published `S20` |
| `OUTER` where a scoring dart was thrown | **8** |
| a different number entirely | 7 |
| a score published where a *miss* was thrown | 2 — **both misses were missed** |

Confidence: **2 at 0.9, 6 at 0.7, 11 at `by_default` 0.5.**

**Read those three with care.** `scorePoint` returns `OUTER` before `wedge_measured` is set,
so every `OUTER` counts as a camera that measured — **not one dart in this clip had a wedge
measured**, though 8 published at 0.7 or 0.9. That is #1489, and until it is fixed a change
pushing more darts off the board reads as the anchor improving.

**The two missing darts are a detection failure, not truncation.** The run did not end early;
visits 6 and 7 simply yielded two darts each where three were thrown.

Eight `OUTER`s against two misses thrown is the shape a wrong **radial scale** makes, and
#1423 measured that cause on this fixture: the colour stage measuring the **treble** ring
while believing it had the board, so a dart at true radius *r* reads at *r* × 170/107 =
**1.589r** and falls off the edge. That is #1485.

So two distinct faults are visible at once and must not be conflated: a **scale** error
putting darts off the board, and an **anchor** failure meaning no wedge is ever measured.

## What this file is not

It is not a check. Nothing fails because of it. It is the reference a check can be written
against, and the first slice of that work is the confidence census — which separates *wrong
score* from *no anchor* without needing this file at all.

The shipped mocks (`mocks/cam_*.mp4`) have no ground truth and **must not be used to judge
accuracy**: they are upstream footage that every calibration constant in this repository was
fitted against (#1478), so a good result there is circular.
