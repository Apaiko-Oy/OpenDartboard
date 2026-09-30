set -u
# #1340: a board is big enough when the bull on it can be measured, not when it fills
# enough of the frame -- measured, on the binary in build/, on both fixtures.
#
# #1340 landed on 2026-09-18 and said so in its own commit message: NOT BUILT OR RUN
# HERE. There was no C++ toolchain on the machine that wrote it. So the change that
# decides which cameras calibrate at all had never been compiled, every number in its
# header was inferred from other issues' logs, and the two predictions it left for the
# rig were both wrong. This file is what runs it.
#
# EVERY detector run is under `timeout`. A board that cannot calibrate goes to #895's
# fault vigil and stays up on purpose, so a phase expecting a refusal and not bounding
# it never returns.

FAILED=0
say() { echo "$1"; [ "$2" = ok ] || FAILED=1; }
plain() { sed 's/\x1b\[[0-9;]*m//g' "$1" > "$2"; }

RIG=/app/mocks/rig-20260918/cam_1.mp4,/app/mocks/rig-20260918/cam_2.mp4,/app/mocks/rig-20260918/cam_3.mp4
MOCKS=/app/mocks/cam_1.mp4,/app/mocks/cam_2.mp4,/app/mocks/cam_3.mp4

# What each camera answered, as one line: "1 (671,309) 194.6" per camera. The bull
# centre AND the board it was sized against, because #1340 moved the second and the
# whole claim is that the first did not follow it.
answer() {
  grep -oE 'Camera [0-9]+ bull at \([0-9]+,[0-9]+\).*of the board radius [0-9.]+ px' "$1" \
    | sed -E 's/Camera ([0-9]+) bull at (\([0-9]+,[0-9]+\)).*of the board radius ([0-9.]+) px/\1 \2 \3/' \
    | sort -u | tr '\n' '; '
}

echo "=== 1. ten consecutive runs of mocks/rig-20260918 give one answer ==============="
# #1340's own acceptance criterion, and the reason it is ten and not one: file input is
# not a fixed frame. #1335 measured two runs of ONE commit landing on different frames
# of the same clip and reading a bull a pixel apart, because which frame calibration
# lands on depends on how the capture threads are scheduled. That is the same mechanism
# as the exposure drift that made the maintainer's rig swing between one and three
# cameras -- sampling a clip that is not constant -- so ten runs of a fixture is a real
# repeatability measurement rather than a determinism tautology.
#
# OD_MAX_CYCLES=20 bounds each run to one calibration pass. Without it the detector
# works through the whole sixty-second clip and calibrates more than once, so ten runs
# take twenty minutes and a "same answer" signature is about how many passes a run
# happened to make as much as about what each camera said. The de-duplication in
# answer() is belt and braces on the same point: the signature is per camera, and a
# second pass that DISAGREED would still show up as an extra entry.
mkdir -p /run1340/ten
TEN=""
for i in $(seq 1 10); do
  OD_MAX_CYCLES=20 timeout 60 /app/build/opendartboard --cams "$RIG" --width 1280 --height 720 \
    > /run1340/ten/$i.out 2>&1 || true
  plain /run1340/ten/$i.out /run1340/ten/$i.txt
  A="$(answer /run1340/ten/$i.txt)"
  N=$(grep -c 'Initial calibration completed successfully on 3 of 3 cameras' /run1340/ten/$i.txt || true)
  echo "run $i: cameras3of3=$N  $A"
  TEN="$TEN$N|$A
"
done
UNIQ=$(printf '%s' "$TEN" | sort -u | grep -c . || true)
if [ "$UNIQ" = "1" ]; then say "OK   all ten runs give the same per-camera answer" ok
else say "FAIL the ten runs give $UNIQ different answers, so this rig still swings" no; fi
# ... and it is the right answer, because ten identical wrong ones would pass the line
# above. These six are mocks/rig-20260918's own, pinned in i1320's and i1331's testers
# before #1340 was written, and #1340 must not have moved them.
for e in "Camera 1 bull at (671,309)" "Camera 2 bull at (626,292)" "Camera 3 bull at (700,298)"; do
  if grep -qF "$e" /run1340/ten/1.txt; then say "OK   rig: $e" ok
  else say "FAIL rig: no line saying $e" no; fi
