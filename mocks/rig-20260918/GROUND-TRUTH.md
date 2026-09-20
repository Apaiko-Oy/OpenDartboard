# What was actually thrown in this footage

Recorded by the maintainer on 2026-09-20 by watching `cam_1.mp4`, and the first ground
truth this project has ever had for any footage.

**Everything else in this repository measures the detector against itself** — whether three
cameras agree, whether a count comes out at twenty, whether a fit is self-consistent. This
file is the only thing that can say whether a score is **right**.

## The darts

Four visits, twelve throws, two of them misses.

| visit | 1 | 2 | 3 |
| --- | --- | --- | --- |
| 1 | 10 | 7 | **T20** |
| 2 | 19 | 20 | *miss* |
| 3 | 15 | 4 | 18 |
| 4 | *miss* | 7 | 2 † |

† The last dart was read as a 2 from `cam_1` alone and the maintainer flagged it as
uncertain — a second camera would settle it. Treat it as unconfirmed rather than as fact,
and do not let a check turn red or green on that one dart alone.

## How to use it

**Order is per visit, not global.** The detector publishes darts in the order it detects
them and separates visits with an `END` line; align visit-to-visit rather than assuming a
single sequence. A visit where the detector found fewer darts than were thrown is a
**detection** failure and is a different finding from a wrong score.

**A miss is not nothing.** The detector publishes a dart outside the scoring area as
`OUTER`, so the two misses above should appear as `OUTER` and their absence is as much a
finding as a wrong wedge.

## Measured against it, on merged `main` at `77710ca`

`OD_MAX_CYCLES=1200`, all three cameras, 2026-09-20. Eleven darts detected of twelve
thrown:

| visit | detector | thrown |
| --- | --- | --- |
| 1 | `OUTER` (569,251) · `S20` (758,267) · `OUTER` (614,332) | 10 · 7 · T20 |
| 2 | `S20` (760,266) · `OUTER` (731,227) · `S20` (543,440) | 19 · 20 · miss |
| 3 | `OUTER` (616,102) · `OUTER` (895,139) · `OUTER` (809,338) | 15 · 4 · 18 |
| 4 | `OUTER` (895,139) · `S20` (484,368) | miss · 7 · 2 † |

**Eight of eleven published as `OUTER` — outside the scoring area — where only two misses
were thrown.** Six real darts are being read as off the board entirely.

That is the shape a wrong **radial scale** makes, and #1423 measured the cause on this very
fixture: the colour stage was measuring the **treble** ring while believing it had the
board, so a dart at true radius *r* reads at *r* × 170/107 = **1.589r** and falls off the
edge. The three `S20`s are all at `confidence 0.5`, which is `by_default` — **no camera
measured a wedge at all** and #1346's fallback asserted the 20.

So this run shows two distinct faults at once, and they must not be conflated: a **scale**
error putting darts off the board, and an **anchor** failure meaning the wedge is asserted
rather than read.

## What this file is not

It is not a check. Nothing fails because of it. It is the reference a check can be written
against, and the first slice of that work is the confidence census — which separates *wrong
score* from *no anchor* without needing this file at all.

The shipped mocks (`mocks/cam_*.mp4`) have no ground truth and **must not be used to judge
accuracy**: they are upstream footage that every calibration constant in this repository was
fitted against (#1478), so a good result there is circular.
