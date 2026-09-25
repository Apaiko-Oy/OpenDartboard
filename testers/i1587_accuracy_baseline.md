# #1587: the ACCURACY baseline, and what visit 6 of rig-20260918 really is

`testers/i1555_census.py` prints one `ACCURACY` line per fixture and window, and pooled
(`bash testers/run_all.sh 1555`). It is **correct over every annotated arrival**, with the
96% target and each row's shortfall beside it. Later slices compare against the lines
below.

## Baseline

The detector is `main` at **`b3528e5`**: this branch changes nothing under `src/`. Two
whole runs of the harness, and they agree to the dart:

- the supervisor's run on the fork, `wall_s=408.6 host_busy_pct=33.8 load_at_end=1.70`,
  re-scored by this census from its saved logs;
- this branch's own run (`adb4999`), `wall_s=502.4 host_busy_pct=65.7 load_at_end=6.83`.
  The box was shared with #1586's testers throughout.

```
rig-20260918 dev      correct 15..16/19 (78.9%) | target 96% = 19/19, short 4 | wrong-score 2, undetected 1, off-board-scored 0, ambiguous 1 | phantoms 2 (2 scoring)
rig-20260918 opening  correct 16/19     (84.2%) | target 96% = 19/19, short 3 | wrong-score 2, undetected 1, off-board-scored 0, ambiguous 0 | phantoms 1 (1 scoring)
rig-20260922 dev      correct 15/23     (65.2%) | target 96% = 23/23, short 8 | wrong-score 4, undetected 4, off-board-scored 0, ambiguous 0 | phantoms 0 (0 scoring)
rig-20260922 opening  correct 16/23     (69.6%) | target 96% = 23/23, short 7 | wrong-score 4, undetected 3, off-board-scored 0, ambiguous 0 | phantoms 3 (2 scoring)
POOLED                correct 62..63/84 (73.8%) | target 96% = 81/84, short 19 | wrong-score 12, undetected 9, off-board-scored 0, ambiguous 1 | phantoms 6 (5 scoring)
```

The per-dart names are the `I1555 ACCURACY-DART` lines in the run's `out.txt`. **Four of the
non-correct darts above are the reference's error, not the detector's** (next section): v6.2
UNDETECTED and v6.3 WRONG-SCORE, in both rig-18 windows.

The one AMBIGUOUS dart is rig-18 dev's v4.3, the off-board miss. The matcher placed no
publication on it. The board published `T7` in that visit, and the matcher claimed that
publication for no arrival either (cost 156.9 against v4.3, over the 150 cap: camera 1
fits the miss at 11.9 px, and camera 3's axis lies 7.8 px from dart 4.2's line). So v4.3
is correct if `T7` is a phantom and off-board-scored if `T7` is its publication. Nothing
in the run says which, and #1504 forbids choosing.

## Visit 6 of rig-20260918: the annotation is a dart out, and the board is right

**Hypothesis in the issue:** a census pairing error. **Finding:** neither the matcher nor the
detector is at fault. The **annotation rows for visit 6 name the wrong darts**, and the
matcher paired correctly against the reference it was given.

**Three darts arrive in visit 6, and all three rest in view.** The changed-pixel trace
(`i1587_shaft_fit.cpp trace`, every 5th frame against f1470) finds the same three arrivals
on all three cameras:

| arrival | frame | cam 1 | cam 2 | cam 3 | where it rests |
| --- | --- | --- | --- | --- | --- |
| C, first | f1490-1495 | right rim | above top rim | left, beside the 11 | black number ring **outside the double**, i.e. off the scoring area |
| B, second | f1530-1535 | upper right | top, near the 7 | middle-left | the **7** |
| A, third | f1565-1570 | top | left | right | the **2** |

Truth is `miss, 7, 2`, in that order. C, B and A are that sequence exactly, with no gap.
The detector published `S7` at camera 3 (489,457), which is B's entry, and `S2` at camera 3
(936,435), which is A's. **Both publications are correct.** It published nothing for C,
which is correct for an off-board dart.

**What the annotation says instead.** It assumes v6.1 "never rests in any camera's view"
and files the first two darts that do land as v6.2 and v6.3. Re-fitted by the arrival mask,
every existing row agrees with one dart to within 5 px:

- **v6.2 "7"**, cameras 1, 2 and 3, is **C**: the miss.
- **v6.3 "2"**, cameras 2 and 3, is **B**: the 7.
- **v6.3 "2"**, camera 1, is **A**: the 2. It is the only row in the visit that is right.
- **A on cameras 2 and 3, and B on camera 1, have no rows at all.**

So v6.3's reference is two different darts. The detection that published `S7` (B) lies
32 px from v6.3's camera-3 entry, because that row is B, and it matches there. The
detection that published `S2` (A) has nothing to match on cameras 2 and 3 and is left
UNCLAIMED. That is the whole of `PAIR v6.3 thrown=S2 | vote=S7` plus `UNCLAIMED v6#2 S2`.

**The same error sits in the ground truth.** `mocks/rig-20260918/GROUND-TRUTH.md`'s
*Throws that produce no event* table says v6.1 was "thrown outside the board: there is no
landing on the board to detect". The dart sticks in the board's outer black ring, the
way rig-20260922's v1.3 does, and b3528e5 corrected that one. Correcting either file is
for the maintainer: nothing under `mocks/` is modified here, and the annotation CSV is
left as it stands.

**The rows a correction would use**, fitted with `i1587_shaft_fit.cpp fit` (before = the
arrival -12, after = the arrival +29, clipped to exclude the next arrival):

```
v6.1 miss  cam1: existing v6.2 cam1 row  (fit 1054,200 -> 1004,508, max residual 3.48 px)
v6.1 miss  cam2: existing v6.2 cam2 row  (fit  691,1   ->  696,130, 3.99 px)
v6.1 miss  cam3: existing v6.2 cam3 row  (fit  290,107 ->  321,327, 3.84 px)
v6.2 7     cam1: NEW  1032,66  -> 953,319  tip 953,319   f1562 vs f1521, 136/158 rows, 3.88 px
v6.2 7     cam2: existing v6.3 cam2 row  (fit  510,16  ->  549,167, 3.16 px)
v6.2 7     cam3: existing v6.3 cam3 row  (fit  428,205 ->  482,453, 4.66 px)
v6.3 2     cam1: existing v6.3 cam1 row  (fit  728,41  ->  715,189, 3.77 px)
v6.3 2     cam2: NEW  273,154  -> 333,311  tip 333,311   f1597 vs f1556, 136/175 rows, 3.91 px
v6.3 2     cam3: NEW  1032,180 -> 934,429  tip 934,429   f1597 vs f1556, 133/192 rows, 3.80 px
```

**Measured on a scratch copy with those rows and nothing else changed**, over the same saved
logs: the only pairings that move on either fixture are visit 6's. `PAIR v6.2 thrown=S7 |
published=S7 exact` and `PAIR v6.3 thrown=S2 | published=S2 exact` appear in both rig-18
windows, `UNCLAIMED v6#2` goes, and every rig-22 PAIR and UNCLAIMED line is byte-identical.
rig-18 would read 18..19/20 (dev) and 19/20 (opening). Pooled would read **68..69/86
(79.1%), short 15**. The denominator gains v6.1, now an arrival, so it is 86 rather than 84.
