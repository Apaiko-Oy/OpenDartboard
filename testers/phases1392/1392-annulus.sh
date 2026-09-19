set -u
# #1392: a camera is refused on evidence that does not move when the same board is mounted
# closer -- measured, on the binary in build/, on both fixtures.
#
# The defect: `board_look` refused a whole camera as "not looking at the dartboard" on
# countNonZero(masks.doublesMask) over masks.doublesMask.total(), and a Mat's total() is
# its full size whether or not anything in it is masked, so the denominator was the frame.
# Ring pixels go as the square of how much of the frame a board fills, so the check was a
# statement about where somebody bolted the camera.
#
# EVERY detector run below is under `timeout`. A board that cannot calibrate goes to
# #895's fault vigil and stays up on purpose, so a phase expecting a refusal and not
# bounding it never returns; #1340's agent lost nine minutes to that on 2026-09-19.
#
# What is asserted, and why each part is here:
#
#   1. Both fixtures still calibrate 3 of 3 and print no ERROR and no WARN, and the six
#      per-camera numbers are pinned in the new terms. A gate nothing passes is not a
#      gate, and #1340's census is what these numbers have to stay consistent with.
#   2. The face is still refused, by name, with its number on the wrong side of the
#      threshold it is printed against (#1321), and the grey wall beside it -- because a
#      check that only ever refuses coloured things has not been shown to refuse anything
#      else. The two board cameras beside the face still calibrate.
#   3. THE ISSUE, exactly, on synthetic evidence: the same camera's numbers with every
#      length multiplied. Footage cannot answer this -- resampling degrades the colour
#      mask and cropping changes the frame's aspect -- and the question is pure
#      arithmetic, so it is asked as arithmetic. testers/i1392_look_check.cpp.
#   4. THE FALSIFIER, on the SAME binary: OD_LOOK=frame restores the pre-#1392 measure,
#      its 12% line and its sentence word for word, and is held to numbers the old stage
#      really produced -- #1318's own 237,036-pixel webcam at 25% against 12%, and both
#      fixtures' own frame shares. A switch that only refuses passes any test asking it to
#      refuse.
#   5. THE SAME BOARD BEHIND A LONGER LENS, on footage: mocks/cam_*.mp4 cropped to 720x720
#      about each bull. The ring's share of its own board circle does not move; its share
#      of the frame nearly doubles. That is the whole claim, on real pixels.

FAILED=0
say() { echo "$1"; [ "$2" = ok ] || FAILED=1; }
plain() { sed 's/\x1b\[[0-9;]*m//g' "$1" > "$2"; }

RIG=/app/mocks/rig-20260918/cam_1.mp4,/app/mocks/rig-20260918/cam_2.mp4,/app/mocks/rig-20260918/cam_3.mp4
MOCKS=/app/mocks/cam_1.mp4,/app/mocks/cam_2.mp4,/app/mocks/cam_3.mp4

# "ring 12.2% of a 266408 px board circle, 3.5% of the frame", per camera, one line each.
sights() {
  grep -oE 'Camera [0-9]+ sight: .*' "$1" \
    | sed -E 's/Camera ([0-9]+) sight: flood=([0-9]+) of frame ([0-9x]+) \(([0-9.]+)%.*ring=([0-9]+) of board disc ([0-9]+) at span ([0-9]+) px \(([0-9.]+)%; of the frame it would be ([0-9.]+)%\).*/\1 flood=\4% ring=\8% ringOfFrame=\9% span=\7/' \
    | sort -u
}
ringshare() { # $1 file, $2 camera -- the ring's share of its own board circle
  grep -oE "Camera $2 sight: .*" "$1" | sed -E 's/.*at span [0-9]+ px \(([0-9.]+)%;.*/\1/' | head -1
}
framesharen() { # $1 file, $2 camera -- what that same ring is as a share of the frame
  grep -oE "Camera $2 sight: .*" "$1" | sed -E 's/.*of the frame it would be ([0-9.]+)%.*/\1/' | head -1
}

echo "--- the fixtures this phase makes for itself ---"
g++ -std=c++17 -O1 -o /run1392/make_source /app/testers/i1318_make_source.cpp \
  $(pkg-config --cflags --libs opencv4) || exit 1
g++ -std=c++17 -O1 -o /run1392/closer /app/testers/i1392_closer_footage.cpp \
  $(pkg-config --cflags --libs opencv4) || exit 1
