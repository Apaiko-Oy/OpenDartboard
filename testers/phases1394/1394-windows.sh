set -u
# #1394: the colour stage's four windows are fractions of the BOARD, not of the frame --
# measured, on the binary in build/, on the rig and on one binary.
#
# #1478: the shipped mocks' column of sections 1 to 5 is gone -- their bulls, fitted
# boards, spans, windows and kept pixels. The rig's was measured beside it and is the
# assertion now (docs/shipped-mock-census.md, A1). Section 6 is NOT re-pointed: it halves
# the board in frame, and the rig is 1.07x from not calibrating at all (1339-denominator's
# header), so there is nothing at half size to make from it. It stays on the mocks' camera 1.
#
# #1323 moved these four distances onto the board and deliberately left their SIZE a
# fraction of the frame's width, so the origin was board-relative and the scale was not.
# At 1280 wide they are a fixed 320, 128, 384 and 96 px on every camera, every rig and
# every mounting: the same window on a board 90 px across and on one 310 px across.
#
# EVERY detector run is bounded by its own recorded pid. A board that cannot calibrate
# goes to #895's fault vigil and stays up on purpose, and OD_MAX_CYCLES does not bound it.
# Phase 6 hands the detector ONE camera, which can never reach the three motion detection
# initialises on, so those runs fault by construction and none would return on its own.

FAILED=0
say() { echo "$1"; [ "$2" = ok ] || FAILED=1; }

RIG=/app/mocks/rig-20260918/cam_1.mp4,/app/mocks/rig-20260918/cam_2.mp4,/app/mocks/rig-20260918/cam_3.mp4
CVFLAGS="$(pkg-config --cflags --libs opencv4)"

run() { # $1 name, $2 cams, $3 optional VAR=value for this run only
  mkdir -p /run1394/$1
  ( cd /run1394/$1 && export ${3:-OD_NOTHING_AT_ALL=1} && OD_MAX_CYCLES=20 \
      exec /app/build/opendartboard --debug --cams $2 --width 1280 --height 720 \
      > /run1394/$1.out 2>&1 ) &
  local P=$!
  local W=0
  while kill -0 $P 2>/dev/null && [ $W -lt 150 ]; do sleep 2; W=$((W + 2)); done
  kill -TERM $P 2>/dev/null
  wait $P 2>/dev/null
  sed 's/\x1b\[[0-9;]*m//g' /run1394/$1.out > /run1394/$1.txt
}

# "168 182 178 " -- the bull's-eye window each camera chose, in camera order.
# #1407: the outer cutoff now differs between the two passes, so it is taken out of the
# line before the de-duplication for every window but itself.
win() { # $1 run, $2 which window
  grep -hoE "Camera [0-9] colour windows off [^;]*" /run1394/$1.txt \
    | if [ "$2" = "outer cutoff" ]; then cat; else sed -E 's/outer cutoff [0-9]+ px, //'; fi | sort -u \
    | grep -oE "$2 [0-9]+ px" | grep -oE '[0-9]+' | tr '\n' ' '; }
# The span each camera measured, which is the length every window is a fraction of.
spans() { grep -hoE 'Camera [0-9] colour windows off a board spanning [0-9]+' /run1394/$1.txt \
  | sort -u | grep -oE '[0-9]+ *$' | tr '\n' ' '; }
# What the stage actually kept, per camera, from its own census -- the only honest answer
# to "did the new windows mask anything", and the reason this slice prints it at all.
kept() { grep -hoE 'Camera [0-9] colour windows kept [0-9]+ px' /run1394/$1.txt | sort -u \
  | grep -oE 'kept [0-9]+' | grep -oE '[0-9]+' | tr '\n' ' '; }
fitted() { grep -hoE 'degrees: [0-9]+ px' /run1394/$1.txt | grep -oE '[0-9]+' | tr '\n' ' '; }
noise() { grep -cE '^\[(ERROR|WARN)\]' /run1394/$1.txt; }

