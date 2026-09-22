set -u
FAILED=0
say() { echo "$1"; [ "${2:-no}" = ok ] || FAILED=1; }
CVFLAGS="$(pkg-config --cflags --libs opencv4)"

# One camera, one clip, ended by its own recorded pid and never by pattern. The subshell
# execs the detector so the pid recorded here is the detector's and not a shell's -- a
# board that cannot calibrate goes to #895's fault vigil and stays there, so a kill that
# reaches the wrong process leaves it running.
run_one() { # $1 binary, $2 name, $3 clip
  mkdir -p /run1323/$2
  ( cd /run1323/$2 && exec $1 --debug --cams $3 --width 1280 --height 720 > /run1323/$2.out 2>&1 ) &
  local P=$!
  sleep 22
  kill -TERM $P 2>/dev/null
  wait $P 2>/dev/null
  sed 's/\x1b\[[0-9;]*m//g' /run1323/$2.out > /run1323/$2.txt
}

# The three mocks. OD_MAX_CYCLES ends a board that calibrates, and a board that CANNOT
# calibrate goes to #895's fault vigil and stays up for good -- which is exactly what
# §5c builds on purpose. So this waits for the cycles to run out and then ends the run by
# its own recorded pid, and never by pattern.
run_mocks() { # $1 binary, $2 name
  mkdir -p /run1323/$2
  ( cd /run1323/$2 && OD_MAX_CYCLES=20 exec $1 \
      --cams /app/mocks/cam_1.mp4,/app/mocks/cam_2.mp4,/app/mocks/cam_3.mp4 \
      --width 1280 --height 720 > /run1323/$2.out 2>&1 ) &
  local P=$!
  local WAITED=0
  while kill -0 $P 2>/dev/null && [ $WAITED -lt 90 ]; do sleep 2; WAITED=$((WAITED + 2)); done
  kill -TERM $P 2>/dev/null
  wait $P 2>/dev/null
  sed 's/\x1b\[[0-9;]*m//g' /run1323/$2.out > /run1323/$2.txt
}

# How many regions reached bull detection at all, which is what colour processing decided.
regions() { grep -ohE '[0-9]+ of [0-9]+ refused' /run1323/$1.txt | head -1 | awk '{print $3}'; }

echo "--- the footage: the mocks, aimed 180 px right and 90 px low ---"
# The shift is #1320's, and so is the speck: a 3 px red disc at 700,380 that reaches bull
# detection as ~250 px of area, through the same blur, dilation and closing the one in
# the issue came through.
g++ -std=c++17 -O1 -o /run1323/offaim /app/testers/i1323_offaim_footage.cpp $CVFLAGS || exit 1
/run1323/offaim /app/mocks/cam_1.mp4 /run1323/off_plain.avi 300 180 90 || exit 1
/run1323/offaim /app/mocks/cam_1.mp4 /run1323/off_speck.avi 300 180 90 700,380,3 || exit 1

echo
echo "=== 1. both rigs calibrate, and find the bulls they have always found ==="
run_mocks /app/build/opendartboard mocks
mkdir -p /run1323/rig
( cd /run1323/rig && OD_MAX_CYCLES=20 exec /app/build/opendartboard \
    --cams /app/mocks/rig-20260918/cam_1.mp4,/app/mocks/rig-20260918/cam_2.mp4,/app/mocks/rig-20260918/cam_3.mp4 \
    --width 1280 --height 720 > /run1323/rig.out 2>&1 )
sed 's/\x1b\[[0-9;]*m//g' /run1323/rig.out > /run1323/rig.txt

for rig in mocks rig; do
  if grep -q 'Initial calibration completed successfully on 3 of 3' /run1323/$rig.txt; then
    say "OK   $rig calibrates on all three cameras" ok
  else say "FAIL $rig did not calibrate on all three cameras" no; fi
  grep -E '^\[(ERROR|WARN)\]' /run1323/$rig.txt || true
  NOISE=$(grep -cE '^\[(ERROR|WARN)\]' /run1323/$rig.txt || true)
  if [ "$NOISE" = "0" ]; then say "OK   $rig prints no ERROR and no WARN" ok
  else say "FAIL $rig prints $NOISE ERROR/WARN lines" no; fi
done
for expected in "Camera 1 bull at (616,283)" "Camera 2 bull at (651,313)" "Camera 3 bull at (654,293)"; do
  if grep -qF "$expected" /run1323/mocks.txt; then say "OK   mocks: $expected" ok
  else say "FAIL mocks: no line saying $expected" no; fi
done
for expected in "Camera 1 bull at (671,309)" "Camera 2 bull at (626,292)" "Camera 3 bull at (700,298)"; do
  if grep -qF "$expected" /run1323/rig.txt; then say "OK   rig: $expected" ok
  else say "FAIL rig: no line saying $expected" no; fi
