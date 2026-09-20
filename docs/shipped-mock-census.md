# Which testers make a geometry claim against the shipped mocks

*#1480. A census, and the rule that produced it. Nothing under `testers/` was changed to
write this — see the end of the file.*

`mocks/cam_{1,2,3}.mp4` is upstream OpenDartboard footage, and
[`mocks/rig-20260918/README.md`](../mocks/rig-20260918/README.md) says every calibration
constant in this repository was fitted against it. A tester that asserts a geometric
quantity over that footage is therefore asking whether constants fitted to it still fit
it. A tester that asserts a refusal, a log shape, an exit code or a socket is not: a
refusal path does not care whose board it refuses.

#1478 re-points the first set at `mocks/rig-20260918/` and leaves the second alone. This
file is the split, and how to reproduce it on file 56.

## Counts, re-derived on `9d699de`

| | files |
|---|---|
| `grep -rl 'mocks/cam_' testers/` | **55** |
| `grep -rl 'rig-20260918' testers/` | **32** |
| both | **19** |
| **A** — makes a geometry, calibration or scoring claim | **27** |
| &nbsp;&nbsp;**A1** — carries a shipped-mock literal (needs a rig measurement) | **11** |
| &nbsp;&nbsp;**A2** — shape or presence only (may re-point for free) | **16** |
| **B** — plumbing, refusals, crash paths, log shapes, sockets | **28** |

**Six of the 55 name `mocks/cam_` only in a comment.** They are counted in B below, and
they are not re-pointing work at all — the name is prose in a header, and the claim, if
there is one, lives in the script the file drives. The stripper that separates code from
prose is `census.sh`'s own `od_code`, so this is the repository's existing rule
(#1430: *a file names a tester when the name is in its code; a name in a comment is
prose*) applied to a fixture path instead of a filename.

## The rule

Apply it per **pass/fail condition**, then take the file's verdict as the union.

> **The substitution test.** Replace `mocks/cam_{1,2,3}.mp4` with
> `mocks/rig-20260918/cam_{1,2,3}.mp4` and change nothing else. For each condition that
> decides the file's exit status, ask: *does this condition still hold, without anybody
> measuring anything on the rig?*
>
> - **It names a geometric, calibration or scoring quantity or outcome → A.**
>   A coordinate, a radius, a span, a pixel or contour or ray or wire count, a ratio or
>   share, a carve, a window, a fitted board, a dart or event or window count, a
>   confidence, a score; or an *outcome* of the vision pipeline — "calibrates N of M",
>   "a bull was found", "prints no ERROR and no WARN", "it published a dart".
>   - **A1** if the condition, or a *fixture parameter the condition depends on*, is a
>     literal figure or a named per-clip property read off the shipped footage. It cannot
>     re-point until somebody measures the rig.
>   - **A2** if it names the quantity but pins no shipped-mock figure. It re-points by
>     changing the path, and then has to be re-run to confirm.
> - **It names only something outside the vision pipeline → B.**
>   Exit codes, stdout byte counts, files on disk, spool and cursor contents, credentials,
>   sockets and subscribers, the mDNS announcement, container lifecycle, compile and
>   link outcomes, AddressSanitizer findings, log lines about the run's *lifecycle*
>   ("END OF FOOTAGE", "cycle budget reached", "Initializing 3 cameras").

Three clarifications that decided real rows:

**A fixture parameter counts, not only an expected value.** `1321-reason.sh` asserts
nothing but stage names and self-consistent counts — and builds its input with
`i1362_broken_ring_footage … 612 338 45 430 0,220`, where `(612,338)` is camera 1's board
centre *on the shipped mocks* and `45–430` is an annulus sized against its 291 px radius.
On the rig that board is at `(671,320)` with a radius of 194, so those four numbers are a
measurement that has to be re-taken. Same for `1320-speck.sh`'s `700,380`,
`1392-annulus.sh`'s three crop centres, and `1449-anchoring.sh`'s
`UNANCHORED=cam_1,cam_3,cam_1` — which is only an unanchored board because `cam_2` is the
one star camera *of this fixture*.

**A tool is not its caller.** `i1321_dark_footage.cpp` and `i1362_broken_ring_footage.cpp`
take every geometric figure as `argv`. They are honest against any footage; the script
that passes them shipped-mock numbers is not.

**A file that reads the footage need not be the one that claims anything about it, and a
file that claims something need not read it.** `i1392_look_check.cpp` opens no video at
all — it carries a hard-coded table (`{"mocks/cam_1.mp4", 38532, 32580, 291.0, 97}`) and
is the purest A1 in the census. `i1319_run.sh` starts the container that runs the claims
and names the mocks only in its header, and is B.

