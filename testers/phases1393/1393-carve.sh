set -u
# #1393: the 50-point bull carve is a fraction of the BOARD, not a fifteenth of the
# frame -- measured, on the binary in build/, on both fixtures and on one binary.
#
# What this stage decides is not cosmetic. Everything red within `searchRadius` of the
# bull centre becomes `bullMask`, and `bullMask` is carved out of `fullMask` before the
# doubles, triples and outer-bull masks are derived from it -- so this number is upstream
# of the ellipse fit at STEP 6, of the wire count that reads it, and of the board every
# motion ratio is a fraction of. `min(cols, rows) / 15` is a fixed 48 px at 720p on every
# camera, every rig and every mounting.
#
# EVERY detector run is bounded by its own recorded pid. A board that cannot calibrate
# goes to #895's fault vigil and stays up on purpose, and OD_MAX_CYCLES does not bound
# it. Phase 5 hands the detector ONE camera, which can never reach the three motion
# detection initialises on, so all four of its runs fault by construction and none of
# them would ever return on its own.

FAILED=0
say() { echo "$1"; [ "$2" = ok ] || FAILED=1; }

MOCKS=/app/mocks/cam_1.mp4,/app/mocks/cam_2.mp4,/app/mocks/cam_3.mp4
RIG=/app/mocks/rig-20260918/cam_1.mp4,/app/mocks/rig-20260918/cam_2.mp4,/app/mocks/rig-20260918/cam_3.mp4
CVFLAGS="$(pkg-config --cflags --libs opencv4)"

# One run, ended by its own recorded pid and never by pattern. The subshell execs the
# detector so the pid recorded here is the detector's and not a shell's.
run() { # $1 name, $2 cams, $3 optional VAR=value for this run only
  mkdir -p /run1393/$1
  ( cd /run1393/$1 && export ${3:-OD_NOTHING_AT_ALL=1} && OD_MAX_CYCLES=20 \
      exec /app/build/opendartboard --debug --cams $2 --width 1280 --height 720 \
      > /run1393/$1.out 2>&1 ) &
  local P=$!
  local W=0
  while kill -0 $P 2>/dev/null && [ $W -lt 150 ]; do sleep 2; W=$((W + 2)); done
  kill -TERM $P 2>/dev/null
  wait $P 2>/dev/null
  sed 's/\x1b\[[0-9;]*m//g' /run1393/$1.out > /run1393/$1.txt
}

# "27 29 29" -- the carve radius each camera chose, in camera order.
radii() { grep -hoE 'Camera [0-9]+ bull carve: [0-9]+ px' /run1393/$1.txt \
  | sort -u | grep -oE ': [0-9]+ px' | grep -oE '[0-9]+' | tr '\n' ' '; }
# "364 364 355" -- the red it really took, which is the only honest answer to "was the
# over-carve masking anything". Everything below it in the pipeline is an inference.
carved() { grep -hoE 'Camera [0-9]+ bull carve:.*carving [0-9]+ px' /run1393/$1.txt \
  | sort -u | grep -oE 'carving [0-9]+' | grep -oE '[0-9]+' | tr '\n' ' '; }
# The doubles ring FITTED at STEP 6, which `1331-framing` section 2.5 pins on both
# fixtures. #1378 put those six numbers there precisely so a change upstream of the fit
# is visible rather than silent, and this slice is upstream of the fit.
fitted() { grep -hoE 'degrees: [0-9]+ px' /run1393/$1.txt | grep -oE '[0-9]+' | tr '\n' ' '; }
# The triples, which are `fullMask` minus the doubles and so the mask this carve reaches
# first. Areas rather than ellipses because that is what the stage prints.
triples() { grep -hoE '(outer|inner) triple ellipse from.*area: [0-9]+' /run1393/$1.txt \
  | sed 's/ ellipse from largest contour//;s/ ellipse from contour [0-9]//' | sort -u | tr '\n' '|'; }

echo "=== 1. both fixtures calibrate, with the six bull centres and six boards ======="
run mocks "$MOCKS"
run rig "$RIG"
for fix in mocks rig; do
  if grep -q 'Initial calibration completed successfully on 3 of 3' /run1393/$fix.txt; then
    say "OK   $fix calibrates on all three cameras" ok
  else say "FAIL $fix did not calibrate on all three cameras" no; fi
done
# #1320's six, asserted by #1323 and #1331 before this slice and unmoved by it. The carve
# is built AROUND the bull centre, so a centre that moved would mean the stage above had
# been disturbed rather than this one.
for e in "Camera 1 bull at (616,283)" "Camera 2 bull at (651,313)" "Camera 3 bull at (654,293)"; do
  if grep -qF "$e" /run1393/mocks.txt; then say "OK   mocks: $e" ok
  else say "FAIL mocks: no line saying $e" no; fi
