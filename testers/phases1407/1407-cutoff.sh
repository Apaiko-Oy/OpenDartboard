set -u
# #1407: the outer cutoff is 1.326 board radii -- the rim, 225.5/170 -- against the board
# radius STEP 1.6 names the ring for, on STEP 2.5's pass; STEP 1's full-frame pass keeps
# the frame's 384 px because the ring identity is read from that pass. Measured on the
# binary in build/, on both fixtures and on one binary: OD_COLOUR_CUTOFF=frame puts the
# frame rule back for this window alone.

FAILED=0
say() { echo "$1"; [ "$2" = ok ] || FAILED=1; }

MOCKS=/app/mocks/cam_1.mp4,/app/mocks/cam_2.mp4,/app/mocks/cam_3.mp4
RIG=/app/mocks/rig-20260918/cam_1.mp4,/app/mocks/rig-20260918/cam_2.mp4,/app/mocks/rig-20260918/cam_3.mp4
CVFLAGS="$(pkg-config --cflags --libs opencv4)"

run() { # $1 name, $2 cams, $3 optional VAR=value for this run only
  mkdir -p /run1407/$1
  ( cd /run1407/$1 && export ${3:-OD_NOTHING_AT_ALL=1} && OD_MAX_CYCLES=20 \
      exec /app/build/opendartboard --debug --cams $2 --width 1280 --height 720 \
      > /run1407/$1.out 2>&1 ) &
  local P=$!
  local W=0
  while kill -0 $P 2>/dev/null && [ $W -lt 150 ]; do sleep 2; W=$((W + 2)); done
  kill -TERM $P 2>/dev/null
  wait $P 2>/dev/null
  sed 's/\x1b\[[0-9;]*m//g' /run1407/$1.out > /run1407/$1.txt
}
has() { grep -qF "$2" /run1407/$1.txt; }
fitted() { grep -hoE 'degrees: [0-9]+ px' /run1407/$1.txt | grep -oE '[0-9]+' | tr '\n' ' '; }
kept() { grep -hoE 'Camera [0-9] colour windows kept [0-9]+ px of [0-9]+' /run1407/$1.txt | tr '\n' ' '; }
outcome() { grep -hE 'bull at \(|degrees: [0-9]+ px|calibration completed' /run1407/$1.txt | sed 's/^\[[^]]*\]//'; }
# The outer cutoff's part of the census, one line per pass per camera.
census() { grep -hoE 'Camera [0-9] colour windows kept .*' /run1407/$1.txt \
  | sed -E 's/: centrality at .*(outer cutoff at)/: \1/; s/; connectivity at .*//'; }

echo "=== 1. both fixtures calibrate, with the six bull centres and six fitted boards ==="
run mocks "$MOCKS"
run rig "$RIG"
for fix in mocks rig; do
  if has $fix 'Initial calibration completed successfully on 3 of 3'; then
    say "OK   $fix calibrates on all three cameras" ok
  else say "FAIL $fix did not calibrate on all three cameras" no; fi
done
for e in "Camera 1 bull at (616,283)" "Camera 2 bull at (651,313)" "Camera 3 bull at (654,293)"; do
  if has mocks "$e"; then say "OK   mocks: $e" ok; else say "FAIL mocks: no line saying $e" no; fi
done
for e in "Camera 1 bull at (671,309)" "Camera 2 bull at (626,292)" "Camera 3 bull at (700,298)"; do
  if has rig "$e"; then say "OK   rig: $e" ok; else say "FAIL rig: no line saying $e" no; fi
done
if [ "$(fitted mocks)" = "183859 173006 175444 " ] && [ "$(fitted rig)" = "197117 200385 194335 " ]; then
  say "OK   the six fitted boards: 183859/173006/175444 and 197117/200385/194335 px" ok
else say "FAIL fitted mocks '$(fitted mocks)' rig '$(fitted rig)'" no; fi

echo
echo "=== 2. the cutoff per camera, in pixels, and the ring it was drawn from ========"
census mocks
census rig
grep -hoE 'Camera [0-9] (outer cutoff [0-9]+ px off [^;]*|ring identity: the span is the [A-Z]+ ring)' \
  /run1407/mocks.txt /run1407/rig.txt | sort -u
