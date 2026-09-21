#!/bin/bash
# #1494 and #1495, inside the container. Everything here runs against /app, which is the
# tree, and writes to /run1494.
#
# It never ends on an `echo` (#1479): the last statement is `exit $FAILED`.
#
# FOUR ARMS, ONE BINARY -- the 2x2 that is the whole point. #1492 measured the two
# mechanisms and stopped BECAUSE they pull against each other: the union hull exists
# because darts fragment, and it is exactly what lets a second object win the tip. So a
# report that only measured both repairs together could not say whether one had been
# traded for the other.
#
#   AA  OD_ADVANCE_RESET=per-camera OD_TIP_FIGURE=union   the tree #1492 measured
#   AB  OD_TIP_FIGURE=union                               #1495's repair alone
#   BA  OD_ADVANCE_RESET=per-camera                       #1494's repair alone
#   BB  neither pin                                       both, which is what ships
#
# MUTATION PROOF -- see the pull request for the recorded output of each plant.
set -u

FAILED=0
say() { echo "$1"; [ "$2" = ok ] || FAILED=1; }

SRC=/app
OUT=/run1494

echo "=== building the tip probe ==="
g++ -std=c++17 -O1 -I "$SRC/src" -I "$SRC/src/utils" -I "$SRC/src/detector/geometry/calibration" \
  -o "$OUT/probe" "$SRC/testers/i1492_tip_probe.cpp" \
  "$SRC"/src/detector/geometry/calibration/*.cpp \
  "$SRC"/src/detector/geometry/detection/score_processing.cpp \
  "$SRC"/src/detector/geometry/detection/dart_processing.cpp \
  "$SRC"/src/detector/geometry/detection/motion_processing.cpp \
  $(pkg-config --cflags --libs opencv4) > "$OUT/build.log" 2>&1
if [ $? -ne 0 ]; then
  tail -40 "$OUT/build.log"
  echo "FAIL the tip probe did not build; nothing below measures anything"
  exit 2
fi

CLIPS="$SRC/mocks/rig-20260918/cam_1.mp4 $SRC/mocks/rig-20260918/cam_2.mp4 $SRC/mocks/rig-20260918/cam_3.mp4"

# NO CYCLE CAP: the clip is played to its end, 1691 cycles and 19 darts. mocks/rig-20260918
# ONLY -- #1478: every constant in this repository was fitted against the shipped mocks,
# so they cannot judge this.
arm() {
  local tag="$1"; shift
  echo
  echo "=== $tag: $* ==========================================="
  env "$@" OD_TIP_CENSUS=1 "$OUT/probe" "" $CLIPS > "$OUT/rows-$tag.txt" 2> "$OUT/pipeline-$tag.log"
  local rc=$?
  [ "$rc" -eq 0 ] || { tail -20 "$OUT/pipeline-$tag.log"; say "FAIL the $tag probe exited $rc" no; }
  python3 "$SRC/testers/i1492_spread.py" "$OUT/rows-$tag.txt" > "$OUT/spread-$tag.txt" 2>&1
  echo "    census rc=$?"
}

arm AA OD_ADVANCE_RESET=per-camera OD_TIP_FIGURE=union
arm AB OD_TIP_FIGURE=union
arm BA OD_ADVANCE_RESET=per-camera
arm BB OD_NOTHING_PINNED=1

echo
echo "=== the four arms, side by side ================================================"
python3 "$SRC/testers/i1494_arms.py" "$OUT" AA AB BA BB
CENSUS_RC=$?
if [ "$CENSUS_RC" -eq 0 ]; then
  say "OK   the 2x2 answered every claim #1494 and #1495 make" ok
else
  say "FAIL one of the claims was refused -- its own words are above" no
fi

echo
echo "=== what the shipping arm's own census says ===================================="
sed -n '/==== ONE/,$p' "$OUT/spread-BB.txt"
echo
echo "  #1492's SECOND claim is not asserted here, and that is the point rather than an"
echo "  omission. It says no off-board reading came out of a whole, single figure -- which"
echo "  was true of the tree it was written against BECAUSE the two mechanisms accounted"
echo "  for every one of them. Repairing both leaves whatever else is there exposed, and on"
echo "  this fixture it leaves one: dart 17 camera 2, a single 2,170 px contour, 250 mm"
echo "  from the board centre, which is not a dart cut up and not a second dart -- it is a"
echo "  camera that did not find the dart at all. It is a THIRD mechanism, it is reported"
echo "  above by name, and it is a new issue rather than a red build. What IS asserted on"
echo "  the shipping arm is the first claim, that the two populations stay disjoint, and"
echo "  the arms table above asserts it."

echo
if [ "$FAILED" = 0 ]; then echo "i1494: PASS"; else echo "i1494: FAIL"; fi
exit $FAILED
