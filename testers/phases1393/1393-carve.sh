set -u
# #1393: the 50-point bull carve is a fraction of the BOARD, not a fifteenth of the
# frame -- measured, on the binary in build/, on the rig and on one binary.
#
# #1478: the shipped mocks' column of sections 1 to 4 is gone -- their bulls, carves, red
# carved and fitted boards. The rig's was measured beside it and is the assertion now
# (docs/shipped-mock-census.md, A1). Section 5 is NOT re-pointed: it halves the board in
# frame, and the rig is 1.07x from not calibrating at all (1339-denominator's header), so
# there is nothing at half size to make from it. It stays on the mocks' camera 1.
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
# first. #1729: until #1499 (merged at f529ecf) the stage fitted them to contours and
# printed each contour's AREA, and this read those. #1499 ray-traces the treble edges and
# prints how many ray boundary points each ellipse was fitted from instead, so the area
# lines are gone and this read nothing at all -- an empty string on both sides, which the
# guard below refused. It reads the ray counts now, in the order the cameras print them:
# "outer=67 inner=67 outer=116 ...".
triples() { grep -hoE '(outer|inner) triple ellipse from [0-9]+ (ray boundary|inner) points' /run1393/$1.txt \
  | sed -E 's/ triple ellipse from ([0-9]+).*/=\1/' | tr '\n' ' '; }

echo "=== 1. the rig calibrates, with its three bull centres and three boards ========"
run rig "$RIG"
for fix in rig; do
  if grep -q 'Initial calibration completed successfully on 3 of 3' /run1393/$fix.txt; then
    say "OK   $fix calibrates on all three cameras" ok
  else say "FAIL $fix did not calibrate on all three cameras" no; fi
done
# #1320's six, asserted by #1323 and #1331 before this slice and unmoved by it. The carve
# is built AROUND the bull centre, so a centre that moved would mean the stage above had
# been disturbed rather than this one.
for e in "Camera 1 bull at (671,309)" "Camera 2 bull at (626,292)" "Camera 3 bull at (700,298)"; do
  if grep -qF "$e" /run1393/rig.txt; then say "OK   rig: $e" ok
  else say "FAIL rig: no line saying $e" no; fi
done

echo
echo "=== 2. the carve radius per camera, and what it used to be ====================="
# The before/after #1393 asks for, on ONE binary. OD_BULL_CARVE=frame restores
# min(cols, rows) / 15 exactly, so neither column is a different build.
run rigframe "$RIG" OD_BULL_CARVE=frame
grep -hoE 'Camera [0-9]+ bull carve:.*' /run1393/rig.txt | sort -u
# #1729: camera 1 was 18 px. 7e0ca67 ("the board is what surrounds the rest of the board")
# sizes rig camera 1 off its broken doubles ring, 317 px, and 0.0935x that is 30 px; the red
# it carves did not move (section 3). Re-measured on fork 61f9bcb's dev build.
if [ "$(radii rig)" = "30 18 18 " ]; then
  say "OK   rig: 30, 18 and 18 px, 0.0935x boards of 317, 196 and 197 px" ok
else say "FAIL rig carves at $(radii rig), not 30 18 18" no; fi
if [ "$(radii rigframe)" = "48 48 48 " ]; then
  say "OK   under the frame rule all three are 48 px -- a fifteenth of the frame, whatever the board" ok
else say "FAIL the frame rule gave $(radii rigframe), not three 48s" no; fi
# The switch has to be capable of changing the number it is asked about, or phase 3's
# "nothing moved" is what a switch that does nothing would also say.
if [ "$(radii rig)" != "$(radii rigframe)" ]; then
  say "OK   OD_BULL_CARVE=frame really moves the carve on the rig" ok
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
echo "rig   red carved: $(carved rig) (derived) vs $(carved rigframe) (frame)"
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
echo "rig   fitted: $(fitted rig) | framed: $(fitted rigframe)"
if [ "$(fitted rig)" = "197117 200385 194335 " ] && [ "$(fitted rigframe)" = "197117 200385 194335 " ]; then
  say "OK   rig: 197117, 200385 and 194335 px under both rules -- 1331-framing 2.5's pins" ok
else say "FAIL rig fitted $(fitted rig) / $(fitted rigframe), not 1331-framing 2.5's 197117 200385 194335" no; fi
# The triples are the mask this carve reaches before the doubles, so they are asked
# separately rather than taken as covered by the fitted board above.
# #1729: and pinned, because "the same under both rules" is also what two empty reads say.
for fix in rig; do
  if [ "$(triples $fix)" = "$(triples ${fix}frame)" ] \
     && [ "$(triples $fix)" = "outer=67 inner=67 outer=116 inner=116 outer=59 inner=59 " ]; then
    say "OK   $fix: the triples are traced from 67, 116 and 59 rays under both rules" ok
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
# #1729: what the eaten red did DOWNSTREAM is no longer what #1393 measured. Before #1499
# the triples were fitted to contours of the carved mask, and the frame rule left an outer
# triple of 5319 px and no inner triple at all against the derived carve's 17976 and 12503.
# #1499 (f529ecf) ray-traces the treble edges instead, and under it both rules fit both
# triple edges, from 119 rays derived and 116 framed. The carve still takes 2.07x the red (above), so the frame rule still
# bites; it is the contour fit that no longer shows it. Re-measured on fork 61f9bcb.
if [ "$(triples half)" = "outer=119 inner=119 " ]; then
  say "OK   derived: both triple edges traced, from 119 rays" ok
else say "FAIL at half size the derived carve fitted $(triples half), not both edges from 119 rays" no; fi
if [ "$(triples halfframe)" = "outer=116 inner=116 " ]; then
  say "OK   FRAMED: both triple edges traced too, from 116 rays -- since #1499 the over-carve no longer costs a triple" ok
else say "FAIL at half size the frame rule fitted $(triples halfframe), not both edges from 116 rays" no; fi

echo
if [ $FAILED -eq 0 ]; then echo "1393-carve: PASS"; else echo "1393-carve: FAIL"; fi
exit $FAILED