for c in 1 2 3; do
  for fix in mocks rig; do
    if has $fix "Camera $c outer cutoff 384 px off the FRAME's width, no ring having been named for this pass"; then
      say "OK   $fix camera $c: STEP 1's full-frame pass keeps the frame's 384 px" ok
    else say "FAIL $fix camera $c: STEP 1's pass did not keep 384 px" no; fi
  done
done
# measureBoard's span x boardRadiusOfSpan() x 1.326. Mocks: three DOUBLES spans of
# 291/314/308 px. Rig: camera 1 a DOUBLES span of 316 px, cameras 2 and 3 TREBLE spans of
# 195/197 px, x 1.589.
for e in "mocks|Camera 1 outer cutoff 386 px off a board radius of 291 px" \
         "mocks|Camera 2 outer cutoff 417 px off a board radius of 315 px" \
         "mocks|Camera 3 outer cutoff 410 px off a board radius of 309 px" \
         "rig|Camera 1 outer cutoff 420 px off a board radius of 317 px" \
         "rig|Camera 2 outer cutoff 412 px off a board radius of 311 px" \
         "rig|Camera 3 outer cutoff 416 px off a board radius of 314 px"; do
  if has "${e%%|*}" "${e#*|}"; then say "OK   ${e%%|*}: ${e#*|}" ok
  else say "FAIL ${e%%|*}: no line saying ${e#*|}" no; fi
done
for e in "mocks|Camera 1 ring identity: the span is the DOUBLES ring" \
         "mocks|Camera 2 ring identity: the span is the DOUBLES ring" \
         "mocks|Camera 3 ring identity: the span is the DOUBLES ring" \
         "rig|Camera 1 ring identity: the span is the DOUBLES ring" \
         "rig|Camera 2 ring identity: the span is the TREBLE ring" \
         "rig|Camera 3 ring identity: the span is the TREBLE ring"; do
  if has "${e%%|*}" "${e#*|}"; then say "OK   ${e%%|*}: ${e#*|}" ok
  else say "FAIL ${e%%|*}: no line saying ${e#*|}" no; fi
done

echo
echo "=== 3. WHAT THE CUTOFF KEEPS AND DROPS, from #1394's census ===================="
# The room it must drop sits at 1.58 and 1.57 R on mocks cameras 2 and 3 and at 1.84 R on
# rig camera 1; the board it must keep sits at 1.10 and 1.18 R on rig camera 3 -- OUTSIDE
# 1.0 R, which is why the stop is the rim and not the doubles' outer wire.
for e in "mocks|outer cutoff at 386 px decides 0 of 46 components (0 px)" \
         "mocks|outer cutoff at 417 px decides 1 of 24 components (5791 px), keeping 0 of them (0 px) [DROPPED 5791 px at (150,472), 497 px out = 1.59 spans = 1.58 R]" \
         "mocks|outer cutoff at 410 px decides 1 of 21 components (8158 px), keeping 0 of them (0 px) [DROPPED 8158 px at (1151,444), 486 px out = 1.59 spans = 1.57 R]" \
         "rig|outer cutoff at 420 px decides 1 of 84 components (4879 px), keeping 0 of them (0 px) [DROPPED 4879 px at (1225,277), 582 px out = 1.86 spans = 1.84 R]" \
         "rig|outer cutoff at 412 px decides 0 of 31 components (0 px)" \
         "rig|outer cutoff at 416 px decides 2 of 76 components (8534 px), keeping 2 of them (8534 px) [kept 4664 px at (634,91), 344 px out = 1.03 spans = 1.10 R] [kept 3870 px at (883,109), 371 px out = 1.11 spans = 1.18 R]"; do
  if has "${e%%|*}" "${e#*|}"; then say "OK   ${e%%|*}: ${e#*|}" ok
  else say "FAIL ${e%%|*}: no census reading ${e#*|}" no; fi
done

