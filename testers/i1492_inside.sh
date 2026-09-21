#!/bin/bash
# #1492, inside the container. Everything here runs against /app, which is the tree, and
# writes to /run1492.
#
# It never ends on an `echo` (#1479): the last statement is `exit $FAILED`.
set -u

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

echo
echo "  readings the pin moved:                 $MOVED"
echo "  off-board readings, floor 400:          $B_OFF"
echo "  off-board readings, floor 0:            $F_OFF"
if [ "${MOVED:-0}" -gt 0 ]; then
  say "OK   OD_TIP_PIECE_FLOOR is live: the pin really chose the floor this run read" ok
else
  say "FAIL OD_TIP_PIECE_FLOOR moved nothing, so the run below measures the same binary twice" no
fi
if [ -n "$F_OFF" ] && [ -n "$B_OFF" ] && [ "$F_OFF" -ge "$B_OFF" ]; then
  say "OK   removing the floor does not reduce the off-board readings, so the floor is not the repair (#1322)" ok
else
  say "FAIL removing the floor DID reduce the off-board readings -- the refusal recorded in #1492 no longer holds and the issue must be re-read" no
fi

echo
if [ "$FAILED" = 0 ]; then echo "i1492: PASS"; else echo "i1492: FAIL"; fi
exit $FAILED