done
if grep -q 'Initial calibration completed successfully on 3 of 3 cameras' /run1340/ten/1.txt; then
  say "OK   a ~150 px board calibrates on all three cameras" ok
else say "FAIL the rig this issue is about did not calibrate on three cameras" no; fi

echo
echo "=== 2. the shipped fixture does not regress, and the before/after is one binary ==="
# #1688: mocks_after runs under OD_BOARD=auto. Since #1676 the board is taken as a Winmau
# Blade 6 by default, and the shipped mocks are not one: their camera 2 finds the four clip
# wires of a wire number ring, so the default says so in two WARN lines (ORIENTATION and
# BOARD RECOGNITION), by design -- 1498-anchor-read pins the mocks the same way for the same
# reason. OD_BOARD=auto keeps #1340's floor (only OD_BOARD=frame restores the old one) and
# measures the board instead of forcing it, so the no-ERROR/no-WARN assertion below is still
# asked of a board the detector recognises. The ten rig runs are a Blade 6 and stay default.
OD_BOARD=auto OD_MAX_CYCLES=20 timeout 60 /app/build/opendartboard --cams "$MOCKS" --width 1280 --height 720 \
  > /run1340/mocks_after.out 2>&1 || true
OD_BOARD=frame OD_MAX_CYCLES=20 timeout 60 /app/build/opendartboard --cams "$MOCKS" --width 1280 --height 720 \
  > /run1340/mocks_before.out 2>&1 || true
plain /run1340/mocks_after.out /run1340/mocks_after.txt
plain /run1340/mocks_before.out /run1340/mocks_before.txt
echo "before (OD_BOARD=frame): $(answer /run1340/mocks_before.txt)"
echo "after  (#1340)         : $(answer /run1340/mocks_after.txt)"
BEFORE_BULLS=$(grep -oE 'Camera [0-9]+ bull at \([0-9]+,[0-9]+\)' /run1340/mocks_before.txt | tr '\n' ' ')
AFTER_BULLS=$(grep -oE 'Camera [0-9]+ bull at \([0-9]+,[0-9]+\)' /run1340/mocks_after.txt | tr '\n' ' ')
if [ -n "$AFTER_BULLS" ] && [ "$BEFORE_BULLS" = "$AFTER_BULLS" ]; then
  say "OK   the shipped fixture's three bull centres are the same before and after" ok
else say "FAIL the shipped fixture's bull centres moved: '$BEFORE_BULLS' -> '$AFTER_BULLS'" no; fi
for e in "Camera 1 bull at (616,283)" "Camera 2 bull at (651,313)" "Camera 3 bull at (654,293)"; do
  if grep -qF "$e" /run1340/mocks_after.txt; then say "OK   mocks: $e" ok
  else say "FAIL mocks: no line saying $e" no; fi
done
# The one WARN not counted is the pin's own announcement on mocks_after ("OD_BOARD=auto is
# set: ..."): every pin says it is set, at WARN, and it says nothing about the board
# (i1605_inside.sh excludes its OD_LOOK_BUDGET line the same way).
for f in mocks_after ten/1; do
  NOISE=$(grep -E '^\[(ERROR|WARN)\]' /run1340/$f.txt | grep -vc 'OD_BOARD=auto is set: ' || true)
  grep -E '^\[(ERROR|WARN)\]' /run1340/$f.txt | grep -v 'OD_BOARD=auto is set: ' || true
  if [ "$NOISE" = "0" ]; then say "OK   $f prints no ERROR and no WARN" ok
  else say "FAIL $f prints $NOISE ERROR/WARN lines" no; fi
done

