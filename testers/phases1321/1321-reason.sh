set -u
# #1321: the failure and the control, in one run, with no --debug on either.
#
# The failing input is made here rather than committed: three clips of the mock footage
# at 18% brightness, which is a board a camera can see and a doubles mask nothing can be
# traced on. It is the same shape of fault the maintainer measured on 2026-09-18 -- the
# doubles ring is not fitted and the three stages below it refuse -- reached through the
# white-pixel threshold rather than the boundary-point count, which is the branch that
# logged nothing at all at any level before this issue.
#
# The assertion is what a tester can see without knowing the flag exists: one ERROR per
# camera that names the camera, the stage and a number, and not one of the three echoes.

echo "--- build the dark footage from the mocks ---"
g++ -std=c++17 -O1 -o /run1321/i1321_dark_footage /app/testers/i1321_dark_footage.cpp \
  -I/usr/local/include/opencv4 -L/usr/local/lib \
  -lopencv_videoio -lopencv_imgcodecs -lopencv_imgproc -lopencv_core || exit 1
for i in 1 2 3; do
  /run1321/i1321_dark_footage /app/mocks/cam_$i.mp4 /run1321/dark_$i.avi 0.18 300 || exit 1
done

echo "--- the failure, WITHOUT --debug ---"
OD_MAX_CYCLES=20 /app/build/opendartboard \
  --cams /run1321/dark_1.avi,/run1321/dark_2.avi,/run1321/dark_3.avi \
  --width 1280 --height 720 > /run1321/dark.out 2> /run1321/dark.err
echo "DARK_RC=$?"

echo "--- the control: the same command against the mocks ---"
OD_MAX_CYCLES=20 /app/build/opendartboard \
  --cams /app/mocks/cam_1.mp4,/app/mocks/cam_2.mp4,/app/mocks/cam_3.mp4 \
  --width 1280 --height 720 > /run1321/control.out 2> /run1321/control.err
echo "CONTROL_RC=$?"

# Colour codes are in every console line; strip them once and read the plain text.
strip() { sed 's/\x1b\[[0-9;]*m//g' "$1"; }
strip /run1321/dark.out > /run1321/dark.txt
strip /run1321/control.out > /run1321/control.txt

FAILED=0
say() { echo "$1"; [ "$2" = ok ] || FAILED=1; }

echo "=== 1. the reason is on screen without --debug ==="
REASONS=$(grep -cE '^\[ERROR\]\[GEOMETRY_CALIBRATION\] - Camera [0-9]+ did not calibrate: .*[0-9]' /run1321/dark.txt || true)
grep -E '^\[ERROR\]\[GEOMETRY_CALIBRATION\] - Camera' /run1321/dark.txt || true
if [ "$REASONS" = "3" ]; then say "OK   one ERROR naming camera, stage and a count, per camera (3)" ok
else say "FAIL expected 3 naming ERROR lines, got $REASONS" no; fi

echo "=== 2. the three echoes are not ERROR ==="
for echo_line in \
  'ERROR.*Invalid input data' \
  'ERROR.*Insufficient calibration data for intersection calculation' \
  'ERROR.*Failed to compute ring-wire intersections'
do
  N=$(grep -cE "$echo_line" /run1321/dark.txt || true)
  if [ "$N" = "0" ]; then say "OK   no ERROR matching /$echo_line/" ok
  else say "FAIL $N ERROR lines matching /$echo_line/" no; fi
done

echo "=== 3. the control calibrates and says nothing new ==="
if grep -q 'Initial calibration completed successfully' /run1321/control.txt; then
  say "OK   the mocks calibrate" ok
else say "FAIL the mocks did not calibrate" no; fi
NOISE=$(grep -cE '^\[(ERROR|WARN)\]' /run1321/control.txt || true)
grep -E '^\[(ERROR|WARN)\]' /run1321/control.txt || true
if [ "$NOISE" = "0" ]; then say "OK   the control prints no ERROR and no WARN" ok
else say "FAIL the control prints $NOISE ERROR/WARN lines" no; fi

echo "CHECK_RC=$FAILED"
exit $FAILED
