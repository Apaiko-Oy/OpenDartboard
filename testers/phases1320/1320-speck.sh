set -u
# #1320: the speck, and the two rigs beside it as controls.
#
# The frame in the issue is one camera of the maintainer's rig on the day, and what it
# recorded is in this repository as mocks/rig-20260918 -- where all three cameras find
# their bull and calibrate. So the 205-pixel speck needs that camera's framing on that
# day and this footage does not reproduce it, which is worth knowing: the fault is not
# in the rig, it is in what the scoring was willing to choose.
#
# What is committed is therefore the shape of the failure, painted onto the footage that
# works. i1320_speck_footage puts a small very round disc on a camera aimed right and low
# the way #1320's camera 3 is. Two things happen there and both are the point: the real
# bull does not survive color_processing on an off-aimed camera -- its bull's-eye window
# is centred on the FRAME, which on this one is 170 px off the board -- and the speck,
# which is near the middle of the frame, does. So the only round thing left is the speck,
# and the old rule would have taken it.
#
# The disc is 3 px in the source and reaches bull detection at 250 px of area: the 205 the
# issue measured, through the same blur, dilation and closing the real one came through.

echo "--- build the speck footage from the mocks ---"
g++ -std=c++17 -O1 -o /run1320/i1320_speck_footage /app/testers/i1320_speck_footage.cpp \
  $(pkg-config --cflags --libs opencv4) || exit 1
/run1320/i1320_speck_footage /app/mocks/cam_1.mp4 /run1320/speck_off.avi 300 180 90 700,380,3 || exit 1

mkdir -p /run1320/mocks /run1320/rig /run1320/off

echo "--- control 1: the shipped mocks, no --debug ---"
cd /run1320/mocks && OD_MAX_CYCLES=20 /app/build/opendartboard \
  --cams /app/mocks/cam_1.mp4,/app/mocks/cam_2.mp4,/app/mocks/cam_3.mp4 \
  --width 1280 --height 720 > /run1320/mocks.out 2>&1
echo "MOCKS_RC=$?"

echo "--- control 2: the rig this issue was measured on, no --debug ---"
cd /run1320/rig && OD_MAX_CYCLES=20 /app/build/opendartboard \
  --cams /app/mocks/rig-20260918/cam_1.mp4,/app/mocks/rig-20260918/cam_2.mp4,/app/mocks/rig-20260918/cam_3.mp4 \
  --width 1280 --height 720 > /run1320/rig.out 2>&1
echo "RIG_RC=$?"

echo "--- the speck, on a camera aimed right and low ---"
# A board that cannot calibrate goes to #895's fault vigil and stays there, so this one
# is run in the background and ended by its own recorded pid, never by pattern.
cd /run1320/off && /app/build/opendartboard --debug \
  --cams /run1320/speck_off.avi --width 1280 --height 720 > /run1320/off.out 2>&1 &
OFF=$!
sleep 25
kill -TERM $OFF 2>/dev/null
wait $OFF 2>/dev/null
echo "OFF_RC=$?"

# Colour codes are in every console line; strip them once and read the plain text.
for f in mocks rig off; do
  sed 's/\x1b\[[0-9;]*m//g' /run1320/$f.out > /run1320/$f.txt
done

FAILED=0
say() { echo "$1"; [ "$2" = ok ] || FAILED=1; }

echo "=== 1. both rigs calibrate, and find the same bulls they always found ==="
for rig in mocks rig; do
  if grep -q 'Initial calibration completed successfully' /run1320/$rig.txt; then
    say "OK   $rig calibrates" ok
  else say "FAIL $rig did not calibrate" no; fi
  NOISE=$(grep -cE '^\[(ERROR|WARN)\]' /run1320/$rig.txt || true)
  grep -E '^\[(ERROR|WARN)\]' /run1320/$rig.txt || true
  if [ "$NOISE" = "0" ]; then say "OK   $rig prints no ERROR and no WARN" ok
  else say "FAIL $rig prints $NOISE ERROR/WARN lines" no; fi
done
for expected in "Camera 1 bull at (616,283)" "Camera 2 bull at (651,313)" "Camera 3 bull at (654,293)"; do
  if grep -qF "$expected" /run1320/mocks.txt; then say "OK   mocks: $expected" ok
  else say "FAIL mocks: no line saying $expected" no; fi
done
for expected in "Camera 1 bull at (671,309)" "Camera 2 bull at (626,292)" "Camera 3 bull at (700,298)"; do
  if grep -qF "$expected" /run1320/rig.txt; then say "OK   rig: $expected" ok
  else say "FAIL rig: no line saying $expected" no; fi
done

echo "=== 2. what the winner was chosen on is on screen, without --debug ==="
# The whole of #1320 is that a 205-pixel speck at C=0.71 beat the bull on circularity
# alone, and the only place that was written down was a debug image.
for rig in mocks rig; do
  if grep -qE 'Camera [0-9]+ bull at \([0-9]+,[0-9]+\), chosen on circularity [0-9.]+, radius [0-9.]+ px \([0-9.]+ of the board radius [0-9.]+ px' /run1320/$rig.txt; then
    say "OK   $rig: the line names circularity, the radius AND the board it was measured against" ok
  else say "FAIL $rig: the winner is not stated with what it was chosen on" no; fi
