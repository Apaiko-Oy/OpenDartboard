set -u
# #1345: what the stage that decides whether a camera has seen a dart is measuring.
#
# The issue read the rig's `STATE GUESS: CLEAN -> DART_1` on camera 1 alone as a
# sensitivity that is right by accident on the camera whose board was fitted too small.
# It is not: `processDartState` never reads the board, the motion region or #1339's
# denominator. Its figure is changed pixels over the WHOLE FRAME against 0.22% of it,
# and on this footage the camera that clears it has none of those pixels on its board.
#
# Nothing here asserts the defect stays. What it asserts is that the refused window's
# account (#1350) now carries the one number that makes its figures readable -- how much
# of each camera's figure was inside the board it is about -- and that adding it changed
# no decision on either rig. The census is echoed so a reader sees it without opening a
# file.

RIG=/app/mocks/rig-20260918
MOCKS=/app/mocks
CYCLES=1800

run() { # run <name> <cams>
  mkdir -p /run1345/$1 && cd /run1345/$1
  OD_MAX_CYCLES=$CYCLES OD_TRACE=/run1345/$1.csv \
    /app/build/opendartboard --cams "$2" --width 1280 --height 720 > /run1345/$1.out 2>&1
  sed 's/\x1b\[[0-9;]*m//g' /run1345/$1.out > /run1345/$1.txt
  cd /run1345
}

echo "--- the rig this issue was filed about, and the control ---"
run rig   "$RIG/cam_1.mp4,$RIG/cam_2.mp4,$RIG/cam_3.mp4"
run mocks "$MOCKS/cam_1.mp4,$MOCKS/cam_2.mp4,$MOCKS/cam_3.mp4"

FAILED=0
say() { echo "$1"; [ "$2" = ok ] || FAILED=1; }
votes() { grep -c 'STATE VOTE' /run1345/$1.txt || true; }
windows() { grep -c 'DART_PROCESSING' /run1345/$1.txt || true; }

echo
echo "=== 0. both runs calibrated, so every number below is about darts ==="
for r in rig mocks; do
  if grep -q 'Initial calibration completed successfully on 3 of 3' /run1345/$r.txt; then
    say "OK   $r calibrated 3 of 3" ok
  else say "FAIL $r did not calibrate 3 of 3, so it measures nothing" no; fi
done

echo
echo "=== 1. the control is quiet: mocks/cam_*.mp4 raises nothing new ==="
ERRS=$(grep -c '^\[ERROR\]' /run1345/mocks.txt || true)
if [ "$ERRS" = 0 ]; then say "OK   the shipped mocks produce no ERROR" ok
else
  grep '^\[ERROR\]' /run1345/mocks.txt | head -3
  say "FAIL the shipped mocks produce $ERRS ERROR lines" no
fi
for r in rig mocks; do
  if grep -q 'DART REGION' /run1345/$r.txt; then
    grep 'DART REGION' /run1345/$r.txt | head -3
    say "FAIL a $r camera could not say how much of its figure was on its board" no
  else say "OK   every $r camera can say how much of its figure was on its board" ok; fi
done

echo
echo "=== 2. a refused window still accounts for itself, and only a refused one ==="
# #1350's promise, which #1345 must not break: a window that scored keeps the blank line.
for r in rig mocks; do
  W=$(windows $r); V=$(votes $r)
  echo "$r: $V of $W completed windows were refused"
  if [ "$W" -gt 0 ] && [ "$V" -le "$W" ]; then
    say "OK   $r accounts for its refusals and no more" ok
  else say "FAIL $r printed $V accounts over $W windows" no; fi
done
# Four of the rig's six windows are refusals; the other two moved the state and keep
# #1350's blank line. A refusal is what #1345 is about, so there must be several.
if [ "$(votes rig)" -ge 4 ]; then
  say "OK   the rig refuses a dart in most of its windows, which is what #1345 is" ok
else say "FAIL the rig refused $(votes rig) windows; this footage refuses four of six" no; fi

echo
echo "=== 3. every refused camera's figure says how much of it was on the board ==="
# Every camera that answered is named with a figure, and every figure carries the share
# of it that was on the board. Counted as occurrences rather than lines: three cameras
# share one line.
SAID=$(grep -h 'STATE VOTE' /run1345/rig.txt /run1345/mocks.txt | grep -oE 'camera [0-9] said' | wc -l)
SHARE=$(grep -h 'STATE VOTE' /run1345/rig.txt /run1345/mocks.txt | grep -oE 'inside its own board' | wc -l)
echo "$SAID camera figures in the refused windows of both rigs, $SHARE of them saying what of it was on the board"
if [ "$SAID" -gt 0 ] && [ "$SAID" = "$SHARE" ]; then
  say "OK   no camera's figure is printed without what of it was on the board" ok
else say "FAIL $SAID camera figures and $SHARE board shares; a figure was printed with nowhere" no; fi

echo
echo "=== 4. and this is what the rig says, in its own words ==="
grep 'STATE VOTE' /run1345/rig.txt | sed 's/.*] - /  /'
if [ "$(votes mocks)" -gt 0 ]; then
  echo "--- the control's refusals ---"
  grep 'STATE VOTE' /run1345/mocks.txt | sed 's/.*] - /  /' | head -4
fi

echo
echo "=== 5. the reading the issue proposed, and what the footage says ==="
# A camera whose whole figure is off its own board is not evidence about a dart. On this
# rig that is the only camera that clears the threshold, which is why "the sensitivity is
# right by accident on one camera" is the wrong half of the sentence: the camera that
# fires is a false positive and the two that do not are correct.
OFF=$(grep -h 'STATE VOTE' /run1345/rig.txt | grep -oE 'none of it inside its own board' | wc -l)
echo "rig: $OFF camera-figures in refused windows had no changed pixel on the board at all"
if [ "$OFF" -ge 10 ]; then
  say "OK   the census is recorded and is the evidence #1345's report reads" ok
else say "FAIL only $OFF of the rig's camera figures were wholly off the board" no; fi

echo "CHECK_RC=$FAILED"
exit $FAILED
