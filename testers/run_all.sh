#!/bin/bash
# #1335: one command runs every tester in this repository and says which failed.
#
# Six slices merged on 2026-09-18, each gated only against the tree its own agent held,
# and nothing ran the whole of testers/ on the merged result -- there was no way to. Every
# harness named the worktree it was written in, so running a sibling's tester meant copying
# it to scratch and repointing it by hand, and three agents each did that separately while
# two testers sat red on main. This is the command a merge can be gated on.
#
#   testers/run_all.sh                    build, then every tester
#   testers/run_all.sh 1320 1317          only the testers whose label contains one of these
#   OD_SKIP_BUILD=1 testers/run_all.sh    measure the binary already in build/
#   OD_TESTER_TIMEOUT=1800 …              seconds one tester may take (default 1200), and
#                                         it governs every tester, `slow` ones included
#
# It builds first, and that is not convenience. Every detector tester measures
# build/opendartboard, and the numbers several of them assert belong to the DEV build:
# DEBUG_SEEK_VIDEO seeks a file source three seconds in, so a release binary calibrates on
# a different frame of the same clip and measures a different bull, a different wire count
# and a different camera census. #1319's agent measured both of this issue's "failures" on
# a release build of the same commit this passes on. A gate that does not build the thing
# it measures is gating on whatever was left in the tree.
#
# One tester at a time, deliberately: they run the detector under --cpus=2 with real
# footage and three of them race a clock, so two at once measure each other's load.
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"

T="$OD_TREE_ROOT/testers"
LOGS="$OD_RUNS_BASE/run_all"
TIMEOUT="${OD_TESTER_TIMEOUT:-1200}"

if ! command -v docker > /dev/null; then
  echo "run_all: docker is not on this machine, and every tester but one runs in $OD_IMAGE" >&2
  exit 2
fi
if ! docker image inspect "$OD_IMAGE" > /dev/null 2>&1; then
  echo "run_all: the image $OD_IMAGE is not on this machine; build it with 'docker compose build opendartboard'" >&2
  exit 2
fi

# ---- the testers, in the order a reader would want to see them fail --------------------
# label                       what runs it. Labels are matched as substrings by the
# arguments, so 'run_all.sh 1317' runs both of #1317's.
LABELS=(); CMDS=(); LIMITS=()
tester() { LABELS+=("$1"); LIMITS+=("$TIMEOUT"); shift; CMDS+=("$*"); }

