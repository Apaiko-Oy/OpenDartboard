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
**detection** failure and is a different finding from a wrong score -- unless the throw
made no event at all, which the table under *Throws that produce no event* records.

**Within a visit, do not align the first published against the first thrown.** This file
said to, and it is right only when every throw produces a publication: one throw that
produces none -- a dart off the board, a dart landing as the clip ends -- shifts every dart
after it in its visit, and each reads as wrong (#1504). Visit 6 is the case: `S7`, `S2`
published for `miss, 7, 2` read *nought* correct first-against-first and two correct once
the off-board miss is read as a gap.

The rule `1484-confidence` uses instead, and why it is the whole of what can be defended:

- Publications keep the order of the throws, so only order-preserving alignments exist.
  As many published as thrown has exactly one -- first against first -- and it stands.
- **Nothing joins a publication to a throw by time.** A `SCORE:` line has no timestamp,
  frame or stream position, and this file records no time per throw (#1504 measured both).
  #1511's annotations do carry a frame per throw, but with nothing on the run's side to
  join it to, it cannot place a publication either. So when fewer were published
  than thrown, nothing in a run says *which* throw has no publication, and the placement
  that scores best is exactly what must not be chosen -- it would flatter any detector.
- A gap is placed only where the table below records a throw that made no event,
  read from the footage and not from any run. Otherwise every order-preserving placement
  is reported and a dart whose verdict differs between them is **ambiguous**, counted
  neither right nor wrong.
- More published than thrown is a phantom or a merged visit (#1552) and is not placed by
  this rule.

Visit 4 is the control: three throws, three publications, the last an `S20` for the miss.
Nothing here moves it, and a rule that did would be a rule that flatters.

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

## Throws that produce no event

Both read from the footage by the maintainer on 2026-09-21 (#1500's closing comment), so
neither is a detection failure, and neither was known from any run. #1512 calls the same
fact a throw with *no arrival* — true of visit 7's throw, and, since the correction below,
not of visit 6's: #1511's hand annotations carry visit 6 dart 1 as an arrival
(`testers/i1511_annotations/README.md`). A row here is what lets the census place a gap in
a visit; it is a fact about the recording, and a run that publishes for one of these
throws makes the row place nothing.

| visit | throw | what the footage shows |
| --- | --- | --- |
| 6 | 1 | thrown wide of the scoring area: it sticks in the board's outer black number ring, so nothing scores — corrected below |
| 7 | 3 | lands as the clip ends: the footage stops before the dart can settle |

**Visit 6's first dart is on the recording, and this table said it was not (corrected
2026-09-25).** The cell above read "thrown outside the board: there is no landing on the
board to detect". #1587 measured otherwise: the dart arrives at frames 1490-1495, all
three cameras see it, and it comes to rest in the black number ring outside the double.
Both halves are true at once — it scores nothing, which is why the throws table says
`miss` and why the row stays in this table at all, and it is a real arrival a detector may
legitimately see and must publish as `MISS`, the way this fixture's visit-4 miss is
treated. It is annotated as an arrival in `testers/i1511_annotations/` (#1603). This is
the identical claim already corrected for `rig-20260922`'s v1.3 in b3528e5. The only throw
of this recording that cannot arrive at all is visit 7's third.

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

## Measurement on merged `main` (`04d1902`), 2026-09-21 — **superseded on 2026-09-23**

**This section is a record of the tree it was measured on, not of today's.** It was taken
the day before the wedge anchors landed (#1486, #1497, #1498), when no camera could measure
a wedge and #1346's fallback asserted `S20` for every dart. It stood under the heading
"definitive" for two days after the anchors made it false, which is exactly the reader this
file exists to protect being misled by it (#1515). The definitive section is now the dated
one below.

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

## Definitive measurement on `main` at `f529ecf`, 2026-09-23

Taken for #1515 by `1484-confidence` after a rebuild, whole clip, no cycle cap — the same
instrument as the superseded section above, on a tree carrying the wedge anchors (#1486,
#1497, #1498) and the treble ray trace with its doubles-marks hold (#1499). It reproduces
what #1499's gate measured on `8d0ca0b` to the dart. This file is a truth record about the
recording, not the recording: correcting it is what it exists for, and nothing in
`cam_*.mp4` moves.

```
0.9  two or more cameras MEASURED a wedge and agreed      9
0.7  measured, but no two agreed                          9
0.5  by_default: NO camera measured a wedge               0
     darts detected                                      18
```

**18 darts detected of 21 thrown. 13 of the 18 scored correctly, and two of the three
trebles come back as trebles.** Not one dart publishes `by_default`, not one publishes
`OUTER`: the asserted-20 fallback and the 25 ring — the two faults the sections above
record — are both gone from this clip.

| visit | detected vs thrown, aligned from visit 1 |
| --- | --- |
| 1 | S13 vs **T13** [ring] · S19 [correct] · S13 [correct] |
| 2 | S7 vs 19 [wedge] · **T14 [correct]** · S5 [correct] |
| 3 | S10 [correct] · S7 [correct] · **T20 [correct]** |
| 4 | S7 vs 19 [wedge] · S20 [correct] · *miss* undetected |
| 5 | S15 [correct] · S4 [correct] · S18 [correct] |
| 6 | S7 vs *miss* [on-board] · S2 vs 7 [wedge] · 2 undetected |
| 7 | S5 [correct] · S20 [correct] · 20 undetected |

| | |
| --- | --- |
| correct | **13** |
| a different number entirely | **3** — S19→S7 twice, S7→S2 |
| right number, wrong ring | **1** — T13→S13, a tip measured 5.5 mm short of the band: #1492's fault, not the ring's |
| a score published where a miss was thrown | **1** — visit 6's miss reads S7 |
| undetected | **3** — visit 4's miss, visit 6's 2, visit 7's 20 |

So the open faults on this fixture, in order of what they cost: **detection** (3 of 21
never seen, and the two thrown misses produce one phantom score and one silence), **the
wedge** (3 of 18 a different number entirely), and **tip radius** (#1492, the one ring
error). The wedge anchor and the 25 ring are repaired and measured so, and the treble
ring reads trebles.

**Re-scored under #1504's alignment, 2026-09-24 -- the same published darts, not a new
run.** The table above aligns first-published against first-thrown, which this file used
to prescribe and #1504 corrected (see *How to use it*). Read with the recorded eventless
throws as gaps and nothing else chosen, the same eighteen publications give:

| visit | aligned by #1504's rule |
| --- | --- |
| 4 | S7 vs 19 or 20 [wedge either way] · S20 **ambiguous** -- vs 20 [correct] or vs *miss* [on-board] · one throw undetected, and nothing in the run says which |
| 6 | *miss* [no event, recorded] · S7 [correct] · S2 [correct] |
| 7 | S5 [correct] · S20 [correct] · 20 [no event, recorded] |

Visits 1, 2, 3 and 5 are unchanged. So **14 of the 18 are correct and one is ambiguous
(14..15), where this section said 13**: visit 6 moves from nought to two, and visit 4's
S20 stops counting as correct because the miss and the 20 cannot be told apart once one
of them is missing. *A different number entirely* falls from 3 to 2 (S7->S2 was the
shift); *a score published where a miss was thrown* falls from 1 to 0; and of the three
*undetected*, two are the recorded eventless throws and one is visit 4's, unplaced.

**Measured fresh under the rule on 2026-09-25** by `1484-confidence` on #1504's merge of
`main` at `4e6c103`, whole clip: **19 detected, 15 of 19 correct, none ambiguous, no
visit boundary merged.** Visits 3, 5, 6 and 7 are wholly correct; visit 4 publishes all
three and its third, `T20` for the miss, stays wrong. The same log aligned
first-against-first reads 13 of 19, visit 6 nought.

**Visit 6's second detected dart is marginal.** Three runs of the byte-identical binary
on 2026-09-23 read this clip 18–18–17 darts: the odd run lost exactly that dart — the
throw at 7 that publishes as `S2@0.7` above — and nothing else, every other verdict
identical. A census differing from this table by that one dart is run-to-run detection
variance, not a regression; chase it only if it stays missing on a quiet box.
