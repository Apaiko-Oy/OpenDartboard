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
#   1. The rig still calibrates 3 of 3 and prints no ERROR and no WARN, and its three
#      per-camera numbers are pinned in the new terms (#1478 dropped the mocks' three).
#      A gate nothing passes is not a gate, and #1340's census is what these numbers
#      have to stay consistent with.
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
# #1478: the shipped mocks are §5's input and nothing else's. §1, §2 and §4 read the rig
# (docs/shipped-mock-census.md, A1); §5 is built from the mocks' own bulls and has no rig
# half, so it is left on them.
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
/run1392/closer /app/mocks/cam_1.mp4 /run1392/c1.avi 616 283 720 720 200 2> /run1392/c1.made || { cat /run1392/c1.made; exit 1; }
cat /run1392/c1.made
/run1392/closer /app/mocks/cam_2.mp4 /run1392/c2.avi 651 313 720 720 200 || exit 1
/run1392/closer /app/mocks/cam_3.mp4 /run1392/c3.avi 654 293 720 720 200 || exit 1

echo
echo "=== 1. the rig calibrates 3 of 3, and here is what it measures ================="
OD_MAX_CYCLES=20 timeout 90 /app/build/opendartboard --debug --cams "$RIG" \
  --width 1280 --height 720 > /run1392/rig.out 2>&1 || true
plain /run1392/rig.out /run1392/rig.txt
echo "mocks/rig-20260918:" ; sights /run1392/rig.txt   | sed 's/^/    camera /'
for f in rig; do
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
# #1729: camera 1 was 23.6% of a 195 px span. 7e0ca67 ("the board is what surrounds the
# rest of the board") sizes rig camera 1 off its broken doubles ring, 317 px, so the same
# 28129 ring pixels are 8.9% of a disc 2.6x the size; cameras 2 and 3 key no doubles ring
# and still span their treble ring. Re-measured on fork 61f9bcb's dev build.
check_ring /run1392/rig.txt   1 8.9
check_ring /run1392/rig.txt   2 26.9
check_ring /run1392/rig.txt   3 25.7
# #1478: a cross-fixture control stood here -- the rig's ring share against the shipped
# mocks' camera 3, which read under half of it because the rig's colour mask is its TREBLE
# ring (#1378). It went with the mocks' column; the rig's three rows above are pinned to
# +/-0.6 and are what now goes red if the span moves.

echo
echo "=== 2. the face is refused by name, and so is a grey wall ======================="
OD_MAX_CYCLES=20 timeout 90 /app/build/opendartboard \
  --cams /run1392/face.avi,/app/mocks/rig-20260918/cam_1.mp4,/app/mocks/rig-20260918/cam_2.mp4 \
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
if [ "$RC" = "0" ]; then say "OK   the gate is scale-free on the rig cameras' own numbers" ok
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
  --cams /run1392/face.avi,/app/mocks/rig-20260918/cam_1.mp4,/app/mocks/rig-20260918/cam_2.mp4 \
  --width 1280 --height 720 > /run1392/faceframe.out 2>&1 || true
OD_LOOK=frame OD_MAX_CYCLES=20 timeout 90 /app/build/opendartboard --debug --cams "$RIG" \
  --width 1280 --height 720 > /run1392/rigframe.out 2>&1 || true
plain /run1392/faceframe.out /run1392/faceframe.txt
plain /run1392/rigframe.out /run1392/rigframe.txt
grep -oE 'Camera 1 .*skin in warm room light is most of one' /run1392/faceframe.txt | head -1 || true
if grep -qE 'Camera 1 .*is not looking at the dartboard: [0-9]+% of its frame keys as dartboard red or green and this check allows at most 12%' /run1392/faceframe.txt; then
  say "OK   under OD_LOOK=frame the refusal is #1318's own sentence, at its own 12% line" ok
else say "FAIL OD_LOOK=frame did not restore the pre-#1392 sentence" no; fi
if grep -q 'Initial calibration completed successfully on 3 of 3 cameras' /run1392/rigframe.txt; then
  say "OK   and the rig calibrates 3 of 3 under the old measure too, so the switch is a before/after" ok
else say "FAIL the rig does not calibrate under OD_LOOK=frame, so the falsifier is refusing everything" no; fi

echo
echo "=== 5. the same three boards behind a longer lens ==============================="
# 720x720 about each bull: the same board, the same room, the same darts, filling more of
# its own frame. The ring's share of its board circle must not move; its share of the
# frame must. The frame's own ceiling moves too -- a square frame lets a whole board
# account for 78.5% of it against 44.2% at 16:9 -- which is why the flood line is derived
# from the frame rather than being a constant.
# #1478: NOT re-pointed. The crops are about the shipped mocks' own bulls and the far
# reading is the mocks at 1280x720, which this section now takes for itself because §1 no
# longer runs them. A rig version needs its crops measured, and is reported rather than made.
OD_MAX_CYCLES=20 timeout 90 /app/build/opendartboard --debug --cams "$MOCKS" \
  --width 1280 --height 720 > /run1392/mocks.out 2>&1 || true
