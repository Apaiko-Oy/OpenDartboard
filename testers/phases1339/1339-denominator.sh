set -u
# #1339: what the motion ratio is a fraction of.
#
# Two rigs are two rooms as well as two lenses, so "the rig sees darts now" cannot on its
# own tell a denominator that is scale-free from one that happens to suit the second room.
# The controlled version is here: ONE clip, every dart and every arm in it unchanged, and
# the single property this issue is about -- how much of the frame the board fills -- moved
# by a known factor. A ratio over the board does not move with it. A ratio over the frame
# falls with the square of it, which is the whole defect.
#
# The scaled clip is made from mocks/cam_*.mp4 rather than from the rig, and the reason is
# a measurement: rig-20260918's largest red/green region is 39,378 px against
# bull_processing's floor of 36,864 (4% of the frame), so that rig is 1.07x from not
# calibrating at all and nothing smaller than it can be made from it. The shipped mocks
# have room, and the rig is run beside it at its own size as the footage the issue is about.

RIG=/app/mocks/rig-20260918
MOCKS=/app/mocks
CYCLES=1500

echo "--- the same footage with the board smaller in the frame ---"
g++ -std=c++17 -O2 -o /run1339/scaled /app/testers/i1339_scaled_footage.cpp \
  $(pkg-config --cflags --libs opencv4) || exit 1
mkdir -p /run1339/small
for n in 1 2 3; do
  /run1339/scaled $MOCKS/cam_$n.mp4 /run1339/small/cam_$n.avi 0.6 1750 || exit 1
done

run() { # run <name> <denominator> <cams>
  mkdir -p /run1339/$1 && cd /run1339/$1
  OD_MAX_CYCLES=$CYCLES OD_TRACE=/run1339/$1.csv OD_MOTION_DENOMINATOR=$2 \
    /app/build/opendartboard --cams "$3" --width 1280 --height 720 > /run1339/$1.out 2>&1
  sed 's/\x1b\[[0-9;]*m//g' /run1339/$1.out > /run1339/$1.txt
  cd /run1339
}

echo "--- the board's own area, at both sizes ---"
run big_board   board "$MOCKS/cam_1.mp4,$MOCKS/cam_2.mp4,$MOCKS/cam_3.mp4"
run small_board board "/run1339/small/cam_1.avi,/run1339/small/cam_2.avi,/run1339/small/cam_3.avi"
echo "--- the whole frame, at both sizes: what this issue is about ---"
run big_frame   frame "$MOCKS/cam_1.mp4,$MOCKS/cam_2.mp4,$MOCKS/cam_3.mp4"
run small_frame frame "/run1339/small/cam_1.avi,/run1339/small/cam_2.avi,/run1339/small/cam_3.avi"
echo "--- and the rig the issue was filed about, both ways ---"
run rig_board   board "$RIG/cam_1.mp4,$RIG/cam_2.mp4,$RIG/cam_3.mp4"
run rig_frame   frame "$RIG/cam_1.mp4,$RIG/cam_2.mp4,$RIG/cam_3.mp4"

# What a dart does, in the unit the thresholds are written in: the mean of the five
# highest cycle intensities in the run. One cycle would be one arm.
peak() { awk -F, 'NR>1 {print $5}' /run1339/$1.csv | sort -rn | head -5 \
         | awk '{s+=$1; n++} END {printf "%.4f", n ? s/n : 0}'; }
events() { grep -c 'DART_PROCESSING' /run1339/$1.txt || true; }
region() { grep -oE 'board, the [0-9]+x[0-9]+ px ellipse[^:]*: [0-9]+ px' /run1339/$1.txt \
           | grep -oE ': [0-9]+ px' | grep -oE '[0-9]+' | head -1; }

