set -u
FAILED=0
say() { echo "$1"; [ "${2:-no}" = ok ] || FAILED=1; }
CVFLAGS="$(pkg-config --cflags --libs opencv4)"

# One camera, one clip, ended by its own recorded pid and never by pattern. The subshell
# execs the detector so the pid recorded here is the detector's and not a shell's -- a
# board that cannot calibrate goes to #895's fault vigil and stays there, so a kill that
# reaches the wrong process leaves it running.
run_one() { # $1 name, $2 clip, $3 optional VAR=value for this run only
  mkdir -p /run1331/$1
  ( cd /run1331/$1 && export ${3:-OD_NOTHING_AT_ALL=1} && exec /app/build/opendartboard \
      --debug --cams $2 --width 1280 --height 720 > /run1331/$1.out 2>&1 ) &
  local P=$!
  sleep 20
  kill -TERM $P 2>/dev/null
  wait $P 2>/dev/null
  sed 's/\x1b\[[0-9;]*m//g' /run1331/$1.out > /run1331/$1.txt
}

# Three cameras. OD_MAX_CYCLES ends a board that calibrates; a board that CANNOT
# calibrate goes to #895's fault vigil and stays up for good, so this waits for the
# cycles to run out and then ends the run by its own recorded pid, never by pattern.
run_three() { # $1 name, $2 cams
  mkdir -p /run1331/$1
  ( cd /run1331/$1 && OD_MAX_CYCLES=20 exec /app/build/opendartboard \
      --debug --cams $2 --width 1280 --height 720 > /run1331/$1.out 2>&1 ) &
  local P=$!
  local WAITED=0
  while kill -0 $P 2>/dev/null && [ $WAITED -lt 120 ]; do sleep 2; WAITED=$((WAITED + 2)); done
  kill -TERM $P 2>/dev/null
  wait $P 2>/dev/null
  sed 's/\x1b\[[0-9;]*m//g' /run1331/$1.out > /run1331/$1.txt
}

# The board radius this run measured, and how many of the 120 rays found the doubles ring.
board_radius() { grep -hoE 'of the board radius [0-9.]+' /run1331/$1.txt | head -1 | awk '{print $5}'; }
outer_points() { grep -hoE 'outer_points=[0-9]+' /run1331/$1.txt | head -1 | cut -d= -f2; }

echo "--- the footage: the mocks, aimed off the middle of their own frame ---"
# Built with #1323's own fixture builder, which is the whole of what is needed here: the
# frame shifted, nothing painted on it. (230,150) puts the board low and right with 27 px
# of its coloured extent still clear of the frame edge -- whole, and a long way from the
# middle. (430,0) and (300,0) put part of it outside the picture.
g++ -std=c++17 -O1 -o /run1331/offaim /app/testers/i1323_offaim_footage.cpp $CVFLAGS || exit 1
/run1331/offaim /app/mocks/cam_1.mp4 /run1331/off_whole.avi 150 230 150 || exit 1
/run1331/offaim /app/mocks/cam_1.mp4 /run1331/off_cut.avi   150 430 0   || exit 1
/run1331/offaim /app/mocks/cam_1.mp4 /run1331/off_edge.avi  150 300 0   || exit 1

echo
echo "=== 1. both rigs calibrate, and measure the boards they have always measured ==="
run_three mocks /app/mocks/cam_1.mp4,/app/mocks/cam_2.mp4,/app/mocks/cam_3.mp4
run_three rig /app/mocks/rig-20260918/cam_1.mp4,/app/mocks/rig-20260918/cam_2.mp4,/app/mocks/rig-20260918/cam_3.mp4

for rig in mocks rig; do
  if grep -q 'Initial calibration completed successfully on 3 of 3' /run1331/$rig.txt; then
    say "OK   $rig calibrates on all three cameras" ok
  else say "FAIL $rig did not calibrate on all three cameras" no; fi
  grep -E '^\[(ERROR|WARN)\]' /run1331/$rig.txt || true
  NOISE=$(grep -cE '^\[(ERROR|WARN)\]' /run1331/$rig.txt || true)
  if [ "$NOISE" = "0" ]; then say "OK   $rig prints no ERROR and no WARN" ok
  else say "FAIL $rig prints $NOISE ERROR/WARN lines" no; fi
