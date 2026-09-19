set -u
# #1394: the colour stage's four windows are fractions of the BOARD, not of the frame --
# measured, on the binary in build/, on both fixtures and on one binary.
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

MOCKS=/app/mocks/cam_1.mp4,/app/mocks/cam_2.mp4,/app/mocks/cam_3.mp4
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
win() { # $1 run, $2 which window
  grep -hoE "Camera [0-9] colour windows off [^;]*" /run1394/$1.txt | sort -u \
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

echo "=== 1. both fixtures calibrate, with the six bull centres and six boards ======="
run mocks "$MOCKS"
run rig "$RIG"
for fix in mocks rig; do
  if grep -q 'Initial calibration completed successfully on 3 of 3' /run1394/$fix.txt; then
    say "OK   $fix calibrates on all three cameras" ok
  else say "FAIL $fix did not calibrate on all three cameras" no; fi
  if [ "$(noise $fix)" = "0" ]; then say "OK   $fix prints no ERROR and no WARN" ok
  else grep -E '^\[(ERROR|WARN)\]' /run1394/$fix.txt | head -3
       say "FAIL $fix prints $(noise $fix) ERROR/WARN lines" no; fi
done
for e in "Camera 1 bull at (616,283)" "Camera 2 bull at (651,313)" "Camera 3 bull at (654,293)"; do
  if grep -qF "$e" /run1394/mocks.txt; then say "OK   mocks: $e" ok
  else say "FAIL mocks: no line saying $e" no; fi
done
for e in "Camera 1 bull at (671,309)" "Camera 2 bull at (626,292)" "Camera 3 bull at (700,298)"; do
  if grep -qF "$e" /run1394/rig.txt; then say "OK   rig: $e" ok
  else say "FAIL rig: no line saying $e" no; fi
done
if [ "$(fitted mocks)" = "183859 173006 175444 " ]; then
  say "OK   mocks: 1331-framing 2.5's three fitted boards, 183859, 173006 and 175444 px" ok
else say "FAIL mocks fitted $(fitted mocks), not 183859 173006 175444" no; fi
if [ "$(fitted rig)" = "197117 200385 194335 " ]; then
  say "OK   rig: 1331-framing 2.5's three fitted boards, 197117, 200385 and 194335 px" ok
else say "FAIL rig fitted $(fitted rig), not 197117 200385 194335" no; fi

echo
echo "=== 2. the window sizes per camera, and what they used to be =================="
# The before/after #1394 asks for, on ONE binary.
run mocksframe "$MOCKS" OD_COLOUR_WINDOWS=frame
run rigframe "$RIG" OD_COLOUR_WINDOWS=frame
grep -hoE "Camera [0-9] colour windows off [^;]*" /run1394/mocks.txt | sort -u
grep -hoE "Camera [0-9] colour windows off [^;]*" /run1394/rig.txt | sort -u
if [ "$(spans mocks)" = "289 312 306 " ] && [ "$(spans rig)" = "314 192 194 " ]; then
  say "OK   the spans the windows are fractions of: mocks 289/312/306, rig 314/192/194" ok
else say "FAIL spans read mocks '$(spans mocks)' and rig '$(spans rig)'" no; fi
if [ "$(win mocks "bull's-eye")" = "168 182 178 " ] && [ "$(win rig "bull's-eye")" = "182 112 113 " ]; then
  say "OK   bull's-eye: mocks 168/182/178 px, rig 182/112/113 px -- 0.582 of each span" ok
else say "FAIL bull's-eye read mocks '$(win mocks "bull's-eye")' and rig '$(win rig "bull's-eye")'" no; fi
if [ "$(win mocks centrality)" = "459 496 486 " ] && [ "$(win rig centrality)" = "498 306 309 " ]; then
  say "OK   centrality: mocks 459/496/486 px, rig 498/306/309 px -- 1.0 board radii" ok
else say "FAIL centrality read mocks '$(win mocks centrality)' and rig '$(win rig centrality)'" no; fi
if [ "$(win mocks connectivity)" = "140 152 149 " ] && [ "$(win rig connectivity)" = "152 94 94 " ]; then
  say "OK   connectivity: mocks 140/152/149 px, rig 152/94/94 px -- 0.306 board radii" ok
else say "FAIL connectivity read mocks '$(win mocks connectivity)' and rig '$(win rig connectivity)'" no; fi
# The one window that did NOT move, and it is asserted rather than left unsaid.
if [ "$(win mocks "outer cutoff")" = "384 384 384 " ] && [ "$(win rig "outer cutoff")" = "384 384 384 " ]; then
  say "OK   the outer cutoff is 384 px on all six under BOTH rules; ColorParams says why" ok
else say "FAIL the outer cutoff moved: mocks '$(win mocks "outer cutoff")', rig '$(win rig "outer cutoff")'" no; fi
# #1340's rule: a switch that only ever refuses passes any test asking it to refuse.
if [ "$(win mocksframe "bull's-eye")" = "128 128 128 " ] && [ "$(win rigframe centrality)" = "320 320 320 " ] \
   && [ "$(win mocks "bull's-eye")" != "$(win mocksframe "bull's-eye")" ] \
   && [ "$(win rig centrality)" != "$(win rigframe centrality)" ]; then
  say "OK   OD_COLOUR_WINDOWS=frame really puts six 128s and six 320s back, on both fixtures" ok
else say "FAIL OD_COLOUR_WINDOWS=frame changed nothing; everything below it proves nothing" no; fi

echo
echo "=== 3. WHAT THE WINDOWS REALLY KEEP, counted rather than inferred ============="
# #1393's rule one stage over: an unmoved ellipse is consistent both with a stage that
# kept the same pixels and with one that kept different pixels the fit shrugged off.
echo "mocks kept: $(kept mocks) (board) vs $(kept mocksframe) (frame)"
echo "rig   kept: $(kept rig) (board) vs $(kept rigframe) (frame)"
if [ "$(kept mocks)" = "38563 39763 48386 " ] && [ "$(kept mocksframe)" = "38537 39763 48386 " ]; then
  say "OK   mocks keep 38563, 39763 and 48386 px against the frame rule's 38537, 39763 and 48386" ok
else say "FAIL mocks kept $(kept mocks) board and $(kept mocksframe) framed" no; fi
if [ "$(kept rig)" = "50876 65205 65278 46870 " ] && [ "$(kept rigframe)" = "49563 65205 65278 46870 " ]; then
  say "OK   rig keeps 50876 px on camera 1 against the frame rule's 49563 -- 1313 px, and nothing elsewhere" ok
else say "FAIL rig kept $(kept rig) board and $(kept rigframe) framed" no; fi
# THE FINDING, and it is why this slice is one piece of work rather than two. Once the
# floor lets rig camera 1 measure its board at all, the frame-sized windows drawn around
# THAT centre lose its bull outright -- 4 regions scored, 4 refused on size -- and the rig
# falls to 2 of 3 cameras. #1323 moved the origin and left the scale, and on this camera
# the old floor was all that stood between that pairing and a refused camera. So the
# control for "what shipped before #1394" is BOTH switches, not one.
if grep -q 'Camera 1 did not calibrate: the bull could not be found' /run1394/rigframe.txt \
   && grep -q '2 of 3 are looking at the dartboard' /run1394/rigframe.txt; then
  say "OK   board centre + FRAME windows loses rig camera 1's bull; the window size is load-bearing" ok
else say "FAIL the frame-sized windows kept rig camera 1's bull, so the size decides nothing here" no; fi
run rigbefore "$RIG" "OD_COLOUR_WINDOWS=frame OD_BOARD_FLOOR=area"
run mocksbefore "$MOCKS" "OD_COLOUR_WINDOWS=frame OD_BOARD_FLOOR=area"
if [ "$(fitted mocksbefore)" = "$(fitted mocks)" ] && [ "$(fitted rigbefore)" = "$(fitted rig)" ] \
   && [ "$(noise rigbefore)" = "0" ] && [ "$(noise mocksbefore)" = "0" ]; then
  say "OK   and against the exact pre-#1394 state -- both switches -- the six fitted boards are identical" ok
else say "FAIL pre-#1394 fitted $(fitted mocksbefore) and $(fitted rigbefore)" no; fi
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
grep -hoE 'Camera [0-9] frame-keyed stages.*' /run1394/mocks.txt /run1394/rig.txt | sort -u | cut -c1-200
OFFBOARD=$(grep -hoE "rectangle adds [0-9]+ px of red on the board and [0-9]+ px off it" \
  /run1394/mocks.txt /run1394/rig.txt | grep -oE 'and [0-9]+ px off' | grep -oE '[0-9]+' | sort -u | tr '\n' ' ')
if [ "$OFFBOARD" = "0 " ]; then
  say "OK   SECTION 5's frame-centred rectangle adds NO red off the board on any of the six" ok
else say "FAIL SECTION 5 added red off the board: $OFFBOARD" no; fi
RAMP=$(grep -hoE "which is [0-9.]+% of the range it spends on the whole frame" /run1394/rig.txt \
  | grep -oE '[0-9.]+%' | sort -u | tr '\n' ' ')
if [ "$RAMP" = "41.0% 41.9% 68.9% " ]; then
  say "OK   SECTION 2's ramp spends 41.0%, 41.9% and 68.9% of its range across the rig's boards" ok
else say "FAIL SECTION 2's ramp spends $RAMP across the rig's boards" no; fi
UNREACHED=$(grep -hoE "left [0-9]+ px of the board's own upper half" /run1394/mocks.txt \
  | grep -oE '[0-9]+' | sort -un | tr '\n' ' ')
if [ "$UNREACHED" = "0 436 " ]; then
  say "OK   SECTION 6.5's repair leaves 436 px of mocks camera 2's upper board unreached, and 0 elsewhere" ok
else say "FAIL SECTION 6.5 left $UNREACHED px of the mocks' upper boards unreached" no; fi
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
if [ "$(spans half)" = "91 " ]; then
  say "OK   at half size the board spans 91 px, and the windows are fractions of that" ok
else say "FAIL at half size the span reads '$(spans half)'" no; fi
if [ "$(win half "bull's-eye")" = "53 " ] && [ "$(win half centrality)" = "144 " ] \
   && [ "$(win half connectivity)" = "44 " ]; then
  say "OK   53, 144 and 44 px -- against the frame rule's 128, 320 and 96 on the same footage" ok
else say "FAIL at half size the windows read $(win half "bull's-eye") $(win half centrality) $(win half connectivity)" no; fi
# The finding. The frame windows on a half-size board keep half again as much.
echo "half kept: $(kept half) (board) vs $(kept halfframe) (#1323's state) vs $(kept halfarea) (pre-#1323)"
# The finding, and it is a three-way rather than a pair. #1323 moved the ORIGIN onto the
# board and left the SCALE on the frame, so `halfframe` -- the board's middle with the
# frame's windows -- is exactly the state this issue was filed against, and it is the
# honest control. On a board half the size it keeps 23145 px inside the region where both
# on the board keeps 15191: 7954 px, 52% more, of what the windows exist to remove.
if [ "$(kept half)" = "15191 " ] && [ "$(kept halfframe)" = "15732 23145 " ] \
   && [ "$(kept halfarea)" = "15446 22859 " ]; then
  say "OK   at half size: 15191 px on the board, 23145 px with #1323's frame-sized windows, 22859 px before #1323" ok
else say "FAIL at half size the stage kept $(kept half), $(kept halfframe) and $(kept halfarea)" no; fi
# And the floor, which is what stops any of it happening at all.
if grep -q 'Camera 1 has no board to measure here: OD_BOARD_FLOOR=area' /run1394/halfarea.txt \
   && grep -q 'the largest coloured region spans 91 px' /run1394/half.txt; then
  say "OK   the old floor refuses that 91 px board outright, so every window falls back to the frame" ok
else say "FAIL the old floor did not refuse the half-size board; phase 4's answer is not general" no; fi

echo
if [ $FAILED -eq 0 ]; then echo "1394-windows: PASS"; else echo "1394-windows: FAIL"; fi
exit $FAILED