echo "=== 1. the rig calibrates, with its three bull centres and three boards ========"
run rig "$RIG"
for fix in rig; do
  if grep -q 'Initial calibration completed successfully on 3 of 3' /run1394/$fix.txt; then
    say "OK   $fix calibrates on all three cameras" ok
  else say "FAIL $fix did not calibrate on all three cameras" no; fi
  if [ "$(noise $fix)" = "0" ]; then say "OK   $fix prints no ERROR and no WARN" ok
  else grep -E '^\[(ERROR|WARN)\]' /run1394/$fix.txt | head -3
       say "FAIL $fix prints $(noise $fix) ERROR/WARN lines" no; fi
done
for e in "Camera 1 bull at (671,309)" "Camera 2 bull at (626,292)" "Camera 3 bull at (700,298)"; do
  if grep -qF "$e" /run1394/rig.txt; then say "OK   rig: $e" ok
  else say "FAIL rig: no line saying $e" no; fi
done
if [ "$(fitted rig)" = "197117 200385 194335 " ]; then
  say "OK   rig: 1331-framing 2.5's three fitted boards, 197117, 200385 and 194335 px" ok
else say "FAIL rig fitted $(fitted rig), not 197117 200385 194335" no; fi

echo
echo "=== 2. the window sizes per camera, and what they used to be =================="
# The before/after #1394 asks for, on ONE binary.
# #1729: OD_CALIBRATION_LOOKS=once, because what this arm reconstructs is #1323's state, and
# #1323 had one look. Since #1445 (9d699de) a camera refused on the averaged frame is looked
# at again, and under the frame windows rig cameras 1 and 3 each find a bull on a later
# single frame, so the default run ends 3 of 3 and section 3's refusal is buried under
# eleven retries apiece. The averaged frame's verdict is the one #1394 is about.
run rigframe "$RIG" "OD_COLOUR_WINDOWS=frame OD_CALIBRATION_LOOKS=once"
grep -hoE "Camera [0-9] colour windows off [^;]*" /run1394/rig.txt | sort -u
# #1729: camera 3 was 194. 7e0ca67 ("the board is what surrounds the rest of the board")
# credits a candidate region with every region whose middle lies inside its hull, and rig
# camera 3's colour-stage board is now 334 px across. Its windows below follow it: 0.582,
# 1.0 and 0.306 of half that span were 113, 309 and 94 px and are 195, 531 and 163.
# Re-measured on fork 61f9bcb's dev build.
if [ "$(spans rig)" = "314 192 334 " ]; then
  say "OK   the spans the windows are fractions of: rig 314/192/334" ok
