set -u
# #1442: twenty is a ceiling as well as a floor, and the numbers that say so.
#
# The repair is one comparison -- `wiresDetected == kWiresRequired` where three files each
# asked a store bounded at twenty whether it was short -- and a comparison is exactly the
# kind of thing a later slice loosens back without anything going red. #1437's fixture
# tester cannot see it: it asks whether a clip ANSWERS in the calibration window, and
# before this issue an over-counting frame answered. #1441's tester cannot see it either:
# it holds each clip's refusals against the same binary's own `doubles` run, and the region
# is not what moves here.
#
#   A  THE PURE HALF, four ways. The count, both guards and a dart, with no footage: a
#      calibration filled the way processWires fills one, twenty-two proposed and twenty
#      stored. It is run in each mode saying truly what it expects, once with a typo'd
#      value that must read two-sided, and once MISMATCHED -- which must FAIL. #1340's
#      rule: a switch that only ever refuses passes any test that asks it to refuse, and a
#      check never shown to fail proves nothing.
#
#   B  THE CENSUS and C ITS VACUITY CHECK -- RETIRED by #1658, 2026-09-28; the reason is
#      where they stood, below.
#
# No detector process is started and so nothing needs bounding by a pid or a timeout: this
# compiles the stage's own sources into a pure check (#895). Since #1658 it no longer
# builds #1437's census program at all.
UNIT=/run1442/count_check
FAILED=0
say() { echo "$1"; [ "$2" = ok ] || FAILED=1; }

echo "=== building: the pure check ==="
# wire_model.cpp is named here because score_processing.cpp reaches it at LINK time:
# since the geometric path landed (#1510 board_model, #1512 entry_intersection, #1555
# wiring them into the publish decision), score_processing.cpp pulls in both headers
# and they call wire_model::planeOf, imageOfBoardAngle, fitTwentyFold, coherenceAtFold
# and minimumCoherence. A hand-rolled link line naming score_processing.cpp must name
# wire_model.cpp too, or ld answers "undefined reference" and the row measures nothing.
g++ -std=c++17 -O1 -o "$UNIT" /app/testers/i1442_count_check.cpp \
  /app/src/detector/geometry/calibration/wire_processing.cpp \
  /app/src/detector/geometry/calibration/wire_model.cpp \
  /app/src/detector/geometry/calibration/perspective_processing.cpp \
  /app/src/detector/geometry/detection/score_processing.cpp \
  -I/app/src -I/app/src/utils \
  -I/app/src/detector/geometry/calibration -I/app/src/detector/geometry/detection \
  $(pkg-config --cflags --libs opencv4) -lpthread \
  > /run1442/build_unit.log 2>&1 || { echo "FAIL could not build the pure check; nothing below measures anything"; exit 2; }

echo
echo "=== A. the count, both guards and a dart -- four ways ==="
"$UNIT" two-sided > /run1442/unit_two_sided.txt 2>&1
A1=$?
OD_WIRE_COUNT=atleast "$UNIT" at-least > /run1442/unit_at_least.txt 2>&1
A2=$?
OD_WIRE_COUNT=banana "$UNIT" two-sided > /run1442/unit_typo.txt 2>&1
A3=$?
OD_WIRE_COUNT=atleast "$UNIT" two-sided > /run1442/unit_mismatch.txt 2>&1
A4=$?
sed 's/^/  /' /run1442/unit_two_sided.txt
echo "  --- and with OD_WIRE_COUNT=atleast, the lines that move ---"
grep -E 'read .yes|whole=yes|intersections=yes|different number' /run1442/unit_at_least.txt | sed 's/^/  /'
echo "  two-sided=$A1  atleast=$A2  a typo'd value=$A3  MISMATCHED=$A4 (this one must be non-zero)"
if [ "$A1" != 0 ] || [ "$A2" != 0 ]; then
  say "FAIL the pure check does not hold in one of its two modes (two-sided=$A1, atleast=$A2)" no
elif [ "$A3" != 0 ]; then
  say "FAIL OD_WIRE_COUNT=banana was obeyed as something; a value naming no test must be ignored" no
elif [ "$A4" = 0 ]; then
  say "FAIL the check passes when told to expect the wrong mode, so it cannot fail and proves nothing" no
else
  say "OK   both modes hold, a typo reads two-sided, and the check really fails when the mode is not what it was told" ok
fi

# === B and C, RETIRED by #1658 (maintainer's decision, 2026-09-28) ===
# B re-took #1437's wire census -- both fixtures, all six clips, the fifteen-frame band --
# under OD_WIRE_COUNT=atleast and as the tree is, and asserted three relationships over the
# frames: a frame proposing MORE than twenty is accepted under the old test and refused
# under the new, one proposing exactly twenty passes both, one proposing fewer is refused
# by both. C failed the tester if the first of those populations was empty, because then B
# was vacuously true.
#
# It is empty. MEASURED on fork 47ea89d (plus #1632's tester-only commits): C printed
# `frames proposing more than twenty: 0 of 135` -- 113 frames proposed exactly 20 and 22
# fewer, and rig-20260918 gave 15 of 15 at exactly 20 on all three clips. So C did its job
# and the footage no longer reaches the case. INFERRED, not bisected: #1467's twenty-fold
# wire model fits twenty rather than counting boundaries, so an over-count may no longer be
# producible from real footage at all. The decision was to retire rather than plant a
# synthetic frame: the calibration work (#1445, #1456, #1467, #1631) removed the case from
# real footage, and a planted frame would guard a scenario the rig no longer reaches.
#
# What still proves the two-sided test is A: it plants 21, 22 and 27 with no footage, and
# its MISMATCHED run must fail. The census program is no longer built here, which is where
# almost all of this tester's cost was (#1632: 270 calibrations, wall 1549 s).

echo
echo "=== the verdict ==="
if [ "$FAILED" = 0 ]; then echo "1442-count: PASS"; else echo "1442-count: FAIL"; fi
exit $FAILED