### Where it goes fuzzy, stated rather than hidden

**Most of B needs the fixture to calibrate before its own verdict is reachable.**
`phases891/contest.sh` and its three siblings exit on `grep -o "\[i803\].*"` — the
cycle-budget line, printed from inside the scoring loop, which a board that never
calibrates never reaches. `i1334_inside.sh`'s spool phases are the same shape. The rule
puts these in B because the condition *names* a lifecycle line and not a vision quantity,
and because the substitution is safe — the rig README states all three of its cameras
calibrate. But it is a shared precondition rather than a proof, and if the rig ever stops
calibrating 3 of 3, a large part of set B goes red for a reason none of those files
mentions. That is the one place where "honest against any footage" is doing more work
than it has earned.

The line that is genuinely arbitrary is **"prints no ERROR and no WARN"**. It is a
statement about the log, which would argue for B, and on a calibration run its content is
almost entirely geometry, which argues for A. It is placed in A, and it never decides a
file on its own: every file asserting it also asserts "the mocks calibrate, all three" in
the next breath.

## Set A — 27 files

### A1 — carries a shipped-mock literal (11)

A rig measurement is needed before these can be re-pointed. **Nine of the eleven already
carry a rig half** and have measured it; for those, re-pointing is closer to *deleting the
mocks half* than to taking a new number.

| file | why | rig half already there |
|---|---|---|
| `i1323_inside.sh` | bulls `(616,283) (651,313) (654,293)`; off-aim bull `(796,373)`; board middle `(798,424)`; speck refused at `8.x px` | yes |
| `i1331_inside.sh` | the same three bulls; full-frame `radius 291/314/308 px, centre (612,338)/(619,370)/(676,374)`; edge margins `121/148/119 px`; fitted boards `183859 173006 175444`; region `663 px around (619,370)`; off-centre bull `(846,432)`, board `287.7 px`; cut board `176 px, centre (1046,304)` | yes |
| `i1392_look_check.cpp` | a hard-coded table of the six cameras: `mocks/cam_1..3` at flood/ring/span/rays `38532,32580,291.0,97` etc. Reads no video | yes |
| `phases1320/1320-speck.sh` | the same three bulls, pinned; `96 px` between bull and speck | yes |
| `phases1321/1321-reason.sh` | fixture parameters `612 338 45 430 0,220` — camera 1's board on the shipped mocks — and the `0.18` dim scale swept on it | no |
| `phases1339/1339-denominator.sh` | §5 floors the shipped mocks at `MB >= 10` dart events in 1500 cycles; the `0.6` scale and the "the mocks have room, the rig is 1.07x from not calibrating" premise | yes |
| `phases1340/1340-floor.sh` | the three mock bulls before and after; the `OD_BOARD=frame` reproductions of #1320's numbers; `36864` floor; speck `700,380`; `96 px` | yes |
| `phases1392/1392-annulus.sh` | crop centres `616,283 / 651,313 / 654,293` are the mock bulls; per-camera expected ring shares pinned per fixture | yes |
| `phases1393/1393-carve.sh` | carves `27 29 29` on boards `291 315 309`; red carved `364 364 355`; fitted `183859 173006 175444`; half-size bull `(628,322)`, `89` points, `171/354 px`, triples `17976/12503/5319` | yes |
| `phases1394/1394-windows.sh` | fitted `183859 173006 175444`; spans `289/312/306`; bull's-eye `168/182/178`; centrality `459/496/486`; connectivity `140/152/149`; kept `38563/39763/48386` | yes |
| `phases1449/1449-anchoring.sh` | the unanchored board is `cam_1,cam_3,cam_1` *because* `cam_2` is this fixture's only star camera — a measured per-clip property, and the whole construction | no |

### A2 — shape or presence only (16)

No shipped-mock figure is pinned. Change the path and re-run.