done

echo "=== 3. a constant measured on one rig is a constant that happens to fit ==="
# Both rigs are quoted, because they disagree: what reaches this stage is the bull after
# a blur, a dilation and a closing, which add pixels rather than a ratio, so the smaller
# the board is in frame the larger its bull measures against it.
grep -ohE 'radius [0-9.]+ px \(0\.[0-9]+ of the board radius [0-9.]+ px' /run1320/mocks.txt /run1320/rig.txt || true
SPREAD=$(grep -ohE '\(0\.[0-9]+ of the board radius' /run1320/mocks.txt /run1320/rig.txt | grep -oE '0\.[0-9]+' | sort -u | wc -l)
if [ "$SPREAD" -ge 2 ]; then say "OK   the two rigs do not agree on the ratio, and the band holds both" ok
else say "FAIL only one ratio was measured, so the band is fitted to one rig" no; fi

echo "=== 4. the speck is refused by name, and nothing is calibrated from it ==="
# What is pinned here is where the disc was PAINTED -- 700,380 is an argument to
# i1320_speck_footage twenty lines up, not a measurement -- and a window of ten pixels
# around it, because a centroid of an antialiased disc is not an integer. Everything else
# about the speck is read out of the run.
#
# #1335: its radius was pinned at 8.9 px and its centroid at y=378 or 379 until two runs
# of this tester on ONE commit measured 250 px at (700,379) and 244 px at (700,380). The
# difference is which frame of the clip calibration lands on -- a binary built with
# DEBUG_SEEK_VIDEO starts three seconds in and one built without it starts at the head --
# and a tenth of a pixel of radius is six pixels of area on a disc this small. The frame
# is not the subject of this issue; that a small very round thing off the middle of the
# board is refused on size is. So the size and the centroid are read, and what is
# asserted is the refusal, its roundness, and the band it is refused against.
SPECK=$(grep -E 'Contour [0-9]+: .*centre=\((69[0-9]|70[0-9]),(37[0-9]|38[0-9])\)' /run1320/off.txt | head -1)
echo "${SPECK:-no contour was reported where the disc was painted}"
if [ -n "$SPECK" ]; then say "OK   the speck reached bull detection, where the disc was painted" ok
else say "FAIL nothing was reported within ten pixels of (700,380), so this run measured no speck" no; fi
if [ -n "$SPECK" ] && echo "$SPECK" | grep -qE 'circ=0\.[89][0-9]' && echo "$SPECK" | grep -q 'REFUSED on size'; then
  say "OK   the speck is round enough to have won on circularity, and is refused on size" ok
else say "FAIL the speck was not refused on size" no; fi
# ... and the number it was refused on is below the band the same sentence prints, so a
# refusal cannot be reported against a radius that is inside the band.
if [ -n "$SPECK" ] && echo "$SPECK" \
  | sed 's/.*it is \([0-9.]*\) px across the radius.*board is \([0-9.]*\) to \([0-9.]*\) px.*/\1 \2 \3/' \
  | awk 'NF == 3 && $1 < $2 { found = 1 } END { exit found ? 0 : 1 }'; then
  say "OK   the radius it was refused on is below the band printed beside it" ok
else say "FAIL the speck's radius is not below the band it was refused against" no; fi
grep -E '^\[ERROR\]\[GEOMETRY_CALIBRATION\] - Camera' /run1320/off.txt | head -1 || true
if grep -qE '^\[ERROR\]\[GEOMETRY_CALIBRATION\] - Camera [0-9]+ did not calibrate: the bull could not be found.*[0-9]+ were refused' /run1320/off.txt; then
  say "OK   one ERROR naming the camera, the stage and what it counted" ok
else say "FAIL the camera did not say why it failed" no; fi
if grep -qE 'BOARD FAULTED: camera [0-9]+ did not calibrate: the bull could not be found' /run1320/off.txt; then
  say "OK   the vigil says which camera and which stage" ok
else say "FAIL BOARD FAULTED does not name the bull" no; fi
if grep -q 'Initial calibration completed successfully' /run1320/off.txt; then
  say "FAIL a centre nothing vouched for was calibrated from" no
else say "OK   no calibration was claimed from a board with no bull on it" ok; fi

echo "=== 4b. every count printed is one the sentence contradicts itself without ==="
# A number nothing can falsify is not evidence. Every radius printed as refused on size
# is printed against the band it fell outside, in the same sentence, so a candidate
# inside the band cannot be reported as refused on it.
WRONG=$(grep -ohE 'it is [0-9.]+ px across the radius, [0-9.]+ of the board radius [0-9.]+ px, and a bull on this board is [0-9.]+ to [0-9.]+ px' /run1320/off.txt \
  | sed 's/.*it is \([0-9.]*\) px.*board is \([0-9.]*\) to \([0-9.]*\) px/\1 \2 \3/' \
  | awk '$1 >= $2 && $1 <= $3 { print }' | wc -l)
if [ "$WRONG" = "0" ]; then say "OK   no candidate refused on size is inside the band it is printed against" ok
else say "FAIL $WRONG candidates were refused on size while inside their own band" no; fi

echo "CHECK_RC=$FAILED"
exit $FAILED