echo
echo "=== 3. the falsifier restores the old stage, not merely a refusal ==============="
# A switch that only ever refuses would pass any test asking it to refuse. What says
# OD_BOARD=frame is the pre-#1340 stage is that it reproduces, to the decimal, six board
# radii and six bull ratios #1320 measured and wrote down before any of this was
# touched. Those numbers are in bull_processing.hpp and in #1320's issue text; this run
# has no way to know them except by computing them the old way.
OD_BOARD=frame OD_MAX_CYCLES=20 timeout 60 /app/build/opendartboard --cams "$RIG" --width 1280 --height 720 \
  > /run1340/rig_before.out 2>&1 || true
plain /run1340/rig_before.out /run1340/rig_before.txt
echo "rig before (OD_BOARD=frame): $(answer /run1340/rig_before.txt)"
echo "rig after  (#1340)         : $(answer /run1340/ten/1.txt)"
for e in "0.100 of the board radius 247.4 px" "0.099 of the board radius 242.5 px" "0.098 of the board radius 250.6 px"; do
  if grep -qF "$e" /run1340/mocks_before.txt; then say "OK   mocks, measured the old way: $e -- #1320's own number" ok
  else say "FAIL mocks, OD_BOARD=frame did not reproduce #1320's $e" no; fi
done
for e in "0.155 of the board radius 151.3 px" "0.154 of the board radius 153.0 px" "0.154 of the board radius 151.2 px"; do
  if grep -qF "$e" /run1340/rig_before.txt; then say "OK   rig, measured the old way: $e -- #1320's own number" ok
  else say "FAIL rig, OD_BOARD=frame did not reproduce #1320's $e" no; fi
done
# ... and the two measures really are different measures on this fixture, which is the
# positive control for everything above: if OD_BOARD changed nothing, §3 and §4 would
# both be measuring one code path twice.
if [ "$(answer /run1340/rig_before.txt)" != "$(answer /run1340/ten/1.txt)" ]; then
  say "OK   the two measures disagree about the rig's board, so the switch does something" ok
else say "FAIL OD_BOARD=frame changed nothing, so nothing below it is a before/after" no; fi

echo
echo "=== 4. the issue itself: a ring that stopped closing ==========================="
# #1340's rig is not a small board, it is a DULL one -- "its ring stopped closing
# wherever the light falls off it". So the fixture is a ring broken over 150 degrees
# and nothing else: same footage, same exposure, same distance, one sector of the
# doubles and triples painted the flat unsaturated grey #1362's tool paints, which is
# what colour processing keeps nothing of.
#
# 150 degrees is chosen by sweeping it, not by finding what passes. 210 degrees takes
# the bull with it and nothing is left to measure; 190 loses the doubles fit; at 150 the
# surviving region encloses 27705 px -- a 25% shortfall against the 36864 floor, where
# the maintainer's live rig measured 29674, 34985, 35428 and 36444, shortfalls of 20%
# down to 1.1%. So this fixture is NOT inside that band, it is one step past the worst
# of it, and the honest reading is that it reproduces the mechanism -- an area that
# collapses while the extent does not -- rather than the exact evening. A sweep between
# the two is available by changing one argument on the line below.
g++ -std=c++17 -O1 -o /run1340/brk /app/testers/i1362_broken_ring_footage.cpp \
  $(pkg-config --cflags --libs opencv4) || exit 1
/run1340/brk /app/mocks/rig-20260918/cam_1.mp4 /run1340/broken.avi 300 670 366 110 340 200,150 || exit 1
BRK=/run1340/broken.avi,/run1340/broken.avi,/run1340/broken.avi
# Three times, because #1338 refuses a board whose slot count is not 3 and would fault
# before the bull this phase is about is ever looked for. The subject is the floor, not
# how many cameras a board has.
OD_BOARD=frame timeout 60 /app/build/opendartboard --cams "$BRK" --width 1280 --height 720 \
  > /run1340/brk_before.out 2>&1 || true
timeout 60 /app/build/opendartboard --cams "$BRK" --width 1280 --height 720 \
  > /run1340/brk_after.out 2>&1 || true
plain /run1340/brk_before.out /run1340/brk_before.txt
plain /run1340/brk_after.out /run1340/brk_after.txt