done
# The six bull centres #1320 recorded and #1323 asserted. ADR-0079 turned the first two
# stages of this pipeline around; not one of these moved by a pixel.
for expected in "Camera 1 bull at (616,283)" "Camera 2 bull at (651,313)" "Camera 3 bull at (654,293)"; do
  if grep -qF "$expected" /run1331/mocks.txt; then say "OK   mocks: $expected" ok
  else say "FAIL mocks: no line saying $expected" no; fi
done
for expected in "Camera 1 bull at (671,309)" "Camera 2 bull at (626,292)" "Camera 3 bull at (700,298)"; do
  if grep -qF "$expected" /run1331/rig.txt; then say "OK   rig: $expected" ok
  else say "FAIL rig: no line saying $expected" no; fi
done
# And the six boards, measured on the FULL frame now rather than inside a frame-centred
# ellipse, are the same six boards to the pixel.
for expected in "Camera 1 board found on the FULL frame: radius 291 px across its widest, centre (612,338)" \
                "Camera 2 board found on the FULL frame: radius 314 px across its widest, centre (619,370)" \
                "Camera 3 board found on the FULL frame: radius 308 px across its widest, centre (676,374)"; do
  if grep -qF "$expected" /run1331/mocks.txt; then say "OK   mocks: $expected" ok
  else grep -hoE 'board found on the FULL frame[^;]*' /run1331/mocks.txt | head -3
       say "FAIL mocks: no line saying $expected" no; fi
done
for expected in "Camera 1 board found on the FULL frame: radius 194 px across its widest, centre (671,320)" \
                "Camera 2 board found on the FULL frame: radius 195 px across its widest, centre (624,313)" \
                "Camera 3 board found on the FULL frame: radius 197 px across its widest, centre (700,328)"; do
  if grep -qF "$expected" /run1331/rig.txt; then say "OK   rig: $expected" ok
  else grep -hoE 'board found on the FULL frame[^;]*' /run1331/rig.txt | head -3
       say "FAIL rig: no line saying $expected" no; fi
done
# ADR-0079 §2 on the footage this repository ships: every one of the six has its whole
# board in shot, and the margin is a distance in pixels rather than a share of anything.
for expected in "kept comes within 121 px" "kept comes within 148 px" "kept comes within 119 px"; do
  if grep -qF "$expected" /run1331/mocks.txt; then say "OK   mocks: the frame edge is $expected away" ok
  else say "FAIL mocks: no camera reports $expected" no; fi
done
for expected in "kept comes within 75 px" "kept comes within 39 px" "kept comes within 55 px"; do
  if grep -qF "$expected" /run1331/rig.txt; then say "OK   rig: the frame edge is $expected away" ok
  else say "FAIL rig: no camera reports $expected" no; fi
done

echo
echo "=== 2. the region is drawn around the board that was found ==="
# 1.25x the board, around the board's own middle. On camera 2 of the mocks that is 393 px
# around (619,370); the ellipse it replaced was 486x316 around (640,360) whatever was in
# the picture.
if grep -qF "Camera 2 region: 393 px around the board found at (619,370), which measured 314 px" /run1331/mocks.txt; then
  say "OK   the region names the board it was drawn around, and its radius is 1.25x of it" ok
else
  grep -hoE 'region: .*' /run1331/mocks.txt | head -3
  say "FAIL the region does not say which board it was drawn around" no
fi

echo
echo "=== 3. a board low and right in its own frame calibrates, whole ==="
run_one whole /run1331/off_whole.avi
if grep -qF 'Camera 1 bull at (846,432)' /run1331/whole.txt; then
  say "OK   off-centre: the bull is at (846,432), the control's (616,283) plus the shift" ok
else
  grep -hE 'bull at|did not calibrate' /run1331/whole.txt | head -1 | cut -c1-200
  say "FAIL off-centre: the bull is not the control's bull plus the shift" no
fi
if grep -q 'CAMERAS: 1 of 1 are looking at the dartboard' /run1331/whole.txt; then
  say "OK   off-centre: the camera calibrates" ok
else say "FAIL off-centre: the camera did not calibrate" no; fi
WR=$(board_radius whole); WP=$(outer_points whole)
echo "off-centre under the rule: board radius ${WR:-none} px, ${WP:-none} of 120 rays found the doubles ring"
if [ "${WR:-0%.*}" = "287.7" ]; then
  say "OK   off-centre: the board measures 287.7 px, which is the control's own board" ok