| file | the claim |
|---|---|
| `i1319_inside.sh` | §4 control: the mocks calibrate with `0` ERROR/WARN, and `0` negotiation/bandwidth lines on a file source |
| `i1334_inside.sh` | it opens its cameras, and `SCORES >= 1` off the mocks — its own header calls these floors, not fingerprints |
| `phases1317/1317-partial.sh` | §control: the mocks calibrate, all three, `0` ERROR/WARN |
| `phases1318/1318-webcam.sh` | B: the mocks calibrate 3 of 3, `0` ERROR/WARN; A/C/D: the two mock cameras beside the face calibrate; the probe chooses the three mock paths, asserted as literal path strings |
| `phases1330/1330-ownership.sh` | phase 4: the mocks calibrate with `0` ERROR/WARN, write a cache, and a `--reuse-calibration` start reads it |
| `phases1338/1338-partial.sh` | D control: the mocks calibrate 3 of 3 and the scorer repeats the census, `0` ERROR/WARN; A/C build boards out of mock cameras |
| `phases1345/1345-figures.sh` | the mocks calibrate 3 of 3, produce `0` ERROR, and refuse `>= 1` window so a refusal is shown possible |
| `phases1348/1348-reduced.sh` | A control: the mocks calibrate, all three, `0` ERROR/WARN |
| `phases1372/1372-cached.sh` | S seed: the mocks calibrate 3 of 3, `0` ERROR/WARN, and write the cache every later phase reads |
| `phases1388/1388-budget.sh` | phase 6 control: the mocks calibrate, `0` ERROR/WARN. Phase 1's budget and phase 3's `WORST` are both derived in the run |
| `phases1389/1389-floor.sh` | A control: the mocks calibrate, `0` ERROR/WARN; D: a two-camera mock board publishes `>= 1` score |
| `phases1441/1441-region.sh` | §C: every mock clip answers identically with and without `OD_WIRE_REGION=doubles`. Differential against the same binary |
| `phases1442/1442-count.sh` | §B: the three count relationships over every frame of both fixtures, and `TOT_OVER`/`TOT_EXACT` non-zero. Differential |
| `phases1445/1445-looks.sh` | §C: **see the blocker below** |
| `phases1451/1451-scoring.sh` | the closing control: `mocks/cam_*.mp4` still calibrates 3 of 3. Its subject is the rig's 21-wire camera 3 |
| `phases899/899-recover.sh` | §9: the mocks calibrate, `0` ERROR/WARN, and say nothing about sight. The two arms are cut from the mocks by `i1388`'s tool with synthetic `20,15` displacement |

## Set B — 28 files

The footage is a carrier, raw material for a clip the tester mutates itself, or a name in
prose. Nothing here asks what the detector saw.

| file | why |
|---|---|
| `i1249_control.sh` | 1100-cycle carrier run; echoes `PROGRAM_RC` and asserts nothing |
| `i1257_control.sh` | as above |
| `i1258_control.sh` | as above |
| `i1259_control.sh` | as above |
| `i1274_control.sh` | as above; carries an `unrun-tester:` marker |
| `i1276_control.sh` | as above |
| `i1259_pairing_check.py` | pairing codes, credential file, token handling. `CAMS` is a constant |
| `i1276_takeout_check.py` | Contest push protocol. #1374 **removed** the one footage-dependent assertion and says so: which dart is a round's last "is a reading of `mocks/cam_*.mp4`, not anything this case arranges" |
| `i1282_inside.sh` | end-of-footage: `rc != 124`, `rc = 0`, stdout `< 2 MB`, three `END OF FOOTAGE cam=`, one `Initializing 3 cameras`, no `BOARD SIGHT LOST`, `CAPDROP > 0`. The mocks are cut into 300-frame clips |
| `i1282_windows.sh` | the same on real Windows, plus subscriber sockets. `unrun-tester:` |
| `i1319_run.sh` | container harness; exits on the inside script. **Prose only** |
| `i1321_dark_footage.cpp` | parameterised darkening tool, `argv`-driven. **Prose only** |
| `i1362_broken_ring_footage.cpp` | parameterised annular-occlusion tool, `argv`-driven. **Prose only** |
| `i1393_run.sh` | container harness. **Prose only** |
| `i1394_run.sh` | container harness. **Prose only** |
| `i1437_fixture_census.py` | a lint over tester *sources*: a file naming more than one clip of a fixture must name them all. Reads no footage. **Prose only** |
| `phases1247/1188-subscribers.sh` | `check_subscribers.py`'s verdict — sockets and TCP_INFO |
| `phases1247/822-unreachable.sh` | spool, cursor, retry, SIGTERM. A recording phase; its own header says its exit status carries nothing |
| `phases1247/892-control.sh` | beats and the stub transcript. Recording phase; exits on `wc -l` |
| `phases1247/895-blind.sh` | the blind path. Recording phase; exits on `wc -l` |
| `phases1274/1274-announce.sh` | the `.service` announcement file and a real connect to port 13520 |
| `phases1317/1317-asan.sh` | AddressSanitizer findings `= 0` over two inputs, plus a pure capacity probe at 9/15/19 wires. The mocks are `#845`'s control input |
| `phases891/contest.sh` | pairing, Contest binding, credential keys, transcript events. Exits on the cycle-budget line |
| `phases891/givenup.sh` | the evening given up; credential keys; the binding not rejoined after restart |
| `phases891/givenup-nobeat.sh` | as above, with no Casual beat |
| `phases891/horizon.sh` | a hand-planted spool and the delivery horizon |
| `phases891/unreachable.sh` | the spool filled at a blackholed address and drained afterwards |
| `tester_paths.sh` | `od_still()` cuts frame 30 of `cam_1` into a JPEG that stands in for a camera that will not open. A fixture-making helper, not a tester |