AREA=$(grep -oE 'region encloses [0-9]+ pixels' /run1340/brk_before.txt | grep -oE '[0-9]+' | head -1)
echo "old floor says: $(grep -oE 'Camera 1 did not calibrate.{0,170}' /run1340/brk_before.txt | head -1)"
if grep -qE 'Camera 1 did not calibrate.*(4\.0% of the frame)' /run1340/brk_before.txt; then
  say "OK   under the old floor this camera is refused for having no board in the frame" ok
else say "FAIL the old floor did not refuse a ring broken over 150 degrees" no; fi
# The number it was refused on has to be inside the band this issue was filed about,
# or this fixture is reproducing some other failure that also happens to be red.
if [ -n "${AREA:-}" ] && [ "$AREA" -ge 25000 ] && [ "$AREA" -lt 36864 ]; then
  say "OK   it encloses $AREA px against the 36864 floor -- a 25% shortfall, where the maintainer's live rig measured 29674..36444, a 1% to 20% one" ok
else say "FAIL it encloses ${AREA:-nothing}, which is not the shortfall this issue is about" no; fi

echo "new floor says: $(grep -oE 'Camera 1 bull at .{0,120}' /run1340/brk_after.txt | head -1)"
BRK_BULL=$(grep -oE 'Camera 1 bull at \([0-9]+,[0-9]+\)' /run1340/brk_after.txt | head -1)
if [ -n "$BRK_BULL" ]; then say "OK   under #1340 the board is measured and a bull is found on it" ok
else say "FAIL #1340 found no bull on a board whose middle is untouched" no; fi
# It is the SAME bull the unbroken fixture finds, within a pixel. Read out of §1's run
# rather than pinned, so that a future frame or constant moves both together.
WANT=$(grep -oE 'Camera 1 bull at \([0-9]+,[0-9]+\)' /run1340/ten/1.txt | head -1)
echo "unbroken camera 1 says ${WANT:-nothing}; the broken ring says ${BRK_BULL:-nothing}"
if [ -n "$WANT" ] && [ -n "$BRK_BULL" ] && python3 -c "
import re,sys
a=[int(v) for v in re.findall(r'[0-9]+', '''$WANT''')[1:]]
b=[int(v) for v in re.findall(r'[0-9]+', '''$BRK_BULL''')[1:]]
sys.exit(0 if abs(a[0]-b[0])<=2 and abs(a[1]-b[1])<=2 else 1)"; then
  say "OK   painting out 150 degrees of ring moved the bull by at most 2 px" ok
else say "FAIL the bull found on the broken ring is not the bull the whole one gives" no; fi
# What the camera is then given. Until #1467 (2bcb9c3, 2026-09-20) it was refused -- by the
# wire stage, "found 10 (or 11) wire boundaries and all 20 are needed" -- and this section
# asserted that refusal. #1688 bisected the red that followed: green on 9d699de, red from
# 2bcb9c3, and none of #1407, #1676 or #1685 moves it. #1467 fits a twenty-fold model to
# the wire candidates instead of counting them, so the 210 degrees of ring the paint left
# place all twenty boundaries (coherence R 0.85 against a minimum of 0.60), and the camera
# calibrates. That is #1467 working as written, not the floor admitting a board it cannot
# measure: on b893830 the broken ring seals camera 1 at angle 10.69 and radius 256.85
# against the unbroken clip's 10.60 and 256.98, bull (671,308) against (671,309).
#
# So the half that keeps this section from being an argument for no floor at all is now
# asked as what it always meant: #1340 does not let a damaged board be scored WRONGLY. A
# broken ring either is refused by a stage that names the break, or is sealed with the
# geometry the whole ring gives -- within half a degree of wedge angle (a wedge is 18) and
# 1% of radius. A board that calibrated to some other geometry is the failure.
SEAL_RE='GEOMETRY SEALED: camera 1 index=[0-9]+ scoring=1 bull=[0-9]+,[0-9]+ [^|]*angle=[-0-9.]+ radius=[0-9.]+'
if grep -qE 'Camera 1 did not calibrate: the (wire stage|doubles ring)' /run1340/brk_after.txt; then
  echo "and then: $(grep -oE 'Camera 1 did not calibrate: the (wire stage|doubles ring).{0,110}' /run1340/brk_after.txt | head -1)"
  say "OK   the broken ring is refused, by the stage that can see the break" ok
