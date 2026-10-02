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
run_three() { # $1 name, $2 cams, $3 optional VAR=value for this run only
  mkdir -p /run1331/$1
  ( cd /run1331/$1 && export ${3:-OD_NOTHING_AT_ALL=1} && OD_MAX_CYCLES=20 exec /app/build/opendartboard \
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
# #1478: NOT re-pointed. Sections 3 to 5 are built from the shipped mocks' camera 1 and pin
# figures read off it -- its bull (616,283) plus the shift, its 287.7 px board, the cut
# board's 176 px at (1046,304). The rig has no half of them, so they need a measurement on
# the rig rather than a deleted column, and are left as they are.
# Built with #1323's own fixture builder, which is the whole of what is needed here: the
# frame shifted, nothing painted on it. (230,150) puts the board low and right with 27 px
# of its coloured extent still clear of the frame edge -- whole, and a long way from the
# middle. (430,0) and (300,0) put part of it outside the picture.
g++ -std=c++17 -O1 -o /run1331/offaim /app/testers/i1323_offaim_footage.cpp $CVFLAGS || exit 1
/run1331/offaim /app/mocks/cam_1.mp4 /run1331/off_whole.avi 150 230 150 || exit 1
/run1331/offaim /app/mocks/cam_1.mp4 /run1331/off_cut.avi   150 430 0   || exit 1
/run1331/offaim /app/mocks/cam_1.mp4 /run1331/off_edge.avi  150 300 0   || exit 1

echo
echo "=== 1. the rig calibrates, and measures the boards it has always measured ==="
# #1478: the shipped mocks' column of this section and of 2.5 is gone -- their bulls, their
# full-frame boards, their margins and their fitted boards. The rig's was measured beside it
# and is the assertion now (docs/shipped-mock-census.md, A1).
run_three rig /app/mocks/rig-20260918/cam_1.mp4,/app/mocks/rig-20260918/cam_2.mp4,/app/mocks/rig-20260918/cam_3.mp4

for rig in rig; do
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
for expected in "Camera 1 bull at (671,309)" "Camera 2 bull at (626,292)" "Camera 3 bull at (700,298)"; do
  if grep -qF "$expected" /run1331/rig.txt; then say "OK   rig: $expected" ok
  else say "FAIL rig: no line saying $expected" no; fi
done
# And the six boards, measured on the FULL frame now rather than inside a frame-centred
# ellipse, are the same six boards to the pixel.
# #1729: camera 1's line was "radius 194 px ... centre (671,320)" until 7e0ca67 ("the board is
# what surrounds the rest of the board"), which credits a broken outer ring with what it
# surrounds -- rig camera 1's doubles ring is broken at the top, so its board is now the
# doubles ring at 316 px rather than the treble ring at 194. Cameras 2 and 3 key no doubles
# ring at all and did not move. Re-measured on fork 61f9bcb's dev build.
for expected in "Camera 1 board found on the FULL frame: radius 316 px across its widest, centre (672,371)" \
                "Camera 2 board found on the FULL frame: radius 195 px across its widest, centre (624,313)" \
                "Camera 3 board found on the FULL frame: radius 197 px across its widest, centre (700,328)"; do
  if grep -qF "$expected" /run1331/rig.txt; then say "OK   rig: $expected" ok
  else grep -hoE 'board found on the FULL frame[^;]*' /run1331/rig.txt | head -3
       say "FAIL rig: no line saying $expected" no; fi
done
# ADR-0079 §2 on the footage this repository ships: every one of the six has its whole
# board in shot, and the margin is a distance in pixels rather than a share of anything.
# #1729: these were 75, 39 and 55 px. 77bb5b1 ("a red room is not the board") measures the
# gap on colour within 1.8x the board's own region instead of on everything kept, which
# moved them to 215, 152 and 203; 7e0ca67's larger camera 1 board then moved its 215 to 137.
# Re-measured on fork 61f9bcb's dev build.
# #1731: 152 and 203 are now 151 and 166. Cameras 2 and 3 measure their TREBLE ring, and
# 1.8x of it held only 75% and 86% of their doubles ring, so 77bb5b1 set the board's own
# doubles ring aside as room; it goes round the board's middle and counts again. Camera 1
# measures its doubles ring and did not move. Admission and GEOMETRY SEALED are identical.
for expected in "kept comes within 137 px" "kept comes within 151 px" "kept comes within 166 px"; do
  if grep -qF "$expected" /run1331/rig.txt; then say "OK   rig: the frame edge is $expected away" ok
  else say "FAIL rig: no camera reports $expected" no; fi
done

echo
echo "=== 2. the region is drawn around the board that was found ==="
# 2.107x the board, around the board's own middle. On camera 2 of the mocks that was 663 px
# around (619,370); the ellipse it replaced was 486x316 around (640,360) whatever was in
# the picture. #1378 moved the margin from 1.25 and roi_processing.hpp carries the
# arithmetic: what this stage is handed is the largest red/green CONTOUR's span, which on
# a board whose doubles ring has dropped out of the colour mask is the TREBLE ring.
#
# #1478: this was camera 2 of the shipped mocks. On the rig it is camera 2's board from
# section 1, (624,313) at 195 px, and the region is not a new figure: it is 2.107x that
# radius, which the log truncates to whole pixels, so anything from 2.107x195 to 2.107x196
# is the same board.
RLINE=$(grep -hoE 'Camera 2 region: [0-9]+ px around the board found at \(624,313\), which measured 195 px' /run1331/rig.txt | head -1)
RPX=$(echo "$RLINE" | grep -oE 'region: [0-9]+' | grep -oE '[0-9]+')
if [ -n "$RPX" ] && python3 -c "import sys; sys.exit(0 if round(2.107*195) <= $RPX <= round(2.107*196) else 1)"; then
  say "OK   the region names the board it was drawn around, (624,313) at 195 px, and its radius $RPX px is 2.107x of it" ok
else
  grep -hoE 'region: .*' /run1331/rig.txt | head -3
  say "FAIL the region does not say which board it was drawn around, at 2.107x of it" no
fi

echo
echo "=== 2.5 #1378: the board the MOTION stage measures against, on both fixtures ==="
# Two different numbers in this pipeline are called "the board" and this is the second
# one. Section 1 above pins `bull_processing::measureBoard`'s red/green span -- a COLOUR
# measurement, taken at STEP 1 so that a region can be drawn. This pins the doubles ring
# FITTED at STEP 6, which is what #1339 makes every motion ratio a fraction of, what
# #1345 counts a dart's changed pixels inside, and what #1358 measures a window by. They
# are not the same quantity and on mocks/rig-20260918 they are not close: 194/195/197 px
# of span against a fitted ring 632/636/631 px across.
#
# It is pinned here because it has now moved twice under a tester that pins its
# consequence rather than itself -- #1331 shrank it to 36.6% and #1378 put it back -- and
# a number nothing states is a number the next region change moves again in silence.
fitted() { grep -hoE 'degrees: [0-9]+ px' /run1331/$1.txt | grep -oE '[0-9]+' | tr '\n' ' '; }
echo "rig   fitted boards: $(fitted rig)"
if [ "$(fitted rig)" = "197117 200385 194335 " ]; then
  say "OK   rig: the three fitted boards are 197117, 200385 and 194335 px" ok
else say "FAIL rig: the fitted boards are $(fitted rig), not 197117 200385 194335" no; fi

echo
echo "=== 2.6 #1378 FALSIFY: put the margin back to 1.25 and watch the rig collapse ==="
# The same binary and the same footage; only the margin moves. At 1.25 the rig's region is
# 243 px around a board whose doubles ring reaches 316, the ring is fitted out of what
# survived the cut, and the fit SUCCEEDS -- smaller. Without this the number above is a
# fixture's reading rather than a consequence of the constant.
run_three rigsmall /app/mocks/rig-20260918/cam_1.mp4,/app/mocks/rig-20260918/cam_2.mp4,/app/mocks/rig-20260918/cam_3.mp4 OD_ROI_MARGIN=1.25
echo "rig at 1.25: $(fitted rigsmall)"
# #1729: #1378 measured 72374 72531 72171 -- all three collapsed. Since 7e0ca67 camera 1's
# board is its 316 px doubles ring (section 1), so its 1.25 region is 396 px and holds the
# whole ring: camera 1 fits its full 197117 px board at either margin, and the collapse is
# cameras 2 and 3, whose board is still the treble span. Re-measured on fork 61f9bcb.
if [ "$(fitted rigsmall)" = "197117 72531 72171 " ]; then
  say "OK   at 1.25 cameras 2 and 3 collapse to 72531 and 72171 px -- 36.2% and 37.1% -- and camera 1, sized off its doubles ring, keeps 197117" ok
else say "FAIL at 1.25 the rig measured $(fitted rigsmall); #1729 measured 197117 72531 72171" no; fi
# And the line whose absence cost eight darts: the region cut coloured board, said out
# loud, per camera, with the share against the share this check allows.
# #1729: the two cameras that collapse, and only they -- camera 1's region at 1.25 cuts
# nothing, which is the control for the same line on the other two.
CUT=$(grep -cE '^\[WARN\].*drew a region that CUT coloured board' /run1331/rigsmall.txt || true)
CUTCAMS=$(grep -hoE '^\[WARN\].*Camera [0-9] drew a region that CUT' /run1331/rigsmall.txt | grep -oE 'Camera [0-9]' | sort -u | tr '\n' ' ')
if [ "$CUT" = "2" ] && [ "$CUTCAMS" = "Camera 2 Camera 3 " ]; then
  grep -hE 'drew a region that CUT' /run1331/rigsmall.txt | sed 's/.*] - //' | cut -c1-150
  say "OK   cameras 2 and 3 say their region cut coloured board, with the share, and camera 1 does not" ok
else say "FAIL $CUT of 3 cameras ($CUTCAMS) reported a region that cut coloured board; #1729 measured cameras 2 and 3" no; fi
# The control for it: at the margin this repository ships, no camera on either fixture
# says it. Section 1 already asserts both runs print no WARN at all, so this is that
# assertion said where a reader of #1378 will look for it.
QUIET=$(grep -cE 'drew a region that CUT coloured board' /run1331/rig.txt | awk -F: '{s+=$2} END {print s+0}')
if [ "$QUIET" = "0" ]; then say "OK   and the rig does not say it at 2.107" ok
else say "FAIL $QUIET cameras report a cut region at the margin this repository ships" no; fi

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
# At 430 px of shift the board is cut: the largest coloured region is the INNER part of a
# broken board, which measures a tidy 176 px sitting 56 px clear of the nearest edge -- so
# a framing question asked of the winning region alone passes it, and this one does not.
# It is refused on the averaged frame and on every further look (#1445), since #1731.
#
# #1732: the 300 px shift ("edge") was the second case here, and it is NOT a cut board.
# Its doubles ring is wholly in shot (right edge near x=1204 of 1280); what reaches the
# frame edge is the red logo and stripe on the surround, which the colour mask joins to the
# board on most frames and not on some. So the averaged frame is refused and a further look
# reads the true margin (79 px) and calibrates -- correctly. What is asserted below is that
# the look it seals is the control's own board, moved by the shift, not that it is refused.
run_one edge /run1331/off_edge.avi
run_one cut /run1331/off_cut.avi
for name in cut; do
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
# #1732: the edge clip's averaged frame is refused, and that refusal is still said once.
if grep -qE '^\[ERROR\].*Camera 1 did not calibrate: it is not looking at a WHOLE dartboard: the coloured region that is its doubles ring comes within 0 px of the edge' /run1331/edge.txt; then
  say "OK   edge: the averaged frame is refused at 0 px, the surround's red joined to the board" ok
else say "FAIL edge: the averaged frame is not refused at the frame edge, so this clip is not the case #1732 measured" no; fi
# ... and a further look admits it, sealing the CONTROL's board. The control is §3's whole
# clip, the same mock shifted (230,150) and calibrated in this run, so the expected bull is
# its bull plus (300-230, 0-150). Tolerances, MEASURED on fork 9c9deec (2026-10-02): bull
# (70,-148) against (70,-150), so 2 px, held to 3 px per axis; angle 17.89 against 17.79,
# so 0.10 deg, held to 0.5; radius 246.11 against 245.00, so 1.11 px, held to 2.5 (1%).
# wedge20 must be identical. Each figure is that run's GEOMETRY SEALED entry for camera 1.
sealed1() { grep -hoE 'GEOMETRY SEALED: camera 1 index=0 scoring=1 bull=[0-9]+,[0-9]+ star=[0-9]+ read=[0-9]+ wedge20=-?[0-9]+ angle=-?[0-9.]+ radius=[0-9.]+' /run1331/$1.txt | head -1 \
  | sed -E 's/.*bull=([0-9]+),([0-9]+) .*wedge20=(-?[0-9]+) angle=(-?[0-9.]+) radius=([0-9.]+)/\1 \2 \3 \4 \5/'; }
EDGE_SEAL=$(sealed1 edge); WHOLE_SEAL=$(sealed1 whole)
echo "edge sealed: ${EDGE_SEAL:-nothing}  |  control (whole, shift 230,150) sealed: ${WHOLE_SEAL:-nothing}"
if [ -n "$EDGE_SEAL" ] && [ -n "$WHOLE_SEAL" ] && python3 - "$EDGE_SEAL" "$WHOLE_SEAL" <<'CHECK'
import sys
e = sys.argv[1].split(); w = sys.argv[2].split()
dx, dy = 300 - 230, 0 - 150  # the edge clip's shift minus the control's
ex, ey = int(w[0]) + dx, int(w[1]) + dy
bad = []
if abs(int(e[0]) - ex) > 3 or abs(int(e[1]) - ey) > 3: bad.append(f"bull ({e[0]},{e[1]}) where the control moved by the shift is ({ex},{ey}), over 3 px")
if e[2] != w[2]: bad.append(f"wedge20 {e[2]} where the control's is {w[2]}")
if abs(float(e[3]) - float(w[3])) > 0.5: bad.append(f"angle {e[3]} against {w[3]}, over 0.5 deg")
if abs(float(e[4]) - float(w[4])) > 2.5: bad.append(f"radius {e[4]} against {w[4]}, over 2.5 px")
print("    " + ("; ".join(bad) if bad else f"bull ({e[0]},{e[1]}) vs ({ex},{ey}), wedge20 {e[2]}, angle {e[3]} vs {w[3]}, radius {e[4]} vs {w[4]}"))
sys.exit(1 if bad else 0)
CHECK
then
  say "OK   edge: a further look seals the control's board moved by the shift -- bull, wedge20, angle and radius" ok
else
  say "FAIL edge: what was sealed is not the control's board moved by the shift (or nothing was sealed)" no
fi
# #1731: and the rule that refuses it, falsified on the same binary. OD_EDGE_GAP=reach is
# 77bb5b1's reach alone: the cut doubles ring lies 79% inside 1.8x the treble ring and is
# set aside as room, the gap reads 59 px, and the cut board is admitted -- which is what
# fork main did from 77bb5b1 to #1731.
run_one cutreach /run1331/off_cut.avi OD_EDGE_GAP=reach
if grep -qF 'centre (1046,304); its boundary encloses' /run1331/cutreach.txt \
   && grep -qE 'centre \(1046,304\).*comes within 59 px of the nearest frame edge' /run1331/cutreach.txt \
   && ! grep -qE 'Camera 1 did not calibrate: it is not looking at a WHOLE dartboard' /run1331/cutreach.txt; then
  say "OK   cut under OD_EDGE_GAP=reach: the doubles ring is set aside, the gap reads 59 px and no frame-edge refusal -- the ring rule is what refuses it" ok
else
  grep -hoE 'comes within [0-9]+ px of the nearest frame edge|Camera 1 did not calibrate[^:]*: [^:]*' /run1331/cutreach.txt | head -2
  say "FAIL cut under OD_EDGE_GAP=reach is not 77bb5b1's 59 px admission, so §5 is not measured against anything that can fail" no
fi
# The half of §5 that makes the other half mean something: at 430 px the region a framing
# check would otherwise have asked about looks entirely healthy.
if grep -qF 'board found on the FULL frame: radius 176 px across its widest, centre (1046,304)' /run1331/cut.txt; then
  say "OK   cut: the largest region alone measures a plausible 176 px board, and is still refused" ok
else
  grep -hoE 'board found on the FULL frame[^;]*' /run1331/cut.txt | head -1
  say "FAIL cut: the region this case is about is not the one that was measured" no
fi

echo
echo "=== 6. a red room is not the board (77bb5b1), on mocks/rig-20260929 ==="
# #1731: nothing in testers/ pinned 77bb5b1's own case, so §5's repair could have undone
# it unseen. rig-20260929's camera 1 keys a 37776 px strip of the room as red/green along
# the top of its picture, hard against y=0: that strip is 0% inside the board's reach and
# its hull misses the board's middle. Measured on everything kept, as before 77bb5b1, the
# camera is refused as clipped at 0 px with its whole board in shot; measured on the
# board's colour it clears the edge by 149 px and the rig calibrates 3 of 3.
run_three room /app/mocks/rig-20260929/cam_1.mp4,/app/mocks/rig-20260929/cam_2.mp4,/app/mocks/rig-20260929/cam_3.mp4
run_three roomall /app/mocks/rig-20260929/cam_1.mp4,/app/mocks/rig-20260929/cam_2.mp4,/app/mocks/rig-20260929/cam_3.mp4 OD_EDGE_GAP=everything
if grep -q 'Initial calibration completed successfully on 3 of 3' /run1331/room.txt; then
  say "OK   room: rig-20260929 calibrates on all three cameras" ok
else say "FAIL room: rig-20260929 did not calibrate on all three cameras" no; fi
if grep -qE 'Camera 1: [0-9]+ red/green px lie outside 1.8x the board.s own region, go round none of it, and are the room.s' /run1331/room.txt \
   && grep -qE 'Camera 1 board found on the FULL frame: .*comes within 149 px of the nearest frame edge' /run1331/room.txt; then
  say "OK   room: camera 1 sets the room's colour aside and clears the frame edge by 149 px" ok
else
  grep -hoE 'Camera 1 board found on the FULL frame: .*' /run1331/room.txt | head -1
  say "FAIL room: camera 1 does not set the room aside and clear the edge by 149 px" no
fi
if grep -qE '^\[ERROR\].*Camera 1 did not calibrate: it is not looking at a WHOLE dartboard: the coloured region that is its doubles ring comes within 0 px of the edge' /run1331/roomall.txt; then
  say "OK   room under OD_EDGE_GAP=everything: camera 1 is refused as clipped at 0 px -- the fixture holds a red room" ok
else
  grep -hE '^\[ERROR\]' /run1331/roomall.txt | head -1 | cut -c1-200
  say "FAIL room under OD_EDGE_GAP=everything: camera 1 is not refused at 0 px, so §6 proves nothing" no
fi

echo
echo "CHECK_RC=$FAILED"
exit $FAILED