g++ -std=c++17 -O1 -o /run1392/look_check /app/testers/i1392_look_check.cpp || exit 1
/run1392/make_source face /run1392/face.avi 1280 720 15 120 || exit 1
/run1392/make_source wall /run1392/wall.avi 1280 720 15 120 || exit 1
# A longer lens on the same three boards. 720x720 is the tightest square in which all the
# colour camera 2 and camera 3 keep is still inside the picture -- ADR-0079 §2 refuses one
# step further in, and camera 1 is already at the edge there, which §5 reports rather than
# hides. The centres are the three bulls i1340's tester pins.
/run1392/closer /app/mocks/cam_1.mp4 /run1392/c1.avi 616 283 720 720 200 || exit 1
/run1392/closer /app/mocks/cam_2.mp4 /run1392/c2.avi 651 313 720 720 200 || exit 1
/run1392/closer /app/mocks/cam_3.mp4 /run1392/c3.avi 654 293 720 720 200 || exit 1

echo
echo "=== 1. both fixtures calibrate 3 of 3, and here is what they measure ============"
OD_MAX_CYCLES=20 timeout 90 /app/build/opendartboard --debug --cams "$MOCKS" \
  --width 1280 --height 720 > /run1392/mocks.out 2>&1 || true
OD_MAX_CYCLES=20 timeout 90 /app/build/opendartboard --debug --cams "$RIG" \
  --width 1280 --height 720 > /run1392/rig.out 2>&1 || true
plain /run1392/mocks.out /run1392/mocks.txt
plain /run1392/rig.out /run1392/rig.txt
echo "mocks/cam_*.mp4:"    ; sights /run1392/mocks.txt | sed 's/^/    camera /'
echo "mocks/rig-20260918:" ; sights /run1392/rig.txt   | sed 's/^/    camera /'
for f in mocks rig; do
  if grep -q 'Initial calibration completed successfully on 3 of 3 cameras' /run1392/$f.txt; then
    say "OK   $f calibrates on all three cameras" ok
  else say "FAIL $f did not calibrate on three cameras" no; fi
  NOISE=$(grep -cE '^\[(ERROR|WARN)\]' /run1392/$f.txt || true)
  grep -E '^\[(ERROR|WARN)\]' /run1392/$f.txt || true
  if [ "$NOISE" = "0" ]; then say "OK   $f prints no ERROR and no WARN" ok
  else say "FAIL $f prints $NOISE ERROR/WARN lines" no; fi
done
# The six numbers themselves. They are pinned because the whole argument of this issue is
# that one of them is a property of the board and the other of the mounting, and a table
# that drifts silently proves neither. A tenth of a percent either way is allowed; the
# clip is seeked into, so the frame calibration lands on is not identical run to run.
check_ring() { # file camera expected-percent
  local got; got="$(ringshare "$1" "$2")"
  if [ -n "$got" ] && python3 -c "import sys; sys.exit(0 if abs($got-$3)<=0.6 else 1)"; then
    say "OK   $(basename $1 .txt) camera $2: the ring is $got% of the circle its board spans (expected $3%)" ok
  else say "FAIL $(basename $1 .txt) camera $2: the ring is ${got:-nothing}% of its board circle, expected $3%" no; fi
}
check_ring /run1392/mocks.txt 1 12.2
check_ring /run1392/mocks.txt 2 11.1
check_ring /run1392/mocks.txt 3 14.7
check_ring /run1392/rig.txt   1 23.6
check_ring /run1392/rig.txt   2 26.9
check_ring /run1392/rig.txt   3 25.7
# ... and the two fixtures disagree about it by more than twice, which is the positive
# control for the table above: the rig's colour mask is its TREBLE ring (#1378), so its
# span is 0.78 of its board and the same rings read 15.8/0.78^2 = 26% of it. If these two
# rows ever read the same, the span has stopped being what #1340 measured.
M3=$(ringshare /run1392/mocks.txt 3); R2=$(ringshare /run1392/rig.txt 2)
if [ -n "$M3" ] && [ -n "$R2" ] && python3 -c "import sys; sys.exit(0 if $R2 > $M3 * 1.5 else 1)"; then
  say "OK   the rig reads $R2% where the mocks read $M3%, which is the span being 0.78 of its board" ok