done
for e in "Camera 1 bull at (671,309)" "Camera 2 bull at (626,292)" "Camera 3 bull at (700,298)"; do
  if grep -qF "$e" /run1393/rig.txt; then say "OK   rig: $e" ok
  else say "FAIL rig: no line saying $e" no; fi
done

echo
echo "=== 2. the carve radius per camera, and what it used to be ====================="
# The before/after #1393 asks for, on ONE binary. OD_BULL_CARVE=frame restores
# min(cols, rows) / 15 exactly, so neither column is a different build.
run mocksframe "$MOCKS" OD_BULL_CARVE=frame
run rigframe "$RIG" OD_BULL_CARVE=frame
grep -hoE 'Camera [0-9]+ bull carve:.*' /run1393/mocks.txt | sort -u
grep -hoE 'Camera [0-9]+ bull carve:.*' /run1393/rig.txt | sort -u
if [ "$(radii mocks)" = "27 29 29 " ]; then
  say "OK   mocks: 27, 29 and 29 px, 0.0935x boards of 291, 315 and 309 px" ok
else say "FAIL mocks carves at $(radii mocks), not 27 29 29" no; fi
if [ "$(radii rig)" = "18 18 18 " ]; then
  say "OK   rig: 18 px on all three, 0.0935x boards of 195, 196 and 197 px" ok
else say "FAIL rig carves at $(radii rig), not 18 18 18" no; fi
if [ "$(radii mocksframe)" = "48 48 48 " ] && [ "$(radii rigframe)" = "48 48 48 " ]; then
  say "OK   under the frame rule all six are 48 px -- the same number on a 291 px board and a 195 px one" ok
else say "FAIL the frame rule gave $(radii mocksframe) and $(radii rigframe), not six 48s" no; fi
# The switch has to be capable of changing the number it is asked about, or phase 3's
# "nothing moved" is what a switch that does nothing would also say.
if [ "$(radii mocks)" != "$(radii mocksframe)" ] && [ "$(radii rig)" != "$(radii rigframe)" ]; then
  say "OK   OD_BULL_CARVE=frame really moves the carve on both fixtures" ok
else say "FAIL OD_BULL_CARVE=frame changed no carve radius; the control below proves nothing" no; fi

echo
echo "=== 3. WAS THE OVER-CARVE MASKING ANYTHING? the red it really took ============="
# This is #1393's fourth criterion and it is answered with the pixel count rather than
# with the stages below it, because an unmoved ellipse is consistent both with a carve
# that took the same pixels and with one that took different pixels the fit shrugged off.
#
# The answer is ALMOST NOTHING, and the reason is the board rather than the fixtures. A
# dartboard's singles are black and cream: red and green exist only in the inner bull,
# the 25-ring, the treble ring and the doubles ring. Between the 25-ring (0.0935 of the
# board radius) and the inner edge of the trebles (99/170 = 0.582 of it) there is no red
# at all. 48 px is 0.165 of the mocks' fitted doubles semi-axis and 0.152 of the rig's --
# both inside that empty annulus -- so the frame rule was over-carving into a part of the
# mask that had nothing in it. Phase 5 is where it stops being empty.
echo "mocks red carved: $(carved mocks) (derived) vs $(carved mocksframe) (frame)"
echo "rig   red carved: $(carved rig) (derived) vs $(carved rigframe) (frame)"
if [ "$(carved mocks)" = "364 364 355 " ] && [ "$(carved mocksframe)" = "364 364 355 " ]; then
  say "OK   mocks: 364, 364 and 355 px of red under BOTH rules -- 48 px reached nothing 27 did not" ok
else say "FAIL mocks carved $(carved mocks) derived and $(carved mocksframe) framed; #1393 measured 364 364 355 both" no; fi
# The rig is where it is not quite nothing, and the one camera that differs is worth its
# own line rather than a tolerance: 81 px of red, 19% more, on camera 3 alone.
if [ "$(carved rig)" = "430 425 418 " ] && [ "$(carved rigframe)" = "430 425 499 " ]; then
  say "OK   rig: cameras 1 and 2 identical at 430 and 425 px; camera 3 took 499 px framed against 418 derived" ok
else say "FAIL rig carved $(carved rig) derived and $(carved rigframe) framed; #1393 measured 430 425 418 and 430 425 499" no; fi

