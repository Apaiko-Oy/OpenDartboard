set -u
# #1282, inside the container: what the detector does when a clip runs out, and the
# control that says the end of a clip is not a board that has gone blind.
#
# Short footage is constructed rather than borrowed, the way #1317, #1321 and #1323
# construct theirs, and with #1317's own tool: at occlude 0 and blur 0 it is a plain copy
# of N frames from a stated second, so the clips are the mocks and nothing else. The full
# mocks are 4,300 frames apiece and reaching their end costs about seven minutes under
# --cpus=2; the mechanism under test is the last three cycles of a clip, so the tester
# buys those three cycles rather than the seven minutes.
#
# EVERY detector run here is bounded by `timeout`. #895's fault vigil means a board that
# cannot calibrate never exits and OD_MAX_CYCLES does not bound it -- and the second phase
# below deliberately blinds a board, which is exactly that state.
cd /app || exit 2
OUT=/run1282
FAIL=0
note() { if [ "$1" -eq 0 ]; then echo "ok   $2"; else echo "FAIL $2"; FAIL=$((FAIL + 1)); fi; }

echo "---- building #1317's footage tool and cutting three short clips ----"
g++ -std=c++17 -O1 -o "$OUT/cut" /app/testers/i1317_partial_footage.cpp \
  $(pkg-config --cflags --libs opencv4) || exit 2
for i in 1 2 3; do
  # From 4.0 s, so the dev build's own DEBUG_SEEK_VIDEO seek (3 s) still lands inside the
  # clip, and 300 frames -- enough to calibrate on and still end within a few seconds.
  "$OUT/cut" "/app/mocks/cam_$i.mp4" "$OUT/short_$i.avi" 4.0 0 0 300 || exit 2
done
CAMS="$OUT/short_1.avi,$OUT/short_2.avi,$OUT/short_3.avi"

echo
echo "---- 1. the clips run out ----"
( cd "$OUT" && timeout 300 /app/build/opendartboard --cams "$CAMS" --width 1280 --height 720 \
    > "$OUT/eof.out" 2> "$OUT/eof.err" ); RC=$?
BYTES=$(wc -c < "$OUT/eof.out")
echo "     detector rc=$RC stdout_bytes=$BYTES stdout_lines=$(wc -l < "$OUT/eof.out")"
[ "$RC" != 124 ]; note $? "the run ended on its own rather than on the tester's timeout"
[ "$RC" = 0 ]; note $? "  and ended cleanly (rc=$RC)"
# The bound is generous by two orders of magnitude and still refuses the defect: the
# measured figure on this tree is a few tens of kilobytes, and what this issue is about
# wrote about 200 MB. A bound that tracks the real figure would fail on a build that
# logs one more line per cycle for some unrelated reason.
[ "$BYTES" -lt 2000000 ]; note $? "  and wrote under 2 MB of stdout ($BYTES bytes)"
[ "$(grep -c 'END OF FOOTAGE cam=' "$OUT/eof.out")" = 3 ]; note $? "each of the three clips said its own end, once"
grep -q 'END OF FOOTAGE: every file source has reached its end' "$OUT/eof.out"
note $? "and the scorer said the run was over because of it"
# The reopen #899's recovery makes rewinds a file to frame 0, so a second census of the
# cameras is the replay this issue is about. There must be exactly one.
# The count is taken off a colour-stripped copy: log_string() wraps the number in an
# ANSI sequence, so "Initializing 3 cameras" matches nothing on the raw stream.
sed 's/\x1b\[[0-9;]*m//g' "$OUT/eof.out" > "$OUT/eof.plain"
[ "$(grep -c 'Initializing 3 cameras' "$OUT/eof.plain")" = 1 ]; note $? "the sources were opened once: the clips were not replayed"
grep -q 'BOARD SIGHT LOST' "$OUT/eof.out" && note 1 "the end of a clip was not reported as the board going blind" \
  || note 0 "the end of a clip was not reported as the board going blind"

echo
echo "---- 2. the control: a board made blind is NOT a board whose footage ended ----"
# The same clips, the same binary, ended by the cycle budget rather than by the footage.
# #798's injection takes every slot and keeps it, which is the state that read() answers
# false for and that must never be read as a clip running out.
# 250 blind cycles: the dev build paces a cycle at 16.7 ms, and kSightLostAfterSeconds is
# three SECONDS rather than a number of cycles, so the budget has to buy the wall clock.
( cd "$OUT" && OD_MAX_CYCLES=350 OD_BLIND_AFTER=100 timeout 300 /app/build/opendartboard \
    --cams "$CAMS" --width 1280 --height 720 > "$OUT/blind.out" 2> "$OUT/blind.err" ); RC=$?
echo "     detector rc=$RC stdout_bytes=$(wc -c < "$OUT/blind.out")"
[ "$RC" != 124 ]; note $? "the blind run ended on the cycle budget rather than on the timeout"
grep -q 'BOARD SIGHT LOST' "$OUT/blind.out"; note $? "the board went blind and said so"
grep -q 'END OF FOOTAGE' "$OUT/blind.out" && note 1 "and NOTHING called it the end of the footage" \
  || note 0 "and NOTHING called it the end of the footage"
# The positive control on the needle: the clips really were still running when it blinded,
# so "no END OF FOOTAGE" is an absence over something that was there to be found.
[ "$(grep -c 'CAPDROP' "$OUT/blind.out")" -gt 0 ]; note $? "  and the slots really were empty (CAPDROP was reported), so the absence is over something"

echo
echo "$FAIL failed"
exit $([ "$FAIL" -eq 0 ] && echo 0 || echo 1)