# A tester whose honest cost is more than the default, with the measured number beside it
# (#1341). Before this, 1317-asan -- the memory-safety check, and the most expensive thing
# in this file -- was given the same 1200 s as a tester that runs the detector for forty
# seconds, and on a loaded box it reported `no answer in 1200s`, which is not a finding and
# reads exactly like one. Write the measurement into the comment beside the call: a number
# nobody measured is how the 1200 itself got here. OD_TESTER_TIMEOUT still wins where it is
# set, so a run that wants everything to fail fast still can.
slow() { [ -n "${OD_TESTER_TIMEOUT:-}" ] || LIMITS[$(( ${#LABELS[@]} - 1 ))]="$1"; }

tester address            "bash '$T/check_default_address.sh'"

# #1452: and this one is about release.yml rather than about the detector, so it costs no
# container and no build either. It asks whether a pull request can still reach the Windows
# job and still publish nothing when it does -- both of which can stop being true with no
# diff to the trigger, because GitHub skips a job whose `needs` was skipped and says so in
# grey. It compiles nothing and reads no C++: #1452 rejected a lint for nonstandard
# identifiers on the grounds that a check which looks like a Windows build and is not one
# is worse than none, and this is not that check.
tester 1452-pr-build      "bash '$T/i1452_pr_build_check.sh'"

# The census first, because it is about this list itself and costs no container: a tester
# this file does not name is outside the gate, which is #1335's own shape one level down
# (#1371). Then the pure checks -- a compile and a few milliseconds each, so they are the
# cheapest place for a reader to learn the build is broken.
tester census             "bash '$T/census.sh'"
# Then the leak check, for the same reason and with the same cost: it starts containers but
# every one of them is `sleep`, and what it measures -- that a killed harness leaves nothing
# behind -- is a property of every tester below it (#1341).
tester leaks              "bash '$T/leak_check.sh'"
tester 1346-vote          "bash '$T/unit_check.sh' 1346"
tester 1347-sector        "bash '$T/unit_check.sh' 1347"
tester 1349-background    "bash '$T/unit_check.sh' 1349"
tester 1350-vote-line     "bash '$T/unit_check.sh' 1350"
tester 1351-ledger        "bash '$T/unit_check.sh' 1351"
tester 1363-anchor        "bash '$T/unit_check.sh' 1363"
# #1451: `canScoreAPoint` -- the scorer's own guard, extracted so the startup census and
# `scorePoint` ask one expression -- plus the count and the per-camera naming built on it.
# It carries the ZERO board, which 1451-scoring deliberately does not: no cache this
# repository can write holds three refused rings, because the one shipped camera with a
# long ring produces it in one slot position only (measured; see the check's own header).
tester 1451-scorable      "bash '$T/unit_check.sh' 1451"

# #1477: the sentence --autocams prints about a camera it rejects. autocam::probe() is
# #ifdef _WIN32 and cannot be compiled here, so what this measures is the pure function
# outside that #ifdef which decides the verdict and writes the words -- the same two
# sentences capture.hpp prints at the OPEN site, held to each other so one cannot drift.
tester 1477-probe-format  "bash '$T/unit_check.sh' 1477"

# #1450: the sealed geometry fingerprint, in BOTH spellings on one binary. A harness of
# its own rather than a row in unit_check.sh, because the measurement is a PAIR of runs --
# plain and OD_SEAL=star -- and unit_check.sh compiles and runs a check once. Costs a
# compile and two milliseconds, like the pure checks above it, and it also holds the
# census that keeps the seal unpersisted: the issue's whole stated cost was a board
# reading an old spelling off disk, and there is no disk.
tester 1450-seal          "bash '$T/i1450_seal_check.sh'"

tester 1258-choice        "bash '$T/i1258_check.sh'"
tester 1319-findings      "bash '$T/i1319_run.sh'"
tester 1320-speck         "bash '$T/i1320_run.sh'"
tester 1323-offaim        "bash '$T/i1323_run.sh'"
tester 1321-reason        "bash '$T/i1321_run.sh'"
tester 1318-webcam        "bash '$T/i1318_run.sh'"
tester 1338-partial       "bash '$T/i1338_run.sh'"
tester 1331-framing       "bash '$T/i1331_run.sh'"
tester 1339-denominator   "bash '$T/i1339_run.sh'"
tester 1340-floor         "bash '$T/i1340_run.sh'"
tester 1392-annulus       "bash '$T/i1392_run.sh'"
tester 1393-carve         "bash '$T/i1393_run.sh'"
tester 1394-windows       "bash '$T/i1394_run.sh'"
# #1437: a fixture answers for every clip it holds. It runs the detector once per fixture,
# once more against a fixture with a clip that sees no dartboard, then ten times over held
# frames, and finally calibrates ninety single frames directly. MEASURED 2026-09-20 on the
# 4-core box, to completion, rc=0: wall 284.8 s at host_busy_pct=60.2, so it takes no
# `slow` -- it sits well inside the default 1200 and the number is here rather than in
# nobody's head, which is how 1317-asan's 1200 got to be wrong.
tester 1437-fixture       "bash '$T/i1437_run.sh'"
tester 1441-region        "bash '$T/i1441_run.sh'"
# #1442: twenty is a ceiling as well as a floor. It calibrates the same ninety single
# frames #1437 does, TWICE -- once under OD_WIRE_COUNT=atleast, which is the one-sided
# test every commit before that issue asked, and once as this tree is -- so both censuses
# come off one binary and differ by one comparison and nothing else. Plus a pure check of
# the count, the two guards it decides and a dart, run four ways including one deliberate
# mismatch that must FAIL. MEASURED 2026-09-20 on the 4-core box, to completion, rc=0:
# wall 110 s -- it starts no detector and reads no build/opendartboard, so it takes no
# `slow` and sits well inside the default 1200.
tester 1442-count         "bash '$T/i1442_run.sh'"

# #1423: which ring the calibration stage measured -- the doubles ring or the treble
# ring -- read from the image rather than from a constant fitted to a rig. Its statistic
# is `reach`, the outermost radius carrying colour at the 90th percentile over 720 rays,
# in spans; the two fixtures sit on the two values the millimetres predict (1.0 and
# 170/107) with a factor of 1.61 of clear air between them.
#
# Registered here because it was NOT: #1423 shipped `i1423_run.sh` and no line in this
# file, so the label existed and the suite would never have run it -- a tester that
# cannot be reached is the same as one that cannot fail (#1463). Found by #1467's agent,
# which is stacked on #1423 and read the file for its own registration.
#
# Measured on the 4-core box at load 9.62: 213 seconds.
tester 1423-ringidentity  "bash '$T/i1423_run.sh'"

# #1467: the wire stage fits a twenty-fold model instead of counting to twenty. Six
# sections, the last of which plants a board plane built with no bull in it -- #1466's
# affine unprojection -- and asserts section 1 could not have passed on that tree. It
# builds the census three times (this tree, the counting falsifier's run is the same
# binary, the planted tree) and runs eighteen calibrations per build; it never starts
# the detector binary, so OD_SKIP_BUILD makes no difference to it. Measured on the
# 4-core box at load 9: 269 seconds.
tester 1467-wiremodel     "bash '$T/i1467_run.sh'"
# #1445: a camera refused on one averaged frame is looked at again. Two censuses of the
# calibration stage -- the averaged frame the board really calibrates on, then the single
# frames after it -- which is where the budget's size comes from; then four detector runs
# for the od_fix pair (#1340) and two more that nudge the camera the retry calibrated, to
# show ADR-0080's refusal still fires about it. MEASURED 2026-09-20 on the 4-core box,
# to completion, rc=0: wall 146 s at load 2.1-2.6, so it takes no `slow` and sits well
# inside the default 1200.
tester 1445-looks         "bash '$T/i1445_run.sh'"
tester 1345-figures       "bash '$T/i1345_run.sh'"
tester 1358-window        "bash '$T/i1358_run.sh'"
tester 1355-bounds        "bash '$T/i1355_run.sh'"
tester 1348-quorum        "bash '$T/i1348_run.sh'"
tester 1372-cached        "bash '$T/i1372_run.sh'"
# #1449: whether the board says AT START how many of its cameras can be read for a wedge.
# Four detector runs on shipped footage and no instrumentation -- an unanchored board is
# three slots filled from the two mocks that are not the star camera -- plus a compile of
# the branch point, which is the "before": admitted, READY, and every dart the asserted 20
# with nothing said until after one had been published. MEASURED 2026-09-20 on the 4-core
# box, to completion, rc=0: wall 180.2 s at host_busy_pct=41.2, so it takes no `slow` --
# it sits well inside the default 1200, and the number is here rather than in nobody's
# head, which is how 1317-asan's 1200 got to be wrong.
tester 1449-anchoring     "bash '$T/i1449_run.sh'"
# #1451: whether the board says AT START how many of its cameras a dart can be SCORED
# from. #1449 one field over and worse -- a camera refused by `scorePoint` contributes
# nothing at all rather than an asserted 20, and the refusal is log_debug. Six detector
# runs plus a compile of the branch point, on mocks/rig-20260918, whose camera 3 finds
# twenty-one wire boundaries. The board is built from that fixture's own cache, written by
# a binary with #1442's OD_WIRE_COUNT=atleast set and read by one without it, because the
# cache is the only door into the state: a freshly calibrated camera with a ring that is
# not whole is refused by `calibrateSingleCamera` and never reaches the census.
# MEASURED 2026-09-20 on the 4-core box, to completion, rc=0: see the line below for the
# number, recorded here rather than left in nobody's head (how 1317-asan's 1200 got wrong).
tester 1451-scoring       "bash '$T/i1451_run.sh'"
# #1485: which ring a fitted contour really is, and what radius a dart is therefore
# judged against. On mocks/rig-20260918 the ellipse named the 25 ring was fitted at 0.97,
# 0.61 and 0.34 of the board where the millimetres put it at 0.0935, so eight of nineteen
# darts on that footage were published OUTER -- a score of 25 at the Turnaus door. Six
# sections: the ring census on both fixtures, the departure as a factor, the scoring
# sweep, OD_RINGS=asfitted as the falsifier, two whole-footage detector runs, and a
# planted band that cannot refuse. Sections 1-4 and 6 need no detector binary; section 5
# runs build/opendartboard over the rig footage twice at OD_MAX_CYCLES=6000, which is what
# reaches all seven visits -- 1200 reached four and the truncation was invisible.
tester 1485-rings         "bash '$T/i1485_run.sh'"
# #1489: whether a camera that published a 25 is a camera that measured a wedge. It was:
# the vote split the cameras on `wedge_asserted` alone, so a BULL or an OUTER -- scored by
# the ring ellipses with the angular ruler never asked -- landed in the bucket called
# `measured` and earned 0.7, or 0.9 with a second camera agreeing. On mocks/rig-20260918
# under #1485's OD_RINGS=asfitted that is eight darts of eighteen, in a run where not one
# wedge was measured anywhere, so a geometry change pushing MORE darts into the 25 ring
# read as the anchor improving. Five sections: the vote, the reading on a synthetic board
# anchored and not, OD_RING_ONLY=counted as the falsifier, three whole-footage detector
# runs at OD_MAX_CYCLES=6000, and two planted lines that delete the distinction at source.
# Sections 1-3 and 5 need no detector binary and cost a compile each.
tester 1489-ringonly      "bash '$T/i1489_run.sh'"
# #1490: how far apart two cameras place ONE dart, in millimetres, through #1467's
# planeOf() and through the BoardPosition the scorer already computes, side by side. A
# PROBE: it changes nothing, it asserts no threshold on the spread -- ADR-0084 s4 defers
# that deliberately, and a number chosen from one fixture is #1322 -- and what it does
# assert is its own instrument: three cameras calibrated, a plane on at least two of them,
# and at least one dart placed by two cameras, because a census that compared nothing has
# measured nothing. It needs no detector binary and OD_SKIP_BUILD changes nothing about
# it: it compiles the calibration and detection stages and replays the whole of
# mocks/rig-20260918 through them, to the end of the footage and with no cycle cap.
tester 1490-spread        "bash '$T/i1490_run.sh'"
# #1492: WHAT each camera found on the darts where they disagree most -- the first half
# of that issue is a measurement and not a repair. It splits #1490's census by whether any
# camera placed the dart OFF the board, prints the mechanism census dart_processing keeps
# about its own figure (OTHER-PIECE: the published tip is not a point of the contour the
# centroid was measured from; FLOOR-BOUND: the tip moves when the 400 px contour floor is
# removed), and asserts the two things that are claims rather than constants -- that the
# two populations are disjoint, and that NO off-board reading came out of a whole single
# figure, so no third mechanism is hiding in this fixture. It then measures the obvious
# repair instead of arguing about it: OD_TIP_PIECE_FLOOR=0 on the same binary, which moves
# readings and does NOT reduce the off-board count (#1322). No detector binary, no cycle
# cap, mocks/rig-20260918 only (#1478); two full replays of the clip.
tester 1492-tips          "bash '$T/i1492_run.sh'"
# #1493: whether the three board planes compose into camera poses good enough to
# TRIANGULATE -- rays from two cameras at one dart, intersected, and the residual reported
# in millimetres per dart. #1490's harness with one extra column: the same replay, the same
# stages, the plane printed as a matrix rather than as its two scalars. A PROBE, on the
# same terms -- it changes nothing, asserts no threshold and concludes no architecture
# (#1488 is the decision and it is the maintainer's). Residuals are partitioned by whether
# both cameras put their tip on the board and the counts are said plainly, because #1492
# measured the between-camera tip spread at median 73.6 mm and a residual from a wrong tip
# is not evidence about a pose. Section 1 is the control: the same arithmetic asked about
# three cameras whose poses are known by construction, which is what makes a residual on
# the real rig mean anything. No detector binary; OD_SKIP_BUILD changes nothing.
tester 1493-rays          "bash '$T/i1493_run.sh'"
# #1474: whether the board SENDS how many of its cameras a dart is scored from. #1343
# shipped the server half -- Turnaus stores the census and the marking page draws it -- and
# no board ever posted it, so the feature was live and inert and every board read as
# unknown. Four detector runs on one binary: a board whose cameras never open (the census
# must be ABSENT, never three noughts), the same binary under OD_BEAT_CAMERAS=0 (the
# pre-#1474 body, which is both the falsifier and the fleet mid-upgrade), the shipped mocks
# (scoring equals fitted) and mocks/rig-20260918 (fewer scoring than fitted, because #1442
# refuses cameras 1 and 3). Every assertion reads the BODY that arrived at the stub, which
# models App\Autoscoring\CameraReport to the comparison -- a 422 there would cost the club
# the board's condition as well as its count.
# MEASURED 2026-09-20 on the 4-core box: see the recorded line in the pull request.
tester 1474-beat-census   "bash '$T/i1474_run.sh'"
# #1484: HOW a run's darts were scored, not only what they scored. chooseScore has
# published the distinction since #1346 -- 0.9 two or more cameras measured a wedge and
# agreed, 0.7 measured with no two agreeing, 0.5 no camera measured a wedge at all -- and
# nothing read it, so a wrong score and no anchor at all were indistinguishable from
# outside. Two detector runs on one binary: mocks/rig-20260918 to the END OF ITS FOOTAGE,
# which is the only footage here whose real darts are recorded and so the only one the
# accuracy half can be asked of, and the shipped mocks under a cycle budget, whose figures
# carry #1478's caveat and whose run is where the truncation notice is proved to fire.
# It asserts NOTHING about the numbers (#1322) and fails on a run it could not read.
# MEASURED 2026-09-21 on the 4-core box, to completion, rc=0: wall 112.6 s at
# host_busy_pct=71.5 and load_at_end=7.05 -- a CONTENDED box, with another agent's tester
# container up alongside it throughout. So it takes no `slow`: it sits well inside the
# default 1200 even there, and the number is here rather than in nobody's head, which is
# how 1317-asan's 1200 got to be wrong.
tester 1484-confidence    "bash '$T/i1484_run.sh'"
tester 1389-floor         "bash '$T/i1389_run.sh'"
tester 1317-partial       "bash '$T/i1317_run.sh'"
tester 1330-ownership     "bash '$T/i1330_run.sh'"
tester 899-recover        "bash '$T/i899_run.sh'"
# Six labels ran something and threw its verdict away until #1479 -- the four immediately
# below, plus 1276-control and 1257-control further down. The five *-control.sh scripts
# ended on `echo "PROGRAM_RC=$?"` and the announce phase on `echo "PHASES_DONE"`, and an
# echo returns 0 whatever it printed, so not one of them could go red however the detector
# died. That is #1412's defect in the directories #1412 did not sweep. Each now exits on
# what it measured and says in its own first lines what that status carries: the detector's
# own for a control, which asserts nothing about what was scored; the count of #1274's own
# three musts for the announce phase, which already stated them in prose.
tester 1274-announce      "bash '$T/i1274_run.sh' announce '$T/phases1274/1274-announce.sh'"
# #1295 is the other half of #1274's rule -- an announcement must not precede the socket --
# reached by the other cause: a board that CAN see and whose listen() fails anyway. It runs
# beside #1274's because the two share a path and a harness shape, and it is the cheaper of
# the two: two starts rather than three, and no blind-camera phase.
# MEASURED 2026-09-20 on the 4-core box: see the pull request for the wall time and the load.
tester 1295-socket        "bash '$T/i1295_run.sh'"
# #1473 is the case #1295 turned out not to cover: not another program on 13520 but
# another BOARD, which httplib's SO_REUSEPORT lets bind alongside the first with nothing
# failing and nothing logged. It runs beside #1295's because its positive control IS
# #1295's refusal -- the two decisions share one path and must not drift apart -- and it
# starts up to two detectors at once, which is why it is the hungrier of the two.
# MEASURED 2026-09-20 on the 4-core box: see the pull request for the wall time and the load.
tester 1473-lock          "bash '$T/i1473_run.sh'"
tester 1249-control       "bash '$T/i1249_run.sh' '$T/i1249_control.sh'"
tester 1258-control       "bash '$T/i1258_run.sh' '$T/i1258_control.sh'"
tester 1259-control       "bash '$T/i1259_run.sh' '$T/i1259_control.sh'"
tester 1259-pairing       "bash '$T/i1259_check.sh'"
tester 1305-update       "bash '$T/i1305_run.sh'"
tester 1305-manifest     "bash '$T/i1305_check.sh'"
# #1408 is the writing half of the same path and needs no container and no build: openssl,
# a throwaway key and the script release.yml really calls. It sits here beside the reading
# half rather than with the pure checks, because what it is about is the pair.
tester 1408-signing      "bash '$T/i1408_sign_check.sh'"
tester 1276-control       "bash '$T/i1276_run.sh' '$T/i1276_control.sh'"
tester 1276-takeout       "bash '$T/i1276_check.sh'"
tester 1366-position      "bash '$T/i1366_run.sh'"
tester 1257-control       "bash '$T/i1257_run.sh' control '$T/i1257_control.sh'"
tester 1257-resolution    "bash '$T/i1257_run.sh' resolution '$T/i1257_resolution.sh'"
# Of the five phases under phases1247/, one computes a verdict and four record (#1412).
# 1188-subscribers runs check_subscribers.py, which exits 0 when every check held and 1
# when one did not -- until #1412 the phase script ended on `echo "CHECK_RC=$?"`, so the
# container exited 0 whatever the check said and this label could not go red. The other
# four run the detector through a situation and leave a transcript for a reader; they
# assert nothing, so a PASS beside them means "it ran to the end" and not "it held". Each
# says so in its own first lines, including what its exit status does and does not carry.
tester 1188-subscribers   "bash '$T/i1247_run.sh' subscribers '$T/phases1247/1188-subscribers.sh' /runs"
tester 822-unreachable    "NET=bridge bash '$T/i1247_run.sh' unreachable '$T/phases1247/822-unreachable.sh' /run822"
tester 892-control        "bash '$T/i1247_run.sh' control '$T/phases1247/892-control.sh' /run892"
tester 895-blind          "bash '$T/i1247_run.sh' blind '$T/phases1247/895-blind.sh' /run895"
tester 895-dark           "bash '$T/i1247_run.sh' dark '$T/phases1247/895-dark.sh' /run895"
tester 891-contest        "bash '$T/i891_run.sh' contest '$T/phases891/contest.sh'"
tester 891-horizon        "bash '$T/i891_run.sh' horizon '$T/phases891/horizon.sh'"
tester 891-givenup        "bash '$T/i891_run.sh' givenup '$T/phases891/givenup.sh'"
tester 891-givenup-nobeat "bash '$T/i891_run.sh' givenup-nobeat '$T/phases891/givenup-nobeat.sh'"
tester 891-unreachable    "bash '$T/i891_run.sh' unreachable '$T/phases891/unreachable.sh'"
tester 1282-footage       "bash '$T/i1282_run.sh'"
tester 1303-launcher      "bash '$T/i1303_check.sh'"
tester 1306-install      "bash '$T/i1306_check.sh'"
tester 1334-networkless   "bash '$T/i1334_run.sh'"
# #1383: a blind board given a cycle budget ends, and a blind board says so where a
# supervisor can read it. Six detector runs on #892's blind fixture and the shipped
# mocks, each half measured against the SAME binary with OD_BLIND_RUN=unbounded -- what
# this tree did before -- so no phase is a claim about a build. It asserts #895's vigil
# is intact first and ends a blind run only where one was handed a number. MEASURED
# 2026-09-20 on the 4-core box: see the label's own line; it needs no `slow`.
tester 1383-blind-end     "bash '$T/i1383_run.sh'"
# #1334's other half measures what systemd does with templates/*.service.template, so it
# needs a live manager and a live journal where everything here runs in a container with
# no init. It is deliberately outside this list and carries a marker saying so.
# #1388: added at the end rather than beside 899-recover, which is the lifecycle it
# extends, because it builds a second binary from a scratch copy of the tree and takes
# several minutes -- 1317-asan's reason, and it belongs in 1317-asan's half of the list.
tester 1388-budget        "bash '$T/i1388_run.sh'"
# Last, because it builds a second binary under AddressSanitizer and takes longer than
# everything above it together.
tester 1317-asan          "bash '$T/i1317_run.sh' '$T/phases1317/1317-asan.sh'"
# MEASURED 2026-09-19 on the 4-core box, to completion, rc=0: wall 601 s with
# host_busy_pct=97.0 and the load average going 3.2 -> 12.9 under it, which is to say the
# 601 s is already a busy-box number and not a quiet-box one. Two ASan builds is most of
# it: this tree's, and the base commit's for the half of #1317's question that asks what
# the OLD build did on the same footage.
#
# 2400 rather than 1200 because 1200 is the number that failed. The baseline of 2026-09-18
# reported `no answer in 1200s` -- the only red of thirty, and not a finding -- so the
# honest budget is twice the limit that was actually exceeded rather than twice the cost
# measured here. Raising it does not make a HUNG run cheap, and it is not meant to: a
# timeout bounds a run that is stuck, and this one only ever bounded a run that was slow.
slow 2400

# ---- which of them this run is about ---------------------------------------------------
WANTED=("$@")
chosen() {
  [ ${#WANTED[@]} -eq 0 ] && return 0
  local w
  for w in "${WANTED[@]}"; do
    case "$1" in *"$w"*) return 0 ;; esac
  done
  return 1
}

PICK=()
for i in "${!LABELS[@]}"; do
  chosen "${LABELS[$i]}" && PICK+=("$i")
done
if [ ${#PICK[@]} -eq 0 ]; then
  echo "run_all: nothing matches ${WANTED[*]}; the labels are: ${LABELS[*]}" >&2
  exit 2
fi

mkdir -p "$LOGS"
echo "tree:    $OD_TREE_ROOT"
echo "runs:    $OD_RUNS_BASE"
echo "logs:    $LOGS"
echo "testers: ${#PICK[@]} of ${#LABELS[@]}"

# ---- the binary every tester below measures --------------------------------------------
if [ "${OD_SKIP_BUILD:-0}" = "1" ]; then
  echo "build:   skipped (OD_SKIP_BUILD=1); measuring whatever is in build/"
else
  echo "build:   $OD_TREE_ROOT/build/opendartboard, with the dev defines"
  if ! od_run build --cpus=4 -e HOME=/root \
      -v "$OD_TREE_ROOT":/app -w /app "$OD_IMAGE" bash -c '
        cmake -S /app -B /app/build -DCMAKE_PREFIX_PATH=/usr/local \
          -DCMAKE_CXX_FLAGS="-DDEBUG_SEEK_VIDEO -DDEBUG_VIA_VIDEO_INPUT" \
          -DAPP_VERSION=0.0.0-dev &&
        cmake --build /app/build -- -j4 --no-print-directory' > "$LOGS/build.log" 2>&1; then
    tail -20 "$LOGS/build.log"
    echo "run_all: the build failed; nothing below it would be measuring this tree" >&2
    exit 2
  fi
fi
echo

# #1341: run_all is the last thing still alive when a tester is killed outright, so it
# sweeps this tree's containers on the way out, whichever way it goes out. Installed here
# rather than at the top because the build above runs under od_run's own traps.
trap 'od_sweep; trap - TERM; trap - EXIT; kill -TERM $$' TERM
trap 'od_sweep; trap - INT;  trap - EXIT; kill -INT  $$' INT
trap 'od_sweep' EXIT

FAILED=()
PASSED=0
SUITE0=$(date +%s)
for i in "${PICK[@]}"; do
  label="${LABELS[$i]}"
  log="$LOGS/$label.log"
  printf '%-22s ' "$label"
  t0=$(date +%s)
  timeout "${LIMITS[$i]}" bash -c "${CMDS[$i]}" > "$log" 2>&1
  rc=$?
  t1=$(date +%s)
  took=$((t1 - t0))
  # #1341: a harness reaps its own container however it dies short of SIGKILL, and this is
  # what catches the SIGKILL. It is also the only place a leak is REPORTED rather than
  # merely prevented, which matters: a container quietly reaped is a fault nobody learns
  # about, and this one was met twice by hand before anybody wrote it down.
  leaked="$(od_sweep)"
  if [ "$rc" = 0 ]; then
    printf 'PASS  %4ds\n' "$took"
    PASSED=$((PASSED + 1))
  else
    if [ "$rc" = 124 ]; then
      printf 'FAIL  %4ds  (no answer in %ss)  %s\n' "$took" "${LIMITS[$i]}" "$log"
    else
      printf 'FAIL  %4ds  (rc=%s)  %s\n' "$took" "$rc" "$log"
    fi
    # The tester said why, in its own words. Show the first few so the gate is readable
    # without opening a file.
    grep -E '^FAIL |^\[ERROR\]' "$log" | head -4 | sed 's/^/                       | /'
    FAILED+=("$label")
  fi
  if [ -n "$leaked" ]; then
    printf '%s\n' "$leaked" >> "$log"
    printf '%s\n' "$leaked" | sed 's/^/                       | /'
  fi
done
SUITE1=$(date +%s)

echo
echo "$PASSED passed, ${#FAILED[@]} failed in $(( (SUITE1 - SUITE0) / 60 ))m$(( (SUITE1 - SUITE0) % 60 ))s"
if [ ${#FAILED[@]} -ne 0 ]; then
  echo "FAILED: ${FAILED[*]}"
  exit 1
fi
echo "every tester green"