done

echo
echo "=== 2. the camera this issue is about finds its bull ==="
# Camera 1 of the mocks, aimed 180 right and 90 low. Its bull is the control's bull plus
# that shift and nothing else, because the shift is the only thing done to the footage.
run_one /app/build/opendartboard plain /run1323/off_plain.avi
if grep -qF 'Camera 1 bull at (796,373)' /run1323/plain.txt; then
  say "OK   off-aimed: the bull is at (796,373), which is (616,283) plus the shift" ok
else
  grep -hE 'bull at|did not calibrate' /run1323/plain.txt | head -1 | cut -c1-200
  say "FAIL off-aimed: the bull is not at the control's bull plus the shift" no
fi
# #1331 note, and the reason this is not `Initial calibration completed successfully`:
# a ONE-camera board cannot claim a calibration because the state vote requires two
# cameras. A two-camera board that meets that quorum is now a supported motion shape, so
# this assertion remains about the camera's geometry rather than admission. The census
# line is the same claim about the same thing: this camera has geometry to score a tip
# against.
if grep -q 'CAMERAS: 1 of 1 are looking at the dartboard (1)' /run1323/plain.txt; then
  say "OK   off-aimed: the camera calibrates" ok
else say "FAIL off-aimed: the camera did not calibrate" no; fi

echo
echo "=== 3. the board is measured here, and it is said out loud ==="
# #1394 replaced this stage's floor: it used to refuse a board on the area its boundary
# encloses -- the quantity #1340 proved collapses when a ring breaks -- and now refuses it
# on the SPAN, which is BullParams::minBoardRadius() read directly. So the first half of
# this sentence moved and the half #1323 is about did not: the middle this camera measured
# is still the board's, still at (798,424), and still not the frame's.
if grep -qE 'Camera 1 board measured from the coloured mask: the largest coloured region spans [0-9]+ px across its widest, and a board this stage can size a window against spans at least [0-9]+ px \(it encloses [0-9]+ px, [0-9.]+% of the frame\)\. Its middle is \(798,424\)' /run1323/plain.txt; then
  say "OK   the rule names the middle it measured, and it is the board's, not the frame's" ok
else
  grep -hE 'board measured|no board to measure' /run1323/plain.txt | head -1 | cut -c1-220
  say "FAIL the rule did not say what it measured" no
fi

echo
echo "=== 4. #1320's speck is on it, and is still not a bull ==="
run_one /app/build/opendartboard speck /run1323/off_speck.avi
if grep -qF 'Camera 1 bull at (796,373)' /run1323/speck.txt; then
  say "OK   speck: the bull is still found, and still at (796,373)" ok
else say "FAIL speck: the bull moved or was lost when a speck was painted on the frame" no; fi
if grep -qE 'REFUSED on size: it is 8\.[0-9] px across the radius' /run1323/speck.txt; then
  say "OK   speck: it reaches bull detection and is refused there, on size, as #1320 arranged" ok
else say "FAIL speck: nothing was refused on size at a speck's radius" no; fi

echo
echo "=== 5a. FALSIFY: put the windows back on the frame ==="
# This is the code before #1323: boardCenter is never set, so every distance below is
# measured from the middle of the frame again. It is built from a COPY of src/, never
# from the worktree.
rm -rf /run1323/mutant && mkdir -p /run1323/mutant
cp /app/CMakeLists.txt /run1323/mutant/ && cp -r /app/src /run1323/mutant/src
cmake -S /run1323/mutant -B /run1323/mutant/build -DCMAKE_PREFIX_PATH=/usr/local \
  -DCMAKE_CXX_FLAGS="-DDEBUG_SEEK_VIDEO -DDEBUG_VIA_VIDEO_INPUT" -DAPP_VERSION=0.0.0-mutant \
  -DFETCHCONTENT_SOURCE_DIR_NLOHMANN_JSON=/app/build/_deps/nlohmann_json-src \
  -DFETCHCONTENT_SOURCE_DIR_HTTPLIB=/app/build/_deps/httplib-src > /run1323/mcmake.log 2>&1 \
  || { tail -20 /run1323/mcmake.log; exit 1; }

# #1394 sized these windows off the board and the three lines below therefore changed
# shape: `enhancedMask.cols * params.centralityThreshold` became `centralityWindow`, a
# length in pixels computed before the loop. The mutations are the same three mutations --
# the window always keeps, the window always drops, the board is never measured -- and
# each still asserts its own anchor is present, so a fourth rewrite of these lines fails
# here by name rather than silently mutating nothing.
mutate() { # $1 python file describing the edit
  cp /app/src/detector/geometry/calibration/color_processing.cpp \
     /run1323/mutant/src/detector/geometry/calibration/color_processing.cpp
  python3 "$1" || exit 1
  cmake --build /run1323/mutant/build -- -j4 --no-print-directory > /run1323/mbuild.log 2>&1 \
    || { tail -20 /run1323/mbuild.log; exit 1; }
}

