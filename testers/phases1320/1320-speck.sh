set -u
# #1320: the speck, the off-aimed camera, and the mock footage as the control.
#
# The frame in the issue is a camera on the maintainer's rig, so it cannot be committed.
# What is committed is the shape of the failure, painted onto the footage the detector
# already calibrates on: a small, very round disc that is nowhere near the bull.
#
#   A. an aimed camera with a speck on it -- the bull is still found, the speck is
#      refused by name, and the board calibrates.
#   B. the same speck on a camera aimed right and low, the way #1320's camera 3 is. The
#      real bull does not survive color_processing there (its bull's-eye window is
#      centred on the FRAME, and on this camera that is 170 px off the board), so
#      nothing can be a bull and the camera must say so rather than take the speck.
#
# Both discs are 3 px in the source and reach bull detection at 250 px of area -- the
# 205 the issue measured, through the same blur, dilation and closing the real one came
# through.

echo "--- build the speck footage from the mocks ---"
g++ -std=c++17 -O1 -o /run1320/i1320_speck_footage /app/testers/i1320_speck_footage.cpp \
  $(pkg-config --cflags --libs opencv4) || exit 1
/run1320/i1320_speck_footage /app/mocks/cam_1.mp4 /run1320/speck_aimed.avi 300 0 0 700,380,3 || exit 1
/run1320/i1320_speck_footage /app/mocks/cam_1.mp4 /run1320/speck_off.avi 300 180 90 700,380,3 || exit 1

mkdir -p /run1320/control /run1320/aimed /run1320/off

echo "--- the control: the footage the detector is known to calibrate on, no --debug ---"
cd /run1320/control && OD_MAX_CYCLES=20 /app/build/opendartboard \
  --cams /app/mocks/cam_1.mp4,/app/mocks/cam_2.mp4,/app/mocks/cam_3.mp4 \
  --width 1280 --height 720 > /run1320/control.out 2>&1
echo "CONTROL_RC=$?"

echo "--- A: an aimed camera with a speck painted on it ---"
cd /run1320/aimed && OD_MAX_CYCLES=20 /app/build/opendartboard --debug \
  --cams /run1320/speck_aimed.avi,/app/mocks/cam_2.mp4,/app/mocks/cam_3.mp4 \
  --width 1280 --height 720 > /run1320/aimed.out 2>&1
echo "AIMED_RC=$?"

echo "--- B: the same speck on an off-aimed camera, which must fail ---"
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
for f in control aimed off; do
  sed 's/\x1b\[[0-9;]*m//g' /run1320/$f.out > /run1320/$f.txt
done

FAILED=0
say() { echo "$1"; [ "$2" = ok ] || FAILED=1; }

echo "=== 1. the control calibrates, and finds the same three bulls it always found ==="
if grep -q 'Initial calibration completed successfully' /run1320/control.txt; then
  say "OK   the mocks calibrate" ok
else say "FAIL the mocks did not calibrate" no; fi
for expected in "Camera 1 bull at (616,283)" "Camera 2 bull at (651,313)" "Camera 3 bull at (654,293)"; do
  if grep -qF "$expected" /run1320/control.txt; then say "OK   $expected" ok
  else say "FAIL no line saying $expected"; FAILED=1; fi
done
grep -E '^\[(ERROR|WARN)\]' /run1320/control.txt || true
NOISE=$(grep -cE '^\[(ERROR|WARN)\]' /run1320/control.txt || true)
if [ "$NOISE" = "0" ]; then say "OK   the control prints no ERROR and no WARN" ok
else say "FAIL the control prints $NOISE ERROR/WARN lines" no; fi

echo "=== 2. what the winner was chosen on is on screen, without --debug ==="
# The whole of #1320 is that a 205-pixel speck at C=0.71 beat the bull on circularity
# alone, and the only place that was written down was a debug image.
if grep -qE 'Camera [0-9]+ bull at \([0-9]+,[0-9]+\), chosen on circularity [0-9.]+, radius [0-9.]+ px \([0-9.]+ of the board radius [0-9.]+ px' /run1320/control.txt; then
  say "OK   the line names circularity, the radius AND the board it was measured against" ok
else say "FAIL the winner is not stated with what it was chosen on" no; fi

echo "=== 3. A: the speck is refused by name, and the bull is still found ==="
grep -E 'REFUSED on size: it is 8\.9 px' /run1320/aimed.txt | head -2 || true
if grep -qE 'radius=8\.9, circ=0\.8[0-9], centre=\(700,37[89]\).*REFUSED on size' /run1320/aimed.txt; then
  say "OK   the speck is refused, on size, naming its radius against the board's" ok
else say "FAIL the speck was not refused on size" no; fi
if grep -qF 'Camera 1 bull at (616,283)' /run1320/aimed.txt; then
  say "OK   the bull on the specked camera is the bull, unmoved" ok
else say "FAIL the specked camera did not find the bull where the clean one does" no; fi
if grep -q 'Initial calibration completed successfully' /run1320/aimed.txt; then
  say "OK   a speck on the board does not stop it calibrating" ok
else say "FAIL the specked board did not calibrate" no; fi

echo "=== 4. B: nothing passes, so the camera fails rather than taking the least-bad ==="
grep -E '^\[ERROR\]\[GEOMETRY_CALIBRATION\] - Camera' /run1320/off.txt | head -1 || true
if grep -qE '^\[ERROR\]\[GEOMETRY_CALIBRATION\] - Camera [0-9]+ did not calibrate: the bull could not be found.*[0-9]+ were refused' /run1320/off.txt; then
  say "OK   one ERROR naming the camera, the stage and what it counted" ok
else say "FAIL the off-aimed camera did not say why it failed" no; fi
if grep -qE 'BOARD FAULTED: camera [0-9]+ did not calibrate: the bull could not be found' /run1320/off.txt; then
  say "OK   the vigil says which camera and which stage" ok
else say "FAIL BOARD FAULTED does not name the bull" no; fi
if grep -q 'Initial calibration completed successfully' /run1320/off.txt; then
  say "FAIL the off-aimed camera calibrated from a centre nothing vouched for" no
else say "OK   no calibration was claimed from a board with no bull on it" ok; fi

echo "=== 4b. every count printed is one the sentence contradicts itself without ==="
# A number nothing can falsify is not evidence. Every radius printed as refused on size
# is printed against the band it fell outside, in the same sentence, so a candidate
# inside the band cannot be reported as refused on it.
WRONG=$(grep -oE 'it is [0-9.]+ px across the radius, [0-9.]+ of the board radius [0-9.]+ px, and a bull on this board is [0-9.]+ to [0-9.]+ px' /run1320/aimed.txt /run1320/off.txt \
  | sed 's/.*it is \([0-9.]*\) px.*board is \([0-9.]*\) to \([0-9.]*\) px/\1 \2 \3/' \
  | awk '$1 >= $2 && $1 <= $3 { print }' | wc -l)
if [ "$WRONG" = "0" ]; then say "OK   no candidate refused on size is inside the band it is printed against" ok
else say "FAIL $WRONG candidates were refused on size while inside their own band" no; fi

echo "CHECK_RC=$FAILED"
exit $FAILED