else say "FAIL off-centre: the board measures ${WR:-nothing}, not the 287.7 px this footage has" no; fi
if [ -n "${WP:-}" ] && [ "$WP" -ge 90 ]; then
  say "OK   off-centre: $WP of 120 rays found the doubles ring" ok
else say "FAIL off-centre: only ${WP:-no} rays found the doubles ring" no; fi

echo
echo "=== 4. FALSIFY: put the region back on the middle of the frame ==="
# The same binary and the same clip. OD_ROI=frame restores the four constants ADR-0079 §1
# retired, and nothing else about the run moves -- which is the point of doing it at run
# time rather than with a second build.
run_one whole_frame /run1331/off_whole.avi OD_ROI=frame
FR=$(board_radius whole_frame); FP=$(outer_points whole_frame)
echo "the same clip on the frame-centred ellipse: board radius ${FR:-none} px, ${FP:-none} of 120 rays"
if [ -z "${FR:-}" ] || [ -z "${WR:-}" ]; then
  say "FAIL one of the two arms measured no board at all, so nothing can be compared" no
elif python3 -c "import sys; sys.exit(0 if $FR < 0.7 * $WR else 1)"; then
  say "OK   on the frame, this board measures $FR px where it really is $WR -- a clipped board, measured as a smaller one" ok
else
  say "FAIL the frame-centred region measured the same board, so §3 proves nothing" no
fi
if [ -n "${FP:-}" ] && [ -n "${WP:-}" ] && [ "$FP" -lt "$WP" ] && [ "$FP" -le 70 ]; then
  say "OK   and its doubles ring is traced by $FP of 120 rays against $WP, ten above the 50 that refuse a camera" ok
else
  say "FAIL the ring was traced as well on the frame as around the board, so the region decides nothing" no
fi

echo
echo "=== 5. a board the FRAME's own edge cuts does not calibrate, and says so ==="
# Two of them. The first is plainly cut. The second is the one that matters: at 430 px of
# shift the largest coloured region is the INNER part of a broken board, which measures a
# tidy 176 px sitting 56 px clear of the nearest edge -- so a framing question asked of
# the winning region alone passes it, and this one does not.
run_one edge /run1331/off_edge.avi
run_one cut /run1331/off_cut.avi
for name in edge cut; do
  if grep -qE '^\[ERROR\].*Camera 1 did not calibrate: it is not looking at a WHOLE dartboard: the coloured region that is its doubles ring comes within [0-9]+ px of the edge of its own frame and a board wholly in shot leaves at least 1' /run1331/$name.txt; then
    say "OK   $name: camera 1 is named, with the gap it failed on and the gap a whole board leaves" ok
  else
    grep -hE '^\[ERROR\]' /run1331/$name.txt | head -1 | cut -c1-200
    say "FAIL $name: no ERROR names camera 1 and the frame edge" no
  fi
  ERRS=$(grep -cE '^\[ERROR\]\[GEOMETRY_CALIBRATION\] - Camera' /run1331/$name.txt || true)
  if [ "$ERRS" = "1" ]; then say "OK   $name: exactly one calibration ERROR for the one refused camera" ok
  else say "FAIL $name: $ERRS calibration ERROR lines for one refused camera" no; fi
  if grep -q 'CAMERAS: 0 of 1 are looking at the dartboard; refused: 1' /run1331/$name.txt; then
    say "OK   $name: the census counts it as refused rather than as silent" ok
  else say "FAIL $name: the census does not name it refused" no; fi
  if grep -qE 'BOARD FAULTED: camera 1 did not calibrate' /run1331/$name.txt; then
    say "OK   $name: #1318's refusal path carries it, naming the camera" ok
  else say "FAIL $name: the board did not fault with the camera named" no; fi
done
# The half of §5 that makes the other half mean something: at 430 px the region a framing
# check would otherwise have asked about looks entirely healthy.
if grep -qF 'board found on the FULL frame: radius 176 px across its widest, centre (1046,304)' /run1331/cut.txt; then
  say "OK   cut: the largest region alone measures a plausible 176 px board, and is still refused" ok
else
  grep -hoE 'board found on the FULL frame[^;]*' /run1331/cut.txt | head -1
  say "FAIL cut: the region this case is about is not the one that was measured" no
fi

echo
echo "CHECK_RC=$FAILED"
exit $FAILED