else say "FAIL the two fixtures no longer disagree about the ring share, so the span has moved" no; fi

echo
echo "=== 2. the face is refused by name, and so is a grey wall ======================="
OD_MAX_CYCLES=20 timeout 90 /app/build/opendartboard \
  --cams /run1392/face.avi,/app/mocks/cam_1.mp4,/app/mocks/cam_2.mp4 \
  --width 1280 --height 720 > /run1392/face.out 2>&1 || true
timeout 40 /app/build/opendartboard --cams /run1392/wall.avi,/run1392/wall.avi,/run1392/wall.avi \
  --width 1280 --height 720 > /run1392/wall.out 2>&1 || true
plain /run1392/face.out /run1392/face.txt
plain /run1392/wall.out /run1392/wall.txt
grep -E '^\[ERROR\]\[GEOMETRY_CALIBRATION\] - Camera' /run1392/face.txt | cut -c1-260 || true
if grep -qE 'Camera 1 .*is not looking at the dartboard: [0-9.]+% of its whole picture keys as dartboard red or green and the biggest board that fits in a 1280x720 frame could account for at most [0-9.]+%' /run1392/face.txt; then
  say "OK   camera 1 is named, with what it measured and what a whole board could account for" ok
else say "FAIL no line names camera 1 and what it failed on" no; fi
# #1321's rule. A camera refused for holding LESS colour than the check allows did not
# fail on colour, and a line nothing can falsify is not evidence.
BAD=$(grep -oE '[0-9.]+% of its whole picture keys.*this check allows [0-9.]+%' /run1392/face.txt \
  | tr -d '%' | awk '{ if ($1 <= $NF) print }' | wc -l)
if [ "$BAD" = "0" ]; then say "OK   every share printed is above the line it is printed against" ok
else say "FAIL $BAD refusals print a share that is not above their own line" no; fi
ERRS=$(grep -cE '^\[ERROR\]\[GEOMETRY_CALIBRATION\] - Camera' /run1392/face.txt || true)
if [ "$ERRS" = "1" ]; then say "OK   one calibration ERROR, for the one camera that earned it" ok
else say "FAIL $ERRS calibration ERROR lines for one refused camera" no; fi
if grep -q 'Initial calibration completed successfully on 2 of 3 cameras' /run1392/face.txt \
   && grep -qE 'CAMERAS: 2 of 3 are looking at the dartboard \(2,3\); refused: 1' /run1392/face.txt; then
  say "OK   the two board cameras beside the face still calibrate, and are named" ok
else say "FAIL the board did not calibrate on the two cameras that can see" no; fi
if grep -q 'CAMERAS: 0 of 3 are looking at the dartboard; refused: 1,2,3' /run1392/wall.txt; then
  say "OK   a grey wall is refused on all three, so this check refuses more than red things" ok
else say "FAIL a grey office wall was not refused" no; fi

echo
echo "=== 3. the issue itself: the same camera, mounted closer ========================"
# Arithmetic, because footage cannot ask it cleanly. i1392_look_check.cpp says why.
/run1392/look_check; RC=$?
if [ "$RC" = "0" ]; then say "OK   the gate is scale-free on the six cameras' own numbers" ok
else say "FAIL the gate is not what testers/i1392_look_check.cpp says it is (rc=$RC)" no; fi

echo
echo "=== 4. the falsifier restores the old stage, not merely a refusal ==============="
# The same program under OD_LOOK=frame: the old measure refuses this board camera for
# being mounted closer, admits it where it is, and reproduces #1318's own sentence and
# numbers on #1318's own webcam.
OD_LOOK=frame /run1392/look_check; RC=$?
if [ "$RC" = "0" ]; then say "OK   OD_LOOK=frame is the pre-#1392 measure, held to #1318's own numbers" ok
else say "FAIL OD_LOOK=frame is not the old stage (rc=$RC)" no; fi
# ... and on footage, in the old words, with the frame shares the old code really printed.
OD_LOOK=frame OD_MAX_CYCLES=20 timeout 90 /app/build/opendartboard \
  --cams /run1392/face.avi,/app/mocks/cam_1.mp4,/app/mocks/cam_2.mp4 \
  --width 1280 --height 720 > /run1392/faceframe.out 2>&1 || true
