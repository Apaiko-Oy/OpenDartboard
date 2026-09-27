#!/bin/bash
# #1501: ONE SUPPORTED BOARD (the Winmau Blade 6), AND A BOARD THAT IS NOT IT SAYS SO.
#
# The maintainer's decision: an unrecognised board is announced at the default log level,
# in words an operator can act on, and the same sentence names the remedy, OD_CAMERA_WEDGES.
# `board_recognition::recognise` is that sentence; geometry_detector says it once per start,
# after every anchor is final.
#
# WHAT IS ASSERTED:
#
#   1. THE PURE CHECK: every verdict from OrientationData a calibration can leave behind,
#      compiled against the header alone (i1501_recognition_check.cpp).
#
#   2. THE BINARY, one calibration-only start per fixture (OD_MAX_CYCLES=1):
#        rig-20260918 (a Winmau Blade 6, printed numbers, no wire ring)
#            -> exactly one BOARD RECOGNITION line, the supported board's shape, at INFO.
#        the shipped mocks (the upstream Unicorn: bent-wire numbers AND clip wires)
#            -> "NOT the Winmau Blade 6", best-effort, at INFO.
#        rig-20260918 under OD_NUMBER_ANCHOR=inner -- the reader on the numberless annulus,
#            #1498's own control, which is what a board whose numbers cannot be read looks
#            like to this reader on the same binary
#            -> "NOT recognised" as a WARN, naming OD_CAMERA_WEDGES as the remedy.
#        the same, with OD_CAMERA_WEDGES stating one camera
#            -> still "NOT recognised", but at INFO, saying the remedy is in effect.
#
# Announce-only is not asserted here; it is proved by the 1555 bake-off being unchanged.
# The script ends on `exit`, never on an `echo`: #1463, #1479.
set -u

BIN=/app/build/opendartboard
RUN=/run1501
SRC=/app
FAIL=0
note() { if [ "$1" -eq 0 ]; then echo "ok   $2"; else echo "FAIL $2"; FAIL=$((FAIL + 1)); fi; }

echo "---- 1. the pure check ----"
g++ -std=c++17 -O1 -I "$SRC/src" -I "$SRC/src/utils" -I "$SRC/src/detector/geometry/calibration" \
  -o $RUN/check "$SRC/testers/i1501_recognition_check.cpp" \
  $(pkg-config --cflags --libs opencv4) > $RUN/check-build.log 2>&1
if [ $? -ne 0 ]; then
  tail -30 $RUN/check-build.log
  note 1 "the pure check built"
else
  $RUN/check; note $? "the pure check passed"
fi

if [ ! -x $BIN ]; then
  echo "FAIL no $BIN -- a fresh worktree has none. Build it: make build (or testers/run_all.sh)."
  exit 1
fi
NEWEST=$(find /app/src /app/CMakeLists.txt -newer $BIN 2>/dev/null | head -5)
if [ -n "$NEWEST" ]; then
  echo "FAIL $BIN is OLDER than the source it is supposed to be measuring:"
  echo "$NEWEST"
  exit 1
fi

calibrate_once() { # $1 fixture dir, $2 name, $3.. extra env
  local dir="$1" name="$2"; shift 2
  rm -rf $RUN/cache $RUN/debug_frames
  ( cd $RUN && env OD_MAX_CYCLES=1 "$@" timeout 300 $BIN \
      --cams "$dir/cam_1.mp4,$dir/cam_2.mp4,$dir/cam_3.mp4" \
      --width 1280 --height 720 > "$RUN/$name.raw" 2>&1 )
  local rc=$?
  sed 's/\x1b\[[0-9;]*m//g' "$RUN/$name.raw" > "$RUN/$name.out"
  grep -a 'BOARD RECOGNITION:' "$RUN/$name.out" > "$RUN/$name.said"
  grep -a 'ORIENTATION: ' "$RUN/$name.out" | sed 's/^/     /'
  echo "     said:"; sed 's/^/     /' "$RUN/$name.said"
  return $rc
}

once_at() { # $1 name, $2 level word, $3 fixed text: exactly one line, at that level, carrying it
  local n
  n=$(grep -c 'BOARD RECOGNITION:' "$RUN/$1.said")
  [ "$n" = 1 ] && grep -q "\[$2\].*BOARD RECOGNITION: $3" "$RUN/$1.said"
}

echo
echo "---- 2a. rig-20260918, a Winmau Blade 6 ----"
calibrate_once "$SRC/mocks/rig-20260918" rig; note $? "the rig start ended cleanly"
once_at rig INFO "this board has the Winmau Blade 6's shape"
note $? "rig: said once, at INFO: the supported board's shape"

echo
echo "---- 2b. the shipped mocks, the upstream Unicorn ----"
calibrate_once "$SRC/mocks" mocks; note $? "the mocks start ended cleanly"
once_at mocks INFO "this is NOT the Winmau Blade 6"
note $? "mocks: said once, at INFO: another board, best-effort"

echo
echo "---- 2c. rig-20260918 read on the numberless annulus (OD_NUMBER_ANCHOR=inner) ----"
calibrate_once "$SRC/mocks/rig-20260918" inner OD_NUMBER_ANCHOR=inner; note $? "the control start ended cleanly"
once_at inner WARN "this board is NOT recognised"
note $? "control: said once, as a WARN: not recognised"
grep -q "REMEDY: set OD_CAMERA_WEDGES" "$RUN/inner.said"
note $? "control: the same sentence names OD_CAMERA_WEDGES as the remedy"

echo
echo "---- 2d. the same, with OD_CAMERA_WEDGES stating camera 1 ----"
calibrate_once "$SRC/mocks/rig-20260918" stated OD_NUMBER_ANCHOR=inner OD_CAMERA_WEDGES=3,0,0
note $? "the stated start ended cleanly"
once_at stated INFO "this board is NOT recognised"
note $? "stated: still not recognised, but at INFO"
grep -q "OD_CAMERA_WEDGES states the anchor on 1 of 3 cameras" "$RUN/stated.said"
note $? "stated: it says the remedy is in effect"

echo
echo "I1501 failures=$FAIL"
exit $FAIL