else say "FAIL spans read rig '$(spans rig)'" no; fi
if [ "$(win rig "bull's-eye")" = "182 112 195 " ]; then
  say "OK   bull's-eye: rig 182/112/195 px -- 0.582 of each span" ok
else say "FAIL bull's-eye read rig '$(win rig "bull's-eye")'" no; fi
if [ "$(win rig centrality)" = "498 306 531 " ]; then
  say "OK   centrality: rig 498/306/531 px -- 1.0 board radii" ok
else say "FAIL centrality read rig '$(win rig centrality)'" no; fi
if [ "$(win rig connectivity)" = "152 94 163 " ]; then
  say "OK   connectivity: rig 152/94/163 px -- 0.306 board radii" ok
else say "FAIL connectivity read rig '$(win rig connectivity)'" no; fi
# The one window #1394 did NOT move. #1407 moved it on STEP 2.5's pass, to the board's rim
# in board radii, and `1407-cutoff` holds those numbers; here only the frame rule is held:
# STEP 1's pass keeps 384 px, and OD_COLOUR_WINDOWS=frame puts 384 back on both passes.
if [ "$(win rig "outer cutoff")" = "384 420 384 412 384 416 " ] \
   && [ "$(win rigframe "outer cutoff")" = "384 384 384 " ]; then
  say "OK   the outer cutoff: 384 px on STEP 1's pass, the rim on STEP 2.5's, 384 again under the frame rule" ok
else say "FAIL the outer cutoff read rig '$(win rig "outer cutoff")', framed '$(win rigframe "outer cutoff")'" no; fi
# #1340's rule: a switch that only ever refuses passes any test asking it to refuse.
if [ "$(win rigframe "bull's-eye")" = "128 128 128 " ] && [ "$(win rigframe centrality)" = "320 320 320 " ] \
   && [ "$(win rig "bull's-eye")" != "$(win rigframe "bull's-eye")" ] \
   && [ "$(win rig centrality)" != "$(win rigframe centrality)" ]; then
  say "OK   OD_COLOUR_WINDOWS=frame really puts three 128s and three 320s back, on the rig" ok
else say "FAIL OD_COLOUR_WINDOWS=frame changed nothing; everything below it proves nothing" no; fi

echo
echo "=== 3. WHAT THE WINDOWS REALLY KEEP, counted rather than inferred ============="
# #1393's rule one stage over: an unmoved ellipse is consistent both with a stage that
# kept the same pixels and with one that kept different pixels the fit shrugged off.
echo "rig   kept: $(kept rig) (board) vs $(kept rigframe) (frame)"
# #1729: camera 3 kept 46870 under both rules. Since 7e0ca67 its board-sized windows keep
# 50740 and the frame windows 45598 -- the camera whose span moved is now the second one the
# window size decides. Re-measured on fork 61f9bcb's dev build, the frame arm at one look.
if [ "$(kept rig)" = "50876 65205 65278 50740 " ] && [ "$(kept rigframe)" = "49563 65205 65278 45598 " ]; then
  say "OK   rig keeps 50876 and 50740 px on cameras 1 and 3 against the frame rule's 49563 and 45598, and nothing moves on camera 2" ok
else say "FAIL rig kept $(kept rig) board and $(kept rigframe) framed" no; fi
# THE FINDING, and it is why this slice is one piece of work rather than two. Once the
# floor lets rig camera 1 measure its board at all, the frame-sized windows drawn around
# THAT centre lose its bull outright -- 4 regions scored, 4 refused on size -- and the rig
# falls to 2 of 3 cameras. #1323 moved the origin and left the scale, and on this camera
# the old floor was all that stood between that pairing and a refused camera. So the
# control for "what shipped before #1394" is BOTH switches, not one.
# #1729: it was camera 1 alone and 2 of 3. Since 7e0ca67 camera 3's board is sized off
# 334 px too, and the frame windows around its centre lose its bull the same way.
if grep -q 'Camera 1 did not calibrate: the bull could not be found' /run1394/rigframe.txt \
   && grep -q 'Camera 3 did not calibrate: the bull could not be found' /run1394/rigframe.txt \
   && grep -q 'CAMERAS: 1 of 3 are looking at the dartboard (2)' /run1394/rigframe.txt; then
  say "OK   board centre + FRAME windows loses rig cameras 1 and 3's bulls; the window size is load-bearing" ok
else say "FAIL the frame-sized windows kept rig camera 1's bull, so the size decides nothing here" no; fi
run rigbefore "$RIG" "OD_COLOUR_WINDOWS=frame OD_BOARD_FLOOR=area"
if [ "$(fitted rigbefore)" = "$(fitted rig)" ] && [ "$(noise rigbefore)" = "0" ]; then
  say "OK   and against the exact pre-#1394 state -- both switches -- the rig's three fitted boards are identical" ok
else say "FAIL pre-#1394 fitted $(fitted rigbefore)" no; fi
echo
echo "=== 4. THE FLOOR: the camera the old one refused ==============================="
# minBoardAreaPercent is #1394's fourth criterion. It decides whether the windows are
# drawn around the board at all, and rig camera 1 is where it says no to a board that is
# plainly there: 2.45% of the frame here, a 194 px radius one stage later.
run rigarea "$RIG" OD_BOARD_FLOOR=area
grep -hoE 'Camera 1 (board measured from the coloured mask|has no board to measure here)[^(]*' \
  /run1394/rig.txt /run1394/rigarea.txt | sort -u | cut -c1-160
if grep -q 'Camera 1 board measured from the coloured mask: the largest coloured region spans 314 px' /run1394/rig.txt; then
  say "OK   on the span floor rig camera 1 measures a board spanning 314 px" ok
else say "FAIL rig camera 1 did not measure a board on the span floor" no; fi
if grep -q 'Camera 1 has no board to measure here: OD_BOARD_FLOOR=area' /run1394/rigarea.txt; then
  say "OK   and the old floor refuses that same camera, on the same binary" ok
else say "FAIL OD_BOARD_FLOOR=area did not refuse rig camera 1; the switch decides nothing" no; fi
if grep -qF 'Camera 1 bull at (671,309)' /run1394/rigarea.txt \
   && grep -q 'Initial calibration completed successfully on 3 of 3' /run1394/rigarea.txt; then
  say "OK   and the bull is at (671,309) and the rig calibrates 3 of 3 under either floor" ok
else say "FAIL the floor moved the bull or the calibration on the rig" no; fi

echo
echo "=== 5. the three frame-keyed sites that STAY, counted ========================="
# Each of these is a measured reason rather than an argument; color_processing.cpp carries
# the reasoning and this is where the numbers come from.
grep -hoE 'Camera [0-9] frame-keyed stages.*' /run1394/rig.txt | sort -u | cut -c1-200
OFFBOARD=$(grep -hoE "rectangle adds [0-9]+ px of red on the board and [0-9]+ px off it" \
  /run1394/rig.txt | grep -oE 'and [0-9]+ px off' | grep -oE '[0-9]+' | sort -u | tr '\n' ' ')
if [ "$OFFBOARD" = "0 " ]; then
  say "OK   SECTION 5's frame-centred rectangle adds NO red off the board on any of the rig's three" ok
else say "FAIL SECTION 5 added red off the board: $OFFBOARD" no; fi
RAMP=$(grep -hoE "which is [0-9.]+% of the range it spends on the whole frame" /run1394/rig.txt \
  | grep -oE '[0-9.]+%' | sort -u | tr '\n' ' ')
# #1729: 41.0% 41.9% 68.9% until 7e0ca67, whose larger camera 3 board takes it to 72.3%.
# Re-measured on fork 61f9bcb's dev build.
if [ "$RAMP" = "41.0% 68.9% 72.3% " ]; then
  say "OK   SECTION 2's ramp spends 41.0%, 68.9% and 72.3% of its range across the rig's boards" ok
else say "FAIL SECTION 2's ramp spends $RAMP across the rig's boards" no; fi
# #1478: this was asked of the shipped mocks alone -- 436 px of their camera 2's upper
# board unreached and 0 on the other two. It is asked of the rig now, at what the rig
# measured on fork f15173f's dev build: 0, 10620 and 13161 px. Taken on a run where this
# file's other rig pins were already red on main (the camera 3 span reads 334, not 194),
# so it is a measurement of this build and is reported on #1478 as such.
UNREACHED=$(grep -hoE "left [0-9]+ px of the board's own upper half" /run1394/rig.txt \
  | grep -oE '[0-9]+' | sort -un | tr '\n' ' ')
echo "SECTION 6.5 on the rig: '${UNREACHED}'"
if [ "$UNREACHED" = "0 10620 13161 " ]; then
  say "OK   SECTION 6.5's repair leaves 0, 10620 and 13161 px of the rig's upper boards unreached" ok
else say "FAIL SECTION 6.5 left $UNREACHED px of the rig's upper boards unreached" no; fi
echo
echo "=== 6. FALSIFY: the board at half the size ===================================="
# #1339's own scaler moves the single property this issue is about -- how much of the
# frame the board fills -- by a known factor, on one clip with every dart and every arm in
# it unchanged. One camera, so the board faults by construction; the camera itself
# calibrates and every number below is printed before that.
g++ -std=c++17 -O1 -o /run1394/scaled /app/testers/i1339_scaled_footage.cpp $CVFLAGS || exit 1
/run1394/scaled /app/mocks/cam_1.mp4 /run1394/half.avi 0.5 400 > /dev/null || exit 1
run half /run1394/half.avi
run halfframe /run1394/half.avi OD_COLOUR_WINDOWS=frame
run halfarea /run1394/half.avi OD_BOARD_FLOOR=area
grep -hoE "Camera 1 colour windows off [^;]*" /run1394/half.txt /run1394/halfframe.txt /run1394/halfarea.txt | sort -u
# The controls that make the difference attributable to the windows alone.
for n in half halfframe halfarea; do
  if grep -q 'CAMERAS: 1 of 1 are looking at the dartboard' /run1394/$n.txt \
     && grep -qF 'Camera 1 bull at (628,322)' /run1394/$n.txt; then
    say "OK   at half size $n calibrates its camera off the bull at (628,322)" ok
  else say "FAIL at half size $n did not calibrate off (628,322)" no; fi
done
# #1729: the half-size board spanned 91 px until 7e0ca67, which credits a broken outer ring
# with what it surrounds; it spans 170 px since, and the windows
# below are fractions of that. Re-measured on fork 61f9bcb's dev build.
if [ "$(spans half)" = "170 " ]; then
  say "OK   at half size the board spans 170 px, and the windows are fractions of that" ok
else say "FAIL at half size the span reads '$(spans half)'" no; fi
if [ "$(win half "bull's-eye")" = "99 " ] && [ "$(win half centrality)" = "269 " ] \
   && [ "$(win half connectivity)" = "82 " ]; then
  say "OK   99, 269 and 82 px -- against the frame rule's 128, 320 and 96 on the same footage" ok
else say "FAIL at half size the windows read $(win half "bull's-eye") $(win half centrality) $(win half connectivity)" no; fi
# The finding. The frame windows on a half-size board keep half again as much.
echo "half kept: $(kept half) (board) vs $(kept halfframe) (#1323's state) vs $(kept halfarea) (pre-#1323)"
# The finding, and it is a three-way rather than a pair. #1323 moved the ORIGIN onto the
# board and left the SCALE on the frame, so `halfframe` -- the board's middle with the
# frame's windows -- is exactly the state this issue was filed against, and it is the
# honest control. On a board half the size it keeps 23145 px inside the region where both
# on the board keeps 15191: 7954 px, 52% more, of what the windows exist to remove.
# #1729: #1394 measured 15191 against 23145 -- 52% more -- when this board's windows were
# fractions of a 91 px span. On the 170 px span it measures since 7e0ca67 the board-sized
# windows are within a quarter of the frame's, and the margin is what that leaves: 19609 and
# 21233 px on the stage's two passes against 22859 on both frame arms, 17% and 8%
# more. Re-measured on fork 61f9bcb's dev build.
if [ "$(kept half)" = "19609 21233 " ] && [ "$(kept halfframe)" = "22859 " ] \
   && [ "$(kept halfarea)" = "22859 " ]; then
  say "OK   at half size: 19609 and 21233 px on the board, 22859 px with #1323's frame-sized windows and before #1323" ok
else say "FAIL at half size the stage kept $(kept half), $(kept halfframe) and $(kept halfarea)" no; fi
# And the floor, which is what stops any of it happening at all.
if grep -q 'Camera 1 has no board to measure here: OD_BOARD_FLOOR=area' /run1394/halfarea.txt \
   && grep -q 'the largest coloured region spans 170 px' /run1394/half.txt; then
  say "OK   the old floor refuses that 170 px board outright, so every window falls back to the frame" ok
else say "FAIL the old floor did not refuse the half-size board; phase 4's answer is not general" no; fi

echo
if [ $FAILED -eq 0 ]; then echo "1394-windows: PASS"; else echo "1394-windows: FAIL"; fi
exit $FAILED