cat > /run1323/m_frame.py <<'PY'
p = '/run1323/mutant/src/detector/geometry/calibration/color_processing.cpp'
s = open(p).read()
# #1394 restructured this line: the floor is applied to the SPAN before the centroid is
# taken, so the condition is now a named bool. The mutation is unchanged in meaning --
# the board is never measured, so every window falls back to the frame.
old = '        if (bigEnough)'
new = '        if (false) // #1323 mutation: the board is never measured, so every window is back on the frame'
assert old in s, 'the frame mutation has nothing to replace'
open(p, 'w').write(s.replace(old, new))
PY
mutate /run1323/m_frame.py
run_one /run1323/mutant/build/opendartboard mframe /run1323/off_speck.avi
grep -hoE 'no candidate on this board can be a bull.{0,120}' /run1323/mframe.txt | head -1
if grep -q 'Camera 1 did not calibrate: the bull could not be found' /run1323/mframe.txt &&
   ! grep -q 'Initial calibration completed successfully' /run1323/mframe.txt; then
  say "OK   on the frame, this camera loses its bull entirely -- which is #1323" ok
else say "FAIL the frame-centred rule found a bull, so §2 proves nothing" no; fi

echo
echo "=== 5b. FALSIFY: a window that always keeps ==="
cat > /run1323/m_keep.py <<'PY'
p = '/run1323/mutant/src/detector/geometry/calibration/color_processing.cpp'
s = open(p).read()
old = '            bool isBullsEyeArea = (distToCenter < bullsEyeWindow);'
new = '            bool isBullsEyeArea = true; // #1323 mutation: the window always keeps'
assert old in s, 'the always-keep mutation has nothing to replace'
open(p, 'w').write(s.replace(old, new))
PY
mutate /run1323/m_keep.py
run_one /run1323/mutant/build/opendartboard mkeep /run1323/off_speck.avi
KEPT=$(regions mkeep); RULED=$(regions speck)
echo "regions reaching bull detection: ${RULED:-none} under the rule, ${KEPT:-none} under the mutation"
if [ -n "${KEPT:-}" ] && [ -n "${RULED:-}" ] && [ "$KEPT" -gt "$RULED" ]; then
  say "OK   a window that keeps everything sends $KEPT regions on where the rule sends $RULED" ok
else say "FAIL the window keeps the same things whether or not it is asked to; it decides nothing" no; fi
FAR=$(grep -hoE 'REFUSED on position: it sits [0-9]+' /run1323/mkeep.txt | awk '{print $NF}' | sort -n | tail -1)
if [ -n "${FAR:-}" ] && [ "${FAR:-0}" -gt 200 ]; then
  say "OK   one of them is ${FAR} px from the middle of the board, which the rule does not keep" ok
else say "FAIL nothing far from the board survived, so the mutation changed nothing" no; fi

echo
echo "=== 5c. FALSIFY: a window that always drops ==="
cat > /run1323/m_drop.py <<'PY'
p = '/run1323/mutant/src/detector/geometry/calibration/color_processing.cpp'
s = open(p).read()
edits = [
    ('            bool isCentral = (distToCenter < centralityWindow);',
     '            bool isCentral = false; // #1323 mutation: the window always drops'),
    ('            bool isBullsEyeArea = (distToCenter < bullsEyeWindow);',
     '            bool isBullsEyeArea = false; // #1323 mutation: the window always drops'),
    ('            bool isTooFarFromCenter = (distToCenter > farWindow);',
     '            bool isTooFarFromCenter = true; // #1323 mutation: the window always drops'),
]
for old, new in edits:
    assert old in s, 'the always-drop mutation has nothing to replace: ' + old.strip()[:40]
    s = s.replace(old, new)
open(p, 'w').write(s)
PY
mutate /run1323/m_drop.py
run_mocks /run1323/mutant/build/opendartboard mdrop
grep -hE 'Initial calibration completed' /run1323/mdrop.txt | head -1
if grep -q 'the bull could not be found' /run1323/mdrop.txt &&
   ! grep -q 'Initial calibration completed successfully on 3 of 3' /run1323/mdrop.txt; then
  say "OK   a window that drops everything loses a real bull on the control" ok
else say "FAIL the control calibrates with the window shut, so §1 proves nothing" no; fi

echo
echo "CHECK_RC=$FAILED"
exit $FAILED