## The 19 that read both fixtures

`i1319_inside.sh`, `i1319_run.sh`, `i1323_inside.sh`, `i1331_inside.sh`,
`i1392_look_check.cpp`, `i1437_fixture_census.py`, `phases1317/1317-asan.sh`,
`phases1317/1317-partial.sh`, `phases1320/1320-speck.sh`,
`phases1339/1339-denominator.sh`, `phases1340/1340-floor.sh`,
`phases1345/1345-figures.sh`, `phases1388/1388-budget.sh`,
`phases1392/1392-annulus.sh`, `phases1393/1393-carve.sh`,
`phases1394/1394-windows.sh`, `phases1441/1441-region.sh`,
`phases1445/1445-looks.sh`, `phases1451/1451-scoring.sh`.

For these the question is not "does the file move" but "which assertion inside it moves",
and for the nine A1 members it is mostly **deletion, not measurement** — the rig figure is
already pinned three lines below the mock one. `i1331_inside.sh` is the clearest case: it
pins six bulls, six full-frame boards, six margins and six fitted areas, three of each per
fixture. #1478 removes one column.

## Findings, reported and not repaired

**1. `phases1445/1445-looks.sh` §C cannot simply re-point, and it is the one row that
could turn #1478 from a path change into a design question.** Its control is keyed on the
string `mocks`:

```sh
elif [ "$f" = mocks ] && [ "$OFF" != "$N" ]; then
  say "FAIL the shipped mocks answer $OFF of $N without the retry; …" no
elif [ "$f" != mocks ] && [ "$OFF" = "$N" ]; then
  say "FAIL $f answers for all $N with the retry DISABLED, so the switch changes nothing …" no
```

The shipped mocks must answer **3 of 3 with the retry disabled** and every other fixture
must **not**. That asymmetry is the falsification: without a fixture that calibrates on the
first look, the switch cannot be shown to change anything. `mocks/rig-20260918` is the
fixture on the other side of it by construction — the same section demands it answer
fewer than 3 of 3 without the retry. So this file needs a fixture the census is about to
delete, and re-pointing it at the rig makes its two branches contradict each other.

**2. `phases1449/1449-anchoring.sh` has no rig half and its fixture is a measured
property.** It needs somebody to determine which `mocks/rig-20260918` camera is the star
camera before the unanchored board can be rebuilt. If more than one is, or none is, the
construction has to change rather than move.

**3. `phases1321/1321-reason.sh` has no rig half either**, and its broken-ring annulus is
sized against a 291 px board. The rig's is 194–197 px, so `45 430` is not a scaling
exercise — the sweep that found `0,220` was 60 degrees wide on the mocks and has to be
re-swept.

**4. The `0.6` scale in `phases1339/1339-denominator.sh` exists because the mocks have
room and the rig does not.** Its own header: `rig-20260918`'s largest red/green region is
39,378 px against `bull_processing`'s floor of 36,864, "so that rig is 1.07x from not
calibrating at all and nothing smaller than it can be made from it." This file cannot be
re-pointed at the rig at all in the sense #1478 means — the experiment is *making the
board smaller*, and the rig has no headroom. It needs a third source or a different
experiment.

**5. `phases1441/1441-region.sh`, `phases1442/1442-count.sh` and
`phases1445/1445-looks.sh` enumerate fixtures from the filesystem** —
`FIXTURES="$(ls -d /app/mocks/*/ …) mocks"`. Deleting the shipped mocks silently shrinks
their population rather than failing. `i1437_fixture_census.py` will not catch it: it asks
whether a file naming several clips of a fixture names them all, not whether the fixture
exists.

**6. Nothing in set A was found asserting a value that is already wrong.** Every A1 figure
this census read is stated against a fixture and a build, and where a number has moved the
file says so in its own header (`1320-speck.sh` on the DEBUG_SEEK_VIDEO frame,
`1331-inside.sh` §2.5 on #1378 moving the margin twice). That is a finding in the other
direction and worth recording: the circularity here is not that the numbers are stale, it
is that they are true of one room.

## Nothing was changed

```
$ git status --short
?? docs/shipped-mock-census.md

$ git diff --stat -- testers/
(nothing)
```

No file under `testers/` was modified, re-pointed or repaired. This census is one new
file and no other change.