echo
echo "=== 4. FALSIFY: OD_COLOUR_CUTOFF=frame, on the same binary ======================"
run mocksframe "$MOCKS" OD_COLOUR_CUTOFF=frame
run rigframe "$RIG" OD_COLOUR_CUTOFF=frame
census mocksframe
census rigframe
# The switch must MOVE the number it is about (#1340's rule) ...
for e in "mocksframe|Camera 1 outer cutoff 384 px off the FRAME's width, by switch or for want of a board middle, where the rim would be 386 px" \
         "mocksframe|Camera 2 outer cutoff 384 px off the FRAME's width, by switch or for want of a board middle, where the rim would be 417 px" \
         "mocksframe|Camera 3 outer cutoff 384 px off the FRAME's width, by switch or for want of a board middle, where the rim would be 410 px" \
         "rigframe|Camera 1 outer cutoff 384 px off the FRAME's width, by switch or for want of a board middle, where the rim would be 420 px" \
         "rigframe|Camera 2 outer cutoff 384 px off the FRAME's width, by switch or for want of a board middle, where the rim would be 412 px" \
         "rigframe|Camera 3 outer cutoff 384 px off the FRAME's width, by switch or for want of a board middle, where the rim would be 416 px"; do
  if has "${e%%|*}" "${e#*|}"; then say "OK   ${e%%|*}: ${e#*|}" ok
  else say "FAIL ${e%%|*}: no line saying ${e#*|}" no; fi
done
if has rigframe "outer cutoff at 384 px decides 2 of 76 components (8534 px), keeping 2 of them (8534 px)" \
   && has mocksframe "outer cutoff at 384 px decides 1 of 24 components (5791 px), keeping 0 of them (0 px)"; then
  say "OK   ... and under the frame rule the census decides the same components at 384 px" ok
else say "FAIL the frame rule's census is not the one #1394 recorded" no; fi
# ... and on these two fixtures that is ALL it moves: no component on either lies between
# 384 px and the rim, so every pixel kept, every bull and every fitted board is the same.
# "Nothing, and here is the number" -- the number is in section 2.
for fix in mocks rig; do
  if [ "$(kept $fix)" = "$(kept ${fix}frame)" ] && [ "$(outcome $fix)" = "$(outcome ${fix}frame)" ]; then
    say "OK   $fix: the rim and the frame keep the same pixels, bulls and boards: $(kept $fix)" ok
  else say "FAIL $fix: kept '$(kept $fix)' against the frame's '$(kept ${fix}frame)'" no; fi
done

echo
echo "=== 5. FALSIFY where the frame rule is wrong: the board at half the size ========"
# #1339's scaler, as #1394 used it: one clip, the board at half size. The frame's 384 px
# does not shrink with it; the rim does.
g++ -std=c++17 -O1 -o /run1407/scaled /app/testers/i1339_scaled_footage.cpp $CVFLAGS || exit 1
/run1407/scaled /app/mocks/cam_1.mp4 /run1407/half.avi 0.5 400 > /dev/null || exit 1
run half /run1407/half.avi
run halfframe /run1407/half.avi OD_COLOUR_CUTOFF=frame
census half
census halfframe
grep -hoE 'Camera 1 (outer cutoff [0-9]+ px off [^;]*|ring identity: the span is the [A-Z]+ ring)' \
  /run1407/half.txt /run1407/halfframe.txt | sort -u
echo "half kept: $(kept half) (rim) vs $(kept halfframe) (frame)"
# Held to what it measures, which is NOT the discrimination this phase was written for.
# STEP 1.6 reads this synthetic board as a TREBLE span -- colour reaching 1.490 spans of a
# 172 px span -- where the same clip at full size reads DOUBLES at 0.965. So the board
# radius is 172 x 1.589 = 274 px and the rim lands at 363 px, 21 px inside the frame's 384,
# and neither window decides anything the other does not. Whether 1.490 is a misreading
# of a scaled-and-padded frame or a true reading of what the scaler made is #1423's
# question and not this issue's; it is asserted so that a change in it is seen.
for e in "Camera 1 ring identity: the span is the TREBLE ring: colour reaches 1.490 spans" \
         "Camera 1 outer cutoff 363 px off a board radius of 274 px" \
         "outer cutoff at 363 px decides 1 of 31 components (1624 px), keeping 1 of them (1624 px) [kept 1624 px at (452,165), 270 px out = 1.59 spans = 0.99 R]"; do
  if has half "$e"; then say "OK   half: $e" ok; else say "FAIL half: no line saying $e" no; fi
done
if has halfframe "outer cutoff at 384 px decides 1 of 31 components (1624 px), keeping 1 of them (1624 px)" \
   && [ "$(kept half)" = "$(kept halfframe)" ]; then
  say "OK   half: the frame's 384 px keeps the same 1624 px component and the same pixels" ok
else say "FAIL half: the frame rule kept '$(kept halfframe)' against the rim's '$(kept half)'" no; fi

echo
if [ $FAILED -eq 0 ]; then echo "1407-cutoff: PASS"; else echo "1407-cutoff: FAIL"; fi
exit $FAILED