OD_LOOK=frame OD_MAX_CYCLES=20 timeout 90 /app/build/opendartboard --debug --cams "$MOCKS" \
  --width 1280 --height 720 > /run1392/mocksframe.out 2>&1 || true
plain /run1392/faceframe.out /run1392/faceframe.txt
plain /run1392/mocksframe.out /run1392/mocksframe.txt
grep -oE 'Camera 1 .*skin in warm room light is most of one' /run1392/faceframe.txt | head -1 || true
if grep -qE 'Camera 1 .*is not looking at the dartboard: [0-9]+% of its frame keys as dartboard red or green and this check allows at most 12%' /run1392/faceframe.txt; then
  say "OK   under OD_LOOK=frame the refusal is #1318's own sentence, at its own 12% line" ok
else say "FAIL OD_LOOK=frame did not restore the pre-#1392 sentence" no; fi
if grep -q 'Initial calibration completed successfully on 3 of 3 cameras' /run1392/mocksframe.txt; then
  say "OK   and the shipped fixture calibrates 3 of 3 under the old measure too, so the switch is a before/after" ok
else say "FAIL the shipped fixture does not calibrate under OD_LOOK=frame, so the falsifier is refusing everything" no; fi

echo
echo "=== 5. the same three boards behind a longer lens ==============================="
# 720x720 about each bull: the same board, the same room, the same darts, filling more of
# its own frame. The ring's share of its board circle must not move; its share of the
# frame must. The frame's own ceiling moves too -- a square frame lets a whole board
# account for 78.5% of it against 44.2% at 16:9 -- which is why the flood line is derived
# from the frame rather than being a constant.
OD_MAX_CYCLES=20 timeout 90 /app/build/opendartboard --debug \
  --cams /run1392/c1.avi,/run1392/c2.avi,/run1392/c3.avi \
  --width 720 --height 720 > /run1392/closer.out 2>&1 || true
plain /run1392/closer.out /run1392/closer.txt
echo "mocks at 1280x720:" ; sights /run1392/mocks.txt  | sed 's/^/    camera /'
echo "the same boards at 720x720:" ; sights /run1392/closer.txt | sed 's/^/    camera /'
for c in 2 3; do
  FAR_RING=$(ringshare /run1392/mocks.txt $c);  NEAR_RING=$(ringshare /run1392/closer.txt $c)
  FAR_FRAME=$(framesharen /run1392/mocks.txt $c); NEAR_FRAME=$(framesharen /run1392/closer.txt $c)
  if [ -z "$NEAR_RING" ] || [ -z "$FAR_RING" ]; then
    say "FAIL camera $c has no reading at one of the two distances, so nothing was compared" no
    continue
  fi
  echo "    camera $c: board circle $FAR_RING% -> $NEAR_RING% ; frame $FAR_FRAME% -> $NEAR_FRAME%"
  if python3 -c "import sys; sys.exit(0 if abs($NEAR_RING-$FAR_RING) <= 1.0 else 1)"; then
    say "OK   camera $c: the ring is $NEAR_RING% of its board circle where it was $FAR_RING%, within a point" ok
  else say "FAIL camera $c: the board-relative share moved from $FAR_RING% to $NEAR_RING%" no; fi
  if python3 -c "import sys; sys.exit(0 if $NEAR_FRAME >= $FAR_FRAME * 1.5 else 1)"; then
    say "OK   camera $c: the SAME ring is $NEAR_FRAME% of the frame where it was $FAR_FRAME% -- the number this issue removed" ok
  else say "FAIL camera $c: the frame share did not move, so this clip is not a longer lens" no; fi
done
if grep -q 'Initial calibration completed successfully on 2 of 3 cameras' /run1392/closer.txt; then
  say "OK   the two boards that are still wholly in shot calibrate at 720x720" ok
else say "FAIL the cropped boards did not calibrate" no; fi
# Camera 1's board really does leave its picture at this crop, and ADR-0079 §2 is what
# says so. It is asserted rather than tolerated, because a crop that clipped all three
# would make every number above meaningless.
if grep -qE 'Camera 1 .*is not looking at a WHOLE dartboard' /run1392/closer.txt; then
  say "OK   camera 1's board leaves its picture at this crop, and is refused for that and not for colour" ok
else say "FAIL camera 1 at 720x720 is not refused by ADR-0079 §2, so this crop is not what it says" no; fi

echo
echo "CHECK_RC=$FAILED"
exit $FAILED