plain /run1392/mocks.out /run1392/mocks.txt
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
if grep -q 'Initial calibration completed successfully on 3 of 3 cameras' /run1392/closer.txt; then
  say "OK   all three cropped boards calibrate at 720x720 (camera 1 on a further look, below)" ok
else say "FAIL the cropped boards did not all calibrate" no; fi
# #1732: camera 1's averaged frame IS refused by ADR-0079 §2 at this crop -- but its board
# does not leave the picture. Its doubles ring is wholly in shot (right edge near x=648 of
# 720); what reaches the frame edge is the surround's red logo and stripe, joined to the
# board on most frames and not on some. So a further look (#1445) reads the true margin
# (63-69 px) and calibrates, and what is asserted is that the look it seals is the far
# reading's own board, moved by the crop's origin -- not that it is refused.
if grep -qE '^\[ERROR\].*Camera 1 .*is not looking at a WHOLE dartboard' /run1392/closer.txt; then
  say "OK   camera 1's averaged frame is refused by ADR-0079 §2 at this crop, the surround's red at the edge" ok
else say "FAIL camera 1's averaged frame at 720x720 is not refused by ADR-0079 §2, so this crop is not the case #1732 measured" no; fi
# The control is camera 1 of the mocks at 1280x720, sealed in this section's own run; the
# crop's origin is what i1392_closer_footage printed. Tolerances, MEASURED on fork 9c9deec
# (2026-10-02): bull (360,284) against (360,283), so 1 px, held to 3 px per axis; angle
# 17.76 against 17.68, 0.08 deg, held to 0.5; radius 245.29 against 245.26, held to 2.5 px
# (the same tolerances 1331 §5 holds its edge clip to). wedge20 must be identical.
sealed1() { grep -hoE 'GEOMETRY SEALED: camera 1 index=0 scoring=1 bull=[0-9]+,[0-9]+ star=[0-9]+ read=[0-9]+ wedge20=-?[0-9]+ angle=-?[0-9.]+ radius=[0-9.]+' "$1" | head -1 \
  | sed -E 's/.*bull=([0-9]+),([0-9]+) .*wedge20=(-?[0-9]+) angle=(-?[0-9.]+) radius=([0-9.]+)/\1 \2 \3 \4 \5/'; }
ORIGIN=$(sed -nE 's/.* at \((-?[0-9]+),(-?[0-9]+)\) into .*/\1 \2/p' /run1392/c1.made | head -1)
NEAR_SEAL=$(sealed1 /run1392/closer.txt); FAR_SEAL=$(sealed1 /run1392/mocks.txt)
echo "    camera 1 crop origin: ${ORIGIN:-unknown}; sealed at 720x720: ${NEAR_SEAL:-nothing}; at 1280x720: ${FAR_SEAL:-nothing}"
if [ -n "$ORIGIN" ] && [ -n "$NEAR_SEAL" ] && [ -n "$FAR_SEAL" ] && python3 - "$NEAR_SEAL" "$FAR_SEAL" "$ORIGIN" <<'CHECK'
import sys
n = sys.argv[1].split(); f = sys.argv[2].split(); o = sys.argv[3].split()
ex, ey = int(f[0]) - int(o[0]), int(f[1]) - int(o[1])
bad = []
if abs(int(n[0]) - ex) > 3 or abs(int(n[1]) - ey) > 3: bad.append(f"bull ({n[0]},{n[1]}) where the far board moved by the crop is ({ex},{ey}), over 3 px")
if n[2] != f[2]: bad.append(f"wedge20 {n[2]} where the far reading's is {f[2]}")
if abs(float(n[3]) - float(f[3])) > 0.5: bad.append(f"angle {n[3]} against {f[3]}, over 0.5 deg")
if abs(float(n[4]) - float(f[4])) > 2.5: bad.append(f"radius {n[4]} against {f[4]}, over 2.5 px")
print("    " + ("; ".join(bad) if bad else f"bull ({n[0]},{n[1]}) vs ({ex},{ey}), wedge20 {n[2]}, angle {n[3]} vs {f[3]}, radius {n[4]} vs {f[4]}"))
sys.exit(1 if bad else 0)
CHECK
then
  say "OK   camera 1 at 720x720 seals the far reading's board moved by the crop -- bull, wedge20, angle and radius" ok
else
  say "FAIL camera 1 at 720x720 sealed something other than the far reading's board moved by the crop (or nothing)" no
fi

echo
echo "CHECK_RC=$FAILED"
exit $FAILED
