#!/bin/bash
# #1497, inside the container. Everything here runs against /app, which is the tree, and
# writes to /run1497.
#
# It never ends on an `echo` (#1463): the last statement is `exit $FAILED`.
set -u

FAILED=0
say() { echo "$1"; [ "$2" = ok ] || FAILED=1; }

CENSUS=/run1497/census
SRC=/app

echo "=== building the number-ring census ============================================"
g++ -std=c++17 -O1 -I "$SRC/src" -I "$SRC/src/utils" -I "$SRC/src/detector/geometry/calibration" \
  -o "$CENSUS" "$SRC/testers/i1497_number_census.cpp" \
  "$SRC"/src/detector/geometry/calibration/*.cpp \
  $(pkg-config --cflags --libs opencv4) > "$CENSUS.build.log" 2>&1
if [ $? -ne 0 ]; then
  tail -40 "$CENSUS.build.log"
  echo "FAIL the census did not build; nothing below it measures anything"
  exit 2
fi

# The two fixtures, measured on the same terms. The rig is the one #1498 is about; the
# shipped mocks are the CONTRAST -- they anchor successfully today (#1486), so if their
# numbers are far more legible than the rig's, that is itself the finding.
for FIX in rig mocks; do
  case "$FIX" in
    rig)   DIR="$SRC/mocks/rig-20260918" ;;
    mocks) DIR="$SRC/mocks" ;;
  esac

  OUT="/run1497/$FIX"
  mkdir -p "$OUT"

  echo
  echo "=== $FIX: calibrating on the averaged frame the detector uses =================="
  "$CENSUS" "$OUT" "$DIR/cam_1.mp4" "$DIR/cam_2.mp4" "$DIR/cam_3.mp4" \
    > "/run1497/$FIX.rows.txt" 2> "/run1497/$FIX.log"
  RC=$?
  grep '^I1497CAM ' "/run1497/$FIX.rows.txt"
  grep '^I1497NOTE ' "/run1497/$FIX.rows.txt"
  grep '^I1497END ' "/run1497/$FIX.rows.txt"
  if [ "$RC" -ne 0 ]; then
    tail -20 "/run1497/$FIX.log"
    say "FAIL $FIX: the census exited $RC; there are no cells to look at" no
    continue
  fi

  echo
  python3 "$SRC/testers/i1497_ring.py" "/run1497/$FIX.rows.txt"

  # The structural control, and it asserts no threshold: three cameras were calibrated, a
  # board plane was built on them, and twenty cells came out of each plane that was. A
  # census that measured nothing must not read as a census that measured mush.
  CAMS=$(grep -c '^I1497CAM ' "/run1497/$FIX.rows.txt" || true)
  PLANES=$(grep '^I1497CAM ' "/run1497/$FIX.rows.txt" | grep -c 'planeBuilt=1' || true)
  CELLS=$(grep -c '^I1497CELL ' "/run1497/$FIX.rows.txt" || true)
  CROPS=$(ls "$OUT"/rect_cam*.png 2>/dev/null | wc -l)
  SHEETS=$(ls "$OUT"/sheet_cam*.png 2>/dev/null | wc -l)
  echo "  $CAMS cameras, $PLANES with a board plane, $CELLS cells, $CROPS rectified crops, $SHEETS contact sheets"
  if [ "$CAMS" -eq 3 ] && [ "$PLANES" -ge 1 ] && [ "$CELLS" -eq $((PLANES * 20)) ] && [ "$CROPS" -eq "$CELLS" ]; then
    say "OK   $FIX: every camera with a plane gave twenty cells and twenty crops to look at" ok
  else
    say "FAIL $FIX: the cells and the crops do not account for the planes that were built" no
  fi
done

echo
echo "=== what a reader would be handed ============================================="
echo "  /run1497/<fixture>/sheet_cam<N>.png   twenty rectified cells, one camera, one picture"
echo "  /run1497/<fixture>/rect_cam<N>_wedge<K>.png  one cell, rectified at 2 px/mm"
echo "  /run1497/<fixture>/raw_cam<N>_wedge<K>.png   the same cell unresampled, 4x nearest"
echo "  /run1497/<fixture>/frame_cam<N>.png   the whole averaged frame with the cells drawn"

echo
if [ "$FAILED" = 0 ]; then echo "i1497: PASS"; else echo "i1497: FAIL"; fi
exit $FAILED
