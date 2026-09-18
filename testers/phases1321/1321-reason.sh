set -u
# #1321: the failure and the control, in one run, with no --debug on either.
#
# The failing input is made here rather than committed: three clips of the mock footage
# at 18% brightness, which is a board a camera can see and a doubles mask nothing can be
# ray-traced on. It is the same shape of fault the maintainer measured on 2026-09-18 --
# the doubles ring is not fitted and the three stages below it refuse -- reached through
# the white-pixel threshold rather than the boundary-point count, which is the branch
# that logged nothing at all at any level before this issue.
#
# Since 74be46f a camera without a fitted doubles ring fails the whole calibration, so
# the dark run does not reach the scoring loop at all: it goes to #895's fault vigil and
# stays there. It is therefore run in the background and ended by its own recorded pid,
# never by pattern, and BOARD FAULTED is read as well as the calibration line.
#
# The assertion is what a tester can see without knowing the flag exists: one ERROR per
# camera naming the camera, the stage and a number, not one of the three echoes, and a
# faulted board that says which half of the ladder it fell off.

echo "--- build the dark footage from the mocks ---"
g++ -std=c++17 -O1 -o /run1321/i1321_dark_footage /app/testers/i1321_dark_footage.cpp \
  $(pkg-config --cflags --libs opencv4) || exit 1
for i in 1 2 3; do
  /run1321/i1321_dark_footage /app/mocks/cam_$i.mp4 /run1321/dark_$i.avi 0.18 300 || exit 1
done

echo "--- the failure, WITHOUT --debug ---"
/app/build/opendartboard \
  --cams /run1321/dark_1.avi,/run1321/dark_2.avi,/run1321/dark_3.avi \
  --width 1280 --height 720 > /run1321/dark.out 2> /run1321/dark.err &
DARK=$!
sleep 25
kill -TERM $DARK 2>/dev/null
wait $DARK 2>/dev/null
echo "DARK_RC=$?"

echo "--- the control: the same footage the detector is known to calibrate on ---"
OD_MAX_CYCLES=20 /app/build/opendartboard \
  --cams /app/mocks/cam_1.mp4,/app/mocks/cam_2.mp4,/app/mocks/cam_3.mp4 \
  --width 1280 --height 720 > /run1321/control.out 2> /run1321/control.err
echo "CONTROL_RC=$?"

# Colour codes are in every console line; strip them once and read the plain text.
sed 's/\x1b\[[0-9;]*m//g' /run1321/dark.out > /run1321/dark.txt
sed 's/\x1b\[[0-9;]*m//g' /run1321/control.out > /run1321/control.txt

FAILED=0
say() { echo "$1"; [ "$2" = ok ] || FAILED=1; }

echo "=== 1. the reason is on screen without --debug ==="
grep -E '^\[ERROR\]\[GEOMETRY_CALIBRATION\] - Camera' /run1321/dark.txt || true
REASONS=$(grep -cE '^\[ERROR\]\[GEOMETRY_CALIBRATION\] - Camera [0-9]+ did not calibrate: .*[0-9]' /run1321/dark.txt || true)
if [ "$REASONS" = "3" ]; then say "OK   one ERROR naming camera, stage and a count, per camera (3)" ok
else say "FAIL expected 3 naming ERROR lines, got $REASONS" no; fi

echo "=== 1b. the number it prints is the number that fell short ==="
# A line nothing can falsify is not evidence. Every count here is a count against a
# threshold in the same sentence, so the sentence contradicts itself if the number comes
# from anywhere but the measurement that refused: a mask holding more pixels than the
# stage asks for did not fail on pixels. Measured by reporting the frame width instead
# of the white-pixel count, where this reads "holds 1280 white pixels and this stage
# needs at least 1000" and the check below goes red.
INCONSISTENT=$(grep -oE 'the doubles mask holds [0-9]+ white pixels and this stage needs at least [0-9]+' /run1321/dark.txt \
  | awk '$5 >= $14 { print }' | wc -l)
RAYS=$(grep -oE '[0-9]+ gave a boundary point, and at least [0-9]+ are needed' /run1321/dark.txt \
  | awk '$1 >= $7 { print }' | wc -l)
if [ "$INCONSISTENT" = "0" ] && [ "$RAYS" = "0" ]; then
  say "OK   every count printed is below the threshold it is printed against" ok
else say "FAIL $INCONSISTENT mask counts and $RAYS ray counts are not below their own threshold" no; fi

echo "=== 2. the three echoes are not ERROR ==="
for pattern in \
  'ERROR.*Invalid input data' \
  'ERROR.*Insufficient calibration data for intersection calculation' \
  'ERROR.*Failed to compute ring-wire intersections'
do
  N=$(grep -cE "$pattern" /run1321/dark.txt || true)
  if [ "$N" = "0" ]; then say "OK   no ERROR matching /$pattern/" ok
  else say "FAIL $N ERROR lines matching /$pattern/" no; fi
done

echo "=== 3. BOARD FAULTED names the stage and the camera ==="
grep -E 'BOARD FAULTED' /run1321/dark.txt | head -2 || true
if grep -qE 'BOARD FAULTED: camera [0-9]+ did not calibrate' /run1321/dark.txt; then
  say "OK   the vigil says which camera and that it was the calibration" ok
else say "FAIL BOARD FAULTED does not name the camera and the stage" no; fi
if grep -qE 'BOARD FAULTED.*cameras did not come up, or' /run1321/dark.txt; then
  say "FAIL BOARD FAULTED still offers both halves and commits to neither" no
else say "OK   it does not offer two alternatives" ok; fi

echo "=== 4. the control calibrates and says nothing new ==="
if grep -q 'Initial calibration completed successfully' /run1321/control.txt; then
  say "OK   the mocks calibrate" ok
else say "FAIL the mocks did not calibrate" no; fi
grep -E '^\[(ERROR|WARN)\]' /run1321/control.txt || true
NOISE=$(grep -cE '^\[(ERROR|WARN)\]' /run1321/control.txt || true)
if [ "$NOISE" = "0" ]; then say "OK   the control prints no ERROR and no WARN" ok
else say "FAIL the control prints $NOISE ERROR/WARN lines" no; fi

echo "CHECK_RC=$FAILED"
exit $FAILED
