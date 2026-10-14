#!/bin/bash
# #1492, inside the container. Everything here runs against /app, which is the tree, and
# writes to /run1492.
#
# It never ends on an `echo` (#1479): the last statement is `exit $FAILED`.
#
# MUTATION PROOF, run 2026-09-21 on the 4-core box, each plant flipping its own half:
#
#   A  `furthest_hull_point = hull[0]` in detectTipAndCenter -- the tip becomes an
#      arbitrary extreme of the figure rather than the end furthest from the piece that
#      was measured. The census claim goes RED ("the worst all-on-board dart is 25.8 mm
#      and the best dart with an off-board camera is 19.6 mm"); the pin's own check --
#      that OD_TIP_PIECE_FLOOR is live -- stays GREEN.
#   B  `piecesFloor()` reading `nullptr` instead of the environment. The pin's check goes
#      RED ("moved nothing, so the run below measures the same binary twice") and BOTH
#      census claims stay GREEN, because the same binary was then measured twice.
#
# Since #1824 plant B has nothing left to turn red: the pin's check and claim TWO are
# historical, printed and not asserted (see the floor section below). What stays
# asserted is claim ONE and the instrument check in i1492_spread.py.
#
# Green on the tree: 201 s, load_at_end 5.87.
set -u
# #1758: the detection windows are pinned to CYCLES. Since ece438d they default to
# milliseconds of the motion clock, which here is the wall clock, so a replay on a loaded
# box cuts different windows from the same footage: 1494-figure's shipping arm and
# 1492-tips' base run -- one binary, one clip, minutes apart -- read the worst on-board
# dart as 108.2 mm and 19.5 mm. A figure this tester asserts must be the tree's, not the box's.
export OD_WINDOW_UNIT=cycles
# turnaus#1838: and the motion clock is the footage's. The cycles pin takes the WINDOWS off
# the wall clock and leaves the cooldown, the spike window, the safety timeout and the
# body-window vote on it, so at a load average of ~18 this tester's two replays of one
# binary read 19 and 20 darts. The probe winds the capture clock (testers/replay_clock.hpp);
# on a quiet box it publishes the same 63 readings on either clock (measured 2026-10-14),
# so this pin moves no figure, it only takes the box's load out of them. 1838-replay-noise
# is the tester that holds it.
export OD_MOTION_CLOCK=capture

FAILED=0
say() { echo "$1"; [ "$2" = ok ] || FAILED=1; }

SRC=/app
OUT=/run1492
PROBE="$OUT/probe"

echo "=== building the tip probe ==="
g++ -std=c++17 -O1 -I "$SRC/src" -I "$SRC/src/utils" -I "$SRC/src/detector/geometry/calibration" \
  -o "$PROBE" "$SRC/testers/i1492_tip_probe.cpp" \
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
echo
echo "=== the tree as it is ==========================================================="
OD_TIP_CENSUS=1 "$PROBE" "" $CLIPS > "$OUT/rows-base.txt" 2> "$OUT/pipeline-base.log"
RC=$?
[ "$RC" -eq 0 ] || { tail -20 "$OUT/pipeline-base.log"; say "FAIL the probe exited $RC" no; }
python3 "$SRC/testers/i1492_spread.py" "$OUT/rows-base.txt" > "$OUT/base.txt" 2>&1
CENSUS_RC=$?
cat "$OUT/base.txt"
if [ "$CENSUS_RC" -eq 0 ]; then
  say "OK   the census answered both of #1492's questions" ok
else
  say "FAIL the census refused one of #1492's two claims -- its own words are above" no
fi

echo
echo "=== the obvious repair, measured rather than argued =============================="
# The 400 px contour floor is what leaves a camera holding the flight alone. Removing it
# is the repair anybody would reach for first, and #1322 says a constant may not be tuned
# to this fixture -- so it is PINNED on the same binary and measured. OD_TIP_PIECE_FLOOR=0
# admits every contour under the 20,000 px cap.
OD_TIP_CENSUS=1 OD_TIP_PIECE_FLOOR=0 "$PROBE" "" $CLIPS > "$OUT/rows-floor0.txt" 2> "$OUT/pipeline-floor0.log"
RC=$?
[ "$RC" -eq 0 ] || { tail -20 "$OUT/pipeline-floor0.log"; say "FAIL the floor-0 probe exited $RC" no; }
python3 "$SRC/testers/i1492_spread.py" "$OUT/rows-floor0.txt" > "$OUT/floor0.txt" 2>&1
sed -n '/==== ONE/,$p' "$OUT/floor0.txt"

B_LINE=$(grep '^I1492SUMMARY ' "$OUT/base.txt" | tail -1)
F_LINE=$(grep '^I1492SUMMARY ' "$OUT/floor0.txt" | tail -1)
B_OFF=$(echo "$B_LINE" | sed 's/.*offboard=\([0-9]*\).*/\1/')
F_OFF=$(echo "$F_LINE" | sed 's/.*offboard=\([0-9]*\).*/\1/')
MOVED=$(python3 - "$OUT/rows-base.txt" "$OUT/rows-floor0.txt" <<'PY'
import sys
def tips(p):
    out = {}
    for line in open(p):
        if not line.startswith("I1492DART "):
            continue
        f = dict(kv.split("=", 1) for kv in line.split() if "=" in kv)
        out[(f["dart"], f["cam"])] = f["tip"]
    return out
a, b = tips(sys.argv[1]), tips(sys.argv[2])
print(sum(1 for k in a if b.get(k) != a[k]))
PY
)

# HISTORICAL since #1824, printed and not asserted. #1492 (5ca42c5, 2026-09-21) measured
# the floor live and its removal no repair. Since #1494 the shipping figure is the largest
# contour's group and has no floor at all, so OD_TIP_PIECE_FLOOR=0 moves nothing on it --
# measured on main with cycle windows: 0 readings moved, 12 off-board at floor 400 and 12
# at floor 0. The "pin is live" check passed before #1758 only because wall-clock windows
# made two runs of one binary differ anyway. On the union figure (OD_TIP_FIGURE=union
# OD_ADVANCE_RESET=per-camera) the pin moves 34 readings and removing the floor takes the
# off-board readings from 15 to 13, the reverse of #1492's refusal. No switch on main
# restores the stage the section measured; the maintainer retired it on 2026-10-12.
echo
echo "  HISTORICAL (#1824) -- the floor experiment is inert on the shipping figure, and"
echo "  these figures colour nothing. #1492 (5ca42c5) recorded a live pin and no reduction."
echo "  readings the pin moved:                 $MOVED"
echo "  off-board readings, floor 400:          $B_OFF"
echo "  off-board readings, floor 0:            $F_OFF"

echo
if [ "$FAILED" = 0 ]; then echo "i1492: PASS"; else echo "i1492: FAIL"; fi
exit $FAILED
