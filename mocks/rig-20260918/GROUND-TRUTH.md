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
| 1 | **T13** | 19 | 13 |
| 2 | 19 | **T14** | 5 |
| 3 | 10 | 7 | **T20** |
| 4 | 19 | 20 | *miss* |
| 5 | 15 | 4 | 18 |
| 6 | *miss* | 7 | 2 |
| 7 | 5 | 20 | 20 |

**Visit 1's second and third darts were transposed in the first recording of this file and
were corrected on 2026-09-21.** They read 19 then 13, not 13 then 19. The error was the
transcription's, not the maintainer's reading, and it is recorded rather than quietly fixed
because it cost a real conclusion: visit 1 was the one visit the detector appeared to get
wholly wrong, and that appearance was this row.

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

## Measured against it, on merged `main`

**Measured 2026-09-21 over the whole clip** by `1484-confidence`, with **no cycle budget**:
the detector plays all 60 s in 62 s and ends on `END OF FOOTAGE` after 1694 cycles. An
earlier run here used `OD_MAX_CYCLES=1200`, reached four visits, and its figures were
quoted as if they were the clip's — they were not. **This fixture needs no cap.**

**19 darts detected of 21 thrown. 2 scored correctly.**

| | |
| --- | --- |
| detected | 19 (visits 1–5 complete, visits 6 and 7 two apiece) |
| correct | see the definitive section below |
| `OUTER`, i.e. published as a **25** | **8** |
| a different number entirely | 7 |
| a score published where a *miss* was thrown | 2 — **both misses were missed** |

Confidence: **2 at 0.9, 6 at 0.7, 11 at `by_default` 0.5.**

**The two missing darts are a detection failure, not truncation.** The run did not end early;
visits 6 and 7 simply yielded two darts each where three were thrown.

## The cause of the eight 25s, measured rather than guessed

An earlier draft of this file guessed a radial scale reading a dart at *r* × 170/107 =
**1.589r** and off the edge, after #1423 found the colour stage measuring the treble ring on
this rig. **#1485 measured that guess false.** The three cameras' ray-traced doubles ellipses
here are **316.05, 317.98 and 315.29 px** — the real doubles ring, agreeing within 0.85%.

What is wrong is the ellipse **named** the 25 ring. `ellipse_processing`'s contour stage fits
it at **0.9733, 0.6112 and 0.3401** of the board where the millimetres put it at 15.9/170 =
**0.0935** — 3.6× to 10.4× too large on all three cameras — and `scorePoint` tests the bull
ellipses first, so a dart anywhere inside that contour becomes a 25. The stage fits five of
six rings to whatever colour blob was largest and **names them after the ring they were
expected to be**, with nothing asking whether they are it.

## Why the confidence figures cannot be read at face value

**Not one dart in this clip had a wedge measured** — every `BOARD:` line says so — yet **8
published at 0.7 or 0.9.**

The mechanism is not an early return, which an earlier draft also guessed wrongly.
`chooseScore` splits the cameras on `wedge_asserted` **alone**, so a reading with *neither*
flag set lands in the bucket named `measured`. That is #1489.

Until it is repaired, a change pushing **more** darts into the 25 reads as a **rise** in 0.7
and 0.9 — the anchor apparently improving while it did nothing.

So two distinct faults are visible at once and must not be conflated: a **ring** error making
darts 25s, and an **anchor** failure meaning no wedge is ever measured.

## What this file is not

It is not a check. Nothing fails because of it. It is the reference a check can be written
against, and the first slice of that work is the confidence census — which separates *wrong
score* from *no anchor* without needing this file at all.

The shipped mocks (`mocks/cam_*.mp4`) have no ground truth and **must not be used to judge
accuracy**: they are upstream footage that every calibration constant in this repository was
fitted against (#1478), so a good result there is circular.

## Definitive measurement on merged `main` (`04d1902`), 2026-09-21

Taken by `1484-confidence` after a rebuild, whole clip, no cycle cap. This supersedes every
earlier figure in this file: those were measured on trees missing #1485 or #1489.

```
0.9  two or more cameras MEASURED a wedge and agreed      0
0.7  measured, but no two agreed                          0
0.5  by_default: NO camera measured a wedge              19
     darts detected                                      19
```

**The board published `S20` for all nineteen darts**, because no camera measured a wedge and
#1346's fallback asserts the 20. Two are "correct" only because two 20s were thrown.

| | |
| --- | --- |
| correct | **2** — both the asserted 20 landing on a thrown 20 |
| a different number entirely | **14** |
| a score published where a miss was thrown | **2** |
| undetected | **2** |

**The ring is essentially repaired and the wedge has never worked.** #1485 removed every
`OUTER` — no dart is called a 25 any more — and every published score is a *single*, which is
right for every single thrown. Only the three trebles are wrong, the ~8% residual #1485
deliberately refused to close by moving a constant.

**The shipped mocks in the same run: 4 darts, all at 0.7, zero `by_default`.** There a camera
*is* anchored and the wedge *is* measured. **The fixture every constant in this repository was
fitted against cannot exhibit this fault** (#1478), which is why it survived this long.

So the open faults, in order of what they cost: the **wedge anchor** (#1486, 19 of 19
asserted), **tip detection** (#1492, median 73.6 mm between cameras), and the treble ring's
8% (no issue; deliberately left).