FAILED=0
say() { echo "$1"; [ "$2" = ok ] || FAILED=1; }
# The parentheses are load-bearing: `printf "%.3f", b > 0 ? ...` is a redirection to a
# file named by the rest of the line, and awk says nothing about it.
ratio() { awk -v a="$1" -v b="$2" 'BEGIN {printf("%.3f", (b > 0 ? a / b : -1))}'; }
within() { awk -v v="$1" -v lo="$2" -v hi="$3" 'BEGIN {exit (v >= lo && v <= hi) ? 0 : 1}'; }

echo
echo "=== 0. every run calibrated, so every number below is about motion ==="
for r in big_board small_board big_frame small_frame rig_board rig_frame; do
  if grep -q 'Initial calibration completed successfully' /run1339/$r.txt; then
    say "OK   $r calibrated" ok
  else say "FAIL $r did not calibrate, so it measures nothing" no; fi
done

echo
echo "=== 1. the property really moved: the board fills less of the scaled frame ==="
BIGR=$(region big_board); SMALLR=$(region small_board)
AREA=$(ratio "${SMALLR:-0}" "${BIGR:-0}")
echo "board region: $BIGR px at full size, $SMALLR px at 0.6x -> $AREA of it (0.6^2 = 0.36)"
if within "$AREA" 0.30 0.42; then say "OK   the board is 0.36x the area it was, so the scaled clip is the experiment" ok
else say "FAIL the scaled clip's board is $AREA of the original, not near 0.36" no; fi

echo
echo "=== 2. over the board, a dart measures the same at both sizes ==="
BB=$(peak big_board); SB=$(peak small_board)
R1=$(ratio "$SB" "$BB")
echo "top-5 intensity: $BB at full size, $SB at 0.6x -> $R1"
if within "$R1" 0.70 1.40; then say "OK   the board denominator is scale-free: 0.36x the board area, the same ratio" ok
else say "FAIL the board denominator moved by $R1 when only the framing changed" no; fi

echo
echo "=== 3. over the frame, the same dart measures a third of itself ==="
BF=$(peak big_frame); SF=$(peak small_frame)
R2=$(ratio "$SF" "$BF")
echo "top-5 intensity: $BF at full size, $SF at 0.6x -> $R2"
if within "$R2" 0.20 0.60; then say "OK   the frame denominator falls with the area, which is the defect #1339 is" ok
else say "FAIL the frame denominator moved by $R2; #1339's premise is not reproduced here" no; fi

echo
echo "=== 4. and that is the difference between seeing darts and not ==="
EB=$(events small_board); EF=$(events small_frame)
echo "dart events on the scaled clip: $EB over the board, $EF over the frame"
if [ "$EB" -gt "$EF" ] && [ "$EB" -ge 5 ]; then
  say "OK   the small board scores $EB events where the frame denominator finds $EF" ok
else say "FAIL the small board found $EB events against the frame's $EF" no; fi

echo
echo "=== 5. the shipped fixture does not regress ==="
MB=$(events big_board); MF=$(events big_frame)
echo "dart events on mocks/cam_*.mp4 in $CYCLES cycles: $MB over the board, $MF over the frame"
if [ "$MB" -ge 10 ] && [ "$MB" -le $((MF * 2)) ]; then
  say "OK   the board denominator neither collapses nor explodes the control ($MB against $MF)" ok
else say "FAIL the control moved from $MF events to $MB" no; fi

echo
echo "=== 6. the rig this issue was filed about ==="
RB=$(events rig_board); RF=$(events rig_frame)
RBP=$(peak rig_board); RFP=$(peak rig_frame)
echo "rig-20260918 in $CYCLES cycles: $RB events over the board, $RF over the frame"
echo "rig-20260918 top-5 intensity: $RBP over the board, $RFP over the frame"
if [ "$RB" -gt "$RF" ]; then say "OK   the rig sees darts it did not see, and putting the frame back takes them away" ok
else say "FAIL the rig found $RB events over its board against $RF over the frame" no; fi

echo "CHECK_RC=$FAILED"
exit $FAILED