else
  BSEAL=$(grep -oE "$SEAL_RE" /run1340/brk_after.txt | head -1)
  WSEAL=$(grep -oE "$SEAL_RE" /run1340/ten/1.txt | head -1)
  echo "broken: ${BSEAL:-no seal}"
  echo "whole : ${WSEAL:-no seal}"
  if [ -n "$BSEAL" ] && [ -n "$WSEAL" ] && python3 - "$BSEAL" "$WSEAL" <<'PY'
import re, sys
def f(s):
    return float(re.search(r'angle=([-0-9.]+)', s).group(1)), float(re.search(r'radius=([0-9.]+)', s).group(1))
(ba, br), (wa, wr) = f(sys.argv[1]), f(sys.argv[2])
sys.exit(0 if abs(ba - wa) <= 0.5 and abs(br - wr) <= 0.01 * wr else 1)
PY
  then
    say "OK   the broken ring is sealed with the whole ring's geometry (#1467's model placed the painted-out wires)" ok
  else say "FAIL a board with 150 degrees of its ring painted out was sealed with a geometry the whole ring does not give" no; fi
fi

echo
echo "=== 5. #1320's speck is still refused on size, on this binary =================="
# The control that gives every line above its meaning. The floor exists to stop a small
# red blob being sized as a board, and lowering it would trade one wrong answer for
# another; #1340's claim is that it raised the evidence instead. So the speck is run
# here too rather than left to i1320's tester, because "the rig calibrates" and "the
# speck is refused" have to be true of ONE binary in ONE run to be worth anything.
g++ -std=c++17 -O1 -o /run1340/speck /app/testers/i1320_speck_footage.cpp \
  $(pkg-config --cflags --libs opencv4) || exit 1
/run1340/speck /app/mocks/cam_1.mp4 /run1340/speck.avi 300 180 90 700,380,3 || exit 1
timeout 60 /app/build/opendartboard --debug \
  --cams /run1340/speck.avi,/run1340/speck.avi,/run1340/speck.avi \
  --width 1280 --height 720 > /run1340/speck.out 2>&1 || true
plain /run1340/speck.out /run1340/speck.txt
SPECK=$(grep -E 'Contour [0-9]+: .*centre=\((69[0-9]|70[0-9]),(37[0-9]|38[0-9])\)' /run1340/speck.txt | head -1)
echo "${SPECK:-no contour was reported where the disc was painted}"
if [ -n "$SPECK" ]; then say "OK   the speck reached bull detection, where the disc was painted" ok
else say "FAIL nothing was reported within ten pixels of (700,380), so this run measured no speck" no; fi
if [ -n "$SPECK" ] && echo "$SPECK" | grep -qE 'circ=0\.[89][0-9]' && echo "$SPECK" | grep -q 'REFUSED on size'; then
  say "OK   it is round enough to have won on circularity, and is refused on size" ok
else say "FAIL the speck was not refused on size" no; fi
SBULL=$(grep -oE 'Camera 1 bull at \([0-9]+,[0-9]+\)' /run1340/speck.txt | head -1)
if [ -z "$SBULL" ] || [ -z "$SPECK" ]; then
  say "FAIL this run has no bull, no speck, or neither, so nothing was told apart" no
elif echo "$SBULL" | grep -qE '\((69[0-9]|70[0-9]),(37[0-9]|38[0-9])\)'; then
  say "FAIL the board was calibrated from where the disc was painted, not from the bull" no
else say "OK   the speck lost to a bull 96 px away from it on the same binary that rescues the rig" ok; fi

echo
echo "CHECK_RC=$FAILED"
exit $FAILED