echo
echo "=== 4. and the ring fit did not move, under either rule ========================"
# `1331-framing` section 2.5's six pins, asked here because this slice changes what is
# carved out of fullMask UPSTREAM of the fit that produces them. Both columns, because
# the claim is that the fit is the same under the old carve and the new one -- and the
# 81 px camera 3 lost in phase 3 is inside this number.
echo "mocks fitted: $(fitted mocks) | framed: $(fitted mocksframe)"
echo "rig   fitted: $(fitted rig) | framed: $(fitted rigframe)"
if [ "$(fitted mocks)" = "183859 173006 175444 " ] && [ "$(fitted mocksframe)" = "183859 173006 175444 " ]; then
  say "OK   mocks: 183859, 173006 and 175444 px under both rules -- 1331-framing 2.5's pins" ok
else say "FAIL mocks fitted $(fitted mocks) / $(fitted mocksframe), not 1331-framing 2.5's 183859 173006 175444" no; fi
if [ "$(fitted rig)" = "197117 200385 194335 " ] && [ "$(fitted rigframe)" = "197117 200385 194335 " ]; then
  say "OK   rig: 197117, 200385 and 194335 px under both rules -- 1331-framing 2.5's pins" ok
else say "FAIL rig fitted $(fitted rig) / $(fitted rigframe), not 1331-framing 2.5's 197117 200385 194335" no; fi
# The triples are the mask this carve reaches before the doubles, so they are asked
# separately rather than taken as covered by the fitted board above.
for fix in mocks rig; do
  if [ "$(triples $fix)" = "$(triples ${fix}frame)" ] && [ -n "$(triples $fix)" ]; then
    say "OK   $fix: the triples contours are the same under both rules" ok
  else say "FAIL $fix triples $(triples $fix) against $(triples ${fix}frame)" no; fi
done

echo
echo "=== 5. FALSIFY: the board at half the size, where the frame rule bites ========="
# Phase 3's answer is only honest beside this one. #1339's own scaler moves the single
# property #1393 is about -- how much of the frame the board fills -- by a known factor,
# on one clip with every dart and every arm in it unchanged. At 0.5 the board measures
# 93 px, the derived carve is 9 px and the frame rule is STILL 48 px: a carve that now
# reaches past the 25-ring, past the empty singles annulus and into the treble ring.
#
# One camera, so the board faults by construction -- motion detection initialises on
# three -- but the camera itself calibrates and every number this phase reads is printed
# before that. The run is bounded by its own pid for exactly this reason.
g++ -std=c++17 -O1 -o /run1393/scaled /app/testers/i1339_scaled_footage.cpp $CVFLAGS || exit 1
/run1393/scaled /app/mocks/cam_1.mp4 /run1393/half.avi 0.5 400 > /dev/null || exit 1
run half /run1393/half.avi
run halfframe /run1393/half.avi OD_BULL_CARVE=frame
grep -hoE 'Camera 1 bull carve:.*' /run1393/half.txt /run1393/halfframe.txt | sort -u
# The two controls that make the difference below attributable to the carve alone: the
# camera calibrates either way, off the same bull, with the same doubles ray trace.
for n in half halfframe; do
  if grep -q 'CAMERAS: 1 of 1 are looking at the dartboard' /run1393/$n.txt \
     && grep -qF 'Camera 1 bull at (628,322)' /run1393/$n.txt; then
    say "OK   at half size $n calibrates its camera off the bull at (628,322)" ok
  else say "FAIL at half size $n did not calibrate off (628,322)" no; fi
done
DP=$(grep -hoE 'outer_points=[0-9]+' /run1393/half.txt | head -1)
DF=$(grep -hoE 'outer_points=[0-9]+' /run1393/halfframe.txt | head -1)
if [ "$DP" = "outer_points=89" ] && [ "$DF" = "outer_points=89" ]; then
  say "OK   and the DOUBLES are untouched either way: 89 boundary points both" ok
else say "FAIL at half size the doubles gave $DP derived and $DF framed, not 89 both" no; fi
# The finding. Same footage, same bull, same doubles -- and the triples ring eaten.
if [ "$(carved half)" = "171 " ] && [ "$(carved halfframe)" = "354 " ]; then
  say "OK   the frame rule takes 354 px of red where the derived carve takes 171 -- 2.07x" ok
else say "FAIL at half size the carves took $(carved half) and $(carved halfframe), not 171 and 354" no; fi
if [ "$(triples half)" = "inner triple (area: 12503|outer triple (area: 17976|" ]; then
  say "OK   derived: an outer triple of 17976 px and an inner triple of 12503 px" ok
else say "FAIL at half size the derived carve fitted $(triples half)" no; fi
if [ "$(triples halfframe)" = "outer triple (area: 5319|" ]; then
  say "OK   FRAMED: the outer triple collapses to 5319 px and NO inner triple is fitted at all" ok
else say "FAIL at half size the frame rule fitted $(triples halfframe), not a lone 5319 px outer triple" no; fi

echo
if [ $FAILED -eq 0 ]; then echo "1393-carve: PASS"; else echo "1393-carve: FAIL"; fi
exit $FAILED
