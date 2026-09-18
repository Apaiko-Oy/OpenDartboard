set -u
# #1321: the failure and the control, in one run, with no --debug on either.
# #1324: two failures, at two named stages, because one of them stopped being the one
#        this tester was written about and the tester stayed green.
#
# The failing inputs are made here rather than committed: clips of the mock footage
# dimmed by i1321_dark_footage, which is 300 kB of build output per clip and gives the
# same failure on any checkout from the same source and the same scale.
#
# WHICH STAGE EACH SCALE REACHES, MEASURED (2026-09-18, dev build with the defines
# run_all.sh uses; three cameras unless said otherwise, camera by camera):
#
#   0.18   bull    bull    bull        the red/green frame yields 0 contours
#   0.30   bull      -       -         the largest red/green region encloses 4255 px
#   0.40   bull      -       -         ... 6751 px, and a board has to enclose 36864
#   0.45   bull    bull    bull        ... 12669, 14329, 14674 px
#   0.47   DOUBLES wires   wires       camera 1: 34 of 120 rays gave a boundary point
#   0.48   wires   wires   wires       2, 3 and 0 of the 20 wire boundaries
#   0.49   wires   wires   wires       3, 4 and 0
#   0.50   DOUBLES DOUBLES wires       camera 1: 40, camera 2: 26, of the 50 rays needed
#   0.55   wires   wires   wires       17, 18 and 18
#
# The doubles stage is a narrow window and it is not monotonic in brightness: camera 1
# falls into it at 0.47, out of it at 0.48 and 0.49, and back into it at 0.50. That is
# not noise in the tester -- the input is one file built the same way every time and the
# detector calibrates on one frame of it (frame 90, because run_all.sh builds with
# DEBUG_SEEK_VIDEO), so the numbers repeat exactly; 0.50 was measured twice, on one
# camera and on three, and gave 40 both times. It is the ray validator's rolling baseline
# being a step function of how much of the ring keys, and it is the reason every
# assertion below names its stage: when a change to the pipeline moves this input into
# the next stage down, this tester has to say so instead of going on passing.
#
# #1321 built this fixture at 0.18 for the doubles stage and got it: there was no bull
# refusal then, so a frame with nothing in it went on to build an empty doubles mask and
# failed on the white-pixel count. #1320 put a bull refusal above that stage, and at 18%
# the bull stage now refuses first -- so this tester went on passing, on the neighbouring
# sentence, while the branch it exists for stopped being exercised at all. That is #1324.
#
# So there are two dimmed inputs and each one is here for a stage:
#
# #1331 NOTE, AND IT IS NOT FIXED HERE. ADR-0079 turned the first two stages of the
# pipeline around -- the board is found on the full frame and the region is drawn around
# what was found -- and the dimmed camera now gets FURTHER than it did: at 0.50 it traces
# its doubles ring and falls out at the WIRE stage with 5 of 20 boundaries, where it used
# to be refused at the doubles stage. That is the pipeline getting better at a dim board
# and it is the thing #1324 built the stage names to make visible, so section 1 is left
# asserting `doubles` and left RED rather than re-pointed at the neighbouring stage.
#
# What a re-pick has to start from, measured on this tree under ADR-0079's ordering, one
# camera per run, 120 frames, no --debug:
#
#   cam_1  0.40 bull | 0.42 bull | 0.45 wires(2) | 0.47 wires(2) | 0.50 wires(5)
#          0.52 wires(4) | 0.55 wires(13) | 0.60, 0.65, 0.70 calibrate
#   cam_2  0.40 bull | 0.45 wires(2) | 0.55 wires(17) | 0.60, 0.65, 0.70 calibrate
#   cam_3  0.45 wires(2) | 0.50 wires(3) | 0.55 wires(15)
#
# So the doubles stage is not reachable by dimming this footage any more: it goes from
# bull at 0.42 straight to wires at 0.45 on every camera tried. A fixture for the doubles
# stage will have to be made some other way, and that is an issue rather than a line here.
#
#   dim_1  at 0.50, ONE camera, for the DOUBLES stage, which is what #1321 is about.
#          One camera because the table above is what a rig really does: at 0.50 camera 1
#          and camera 2 are refused at the doubles stage and camera 3 traces its ring and
#          falls out at the wire stage, and there is no scale at which all three are
#          refused at the same one. Pinning three cameras to one scale would be two
#          assertions about the branch this issue is for and one about wherever the third
#          camera's heuristic happens to land. The census that wants three cameras --
#          #1321's rule that one refused camera is one ERROR and no more -- is made on
#          the dark run below, where all three are refused for the same reason and there
#          is no cliff anywhere near them.
#
#   dark_* at 0.18, all three cameras, for the BULL stage. #1320's phases1320/1320-speck.sh
#          asserts the same ERROR and the same fault, but on a board whose candidates were
#          all refused -- a different sentence from this one, which is a frame with no
#          contours in it at all. It is kept for that, and for a second reason worth more:
#          it is the neighbouring sentence. Every assertion below names the stage it is
#          about, and this run is what proves that naming bites -- put the dark clips into
#          the doubles run and the doubles assertions go red instead of passing on a bull
#          refusal, which is exactly what they did until this issue.
#
# The white-pixel branch of the doubles stage -- "the doubles mask holds N white pixels
# and this stage needs at least 1000" -- is deliberately NOT asserted here any more.
# Nothing dimmed reaches it: a doubles mask that empty comes from a red/green frame the
# bull stage has already refused as unmeasurable, and the table above is how that was
# found. It is still the sentence #1321 wrote; it is now reachable only from a mask that
# is not merely dark, and no fixture in this repository makes one.
#
# Since 74be46f a camera without a fitted doubles ring fails the whole calibration, so
# neither failing run reaches the scoring loop: each goes to #895's fault vigil and stays
# there. They are therefore run in the background and ended by their own recorded pid,
# never by pattern, and BOARD FAULTED is read as well as the calibration line.

echo "--- build the dimmed footage from the mocks ---"
g++ -std=c++17 -O1 -o /run1321/i1321_dark_footage /app/testers/i1321_dark_footage.cpp \
  $(pkg-config --cflags --libs opencv4) || exit 1
/run1321/i1321_dark_footage /app/mocks/cam_1.mp4 /run1321/dim_1.avi 0.50 300 || exit 1
for i in 1 2 3; do
  /run1321/i1321_dark_footage /app/mocks/cam_$i.mp4 /run1321/dark_$i.avi 0.18 300 || exit 1
done

# One failing run: start it, give it the time a calibration takes, end it by its pid.
faulting_run() { # <name> <cams>
  /app/build/opendartboard --cams "$2" --width 1280 --height 720 \
    > /run1321/$1.out 2> /run1321/$1.err &
  local pid=$!
  sleep 25
  kill -TERM $pid 2>/dev/null
  wait $pid 2>/dev/null
  echo "${1}_RC=$?"
}

echo "--- the doubles-stage failure, WITHOUT --debug ---"
faulting_run dim /run1321/dim_1.avi

echo "--- the bull-stage refusal, the same footage dimmed further, WITHOUT --debug ---"
faulting_run dark /run1321/dark_1.avi,/run1321/dark_2.avi,/run1321/dark_3.avi

echo "--- the control: the same footage the detector is known to calibrate on ---"
OD_MAX_CYCLES=20 /app/build/opendartboard \
  --cams /app/mocks/cam_1.mp4,/app/mocks/cam_2.mp4,/app/mocks/cam_3.mp4 \
  --width 1280 --height 720 > /run1321/control.out 2> /run1321/control.err
echo "CONTROL_RC=$?"

# Colour codes are in every console line; strip them once and read the plain text.
for f in dim dark control; do
  sed 's/\x1b\[[0-9;]*m//g' /run1321/$f.out > /run1321/$f.txt
done

FAILED=0
say() { echo "$1"; [ "$2" = ok ] || FAILED=1; }

# Which stage refused a camera, read out of the ERROR that camera printed. This is the
# whole of #1324: an assertion that does not say which stage it is about is an assertion
# a neighbouring stage can satisfy, and that is how this tester stayed green for a branch
# it had stopped reaching.
stage_of() { # <camera> <file> -> board | bull | doubles | wires | none | other
  local line
  line=$(grep -E "^\[ERROR\]\[GEOMETRY_CALIBRATION\] - Camera $1 did not calibrate: " "$2" | head -1)
  case "$line" in
    "")                                          echo none ;;
    # #1331/ADR-0079: a stage above the bull, and it is new rather than renamed. The board
    # is now measured on the FULL frame before any region is drawn, with the same function
    # the bull stage calls, so an input with no measurable board in it is refused here and
    # never reaches the bull. The sentence it is refused with is bull_processing's own --
    # see the contour-count assertion in section 2, which still reads it.
    *"there is no board in this frame to build a region around"*) echo board ;;
    *"the bull could not be found"*)             echo bull ;;
    *"the doubles ring could not be fitted"*)    echo doubles ;;
    *"the wire stage found"*)                    echo wires ;;
    *)                                           echo other ;;
  esac
}

refused_at() { # <file> <stage> <what this run is> <camera> ...
  local file="$1" stage="$2" what="$3"; shift 3
  local cam got n
  for cam in "$@"; do
    got=$(stage_of "$cam" "$file")
    if [ "$got" = "$stage" ]; then
      say "OK   $what: camera $cam was refused at the $stage stage" ok
    else
      say "FAIL $what: camera $cam was refused at the $got stage, and this run is about the $stage stage" no
    fi
  done
  # #1321's rule: one refused camera is one ERROR, and no camera is told about twice.
  n=$(grep -cE '^\[ERROR\]\[GEOMETRY_CALIBRATION\] - Camera [0-9]+ did not calibrate: .*[0-9]' "$file" || true)
  if [ "$n" = "$#" ]; then say "OK   $what: one ERROR naming camera, stage and a count, per camera ($#)" ok
  else say "FAIL $what: expected $# naming ERROR lines, got $n" no; fi
}

echo "=== 1. the dimmed run is refused at the DOUBLES stage, without --debug ==="
grep -E '^\[ERROR\]\[GEOMETRY_CALIBRATION\] - Camera' /run1321/dim.txt || true
refused_at /run1321/dim.txt doubles "dim 0.50" 1

echo "=== 1b. the doubles sentence says what it counted, and the count is below its own threshold ==="
# A line nothing can falsify is not evidence. The count is printed against the threshold
# in the same sentence, so the sentence contradicts itself if the number comes from
# anywhere but the measurement that refused. Measured by printing the number of rays cast
# instead of the number that gave a point, where this reads "120 rays traced, 120 gave a
# boundary point, and at least 50 are needed" and the check below goes red.
#
# It is also asserted PRESENT, not merely consistent. Until #1324 this read a `grep -c`
# of a pattern that matched nothing on this input at all, and zero inconsistent counts
# out of zero counts was the green tick that hid the whole of this issue.
SAID=$(grep -cE '[0-9]+ rays traced, [0-9]+ gave a boundary point, and at least [0-9]+ are needed' /run1321/dim.txt || true)
WRONG=$(grep -ohE '[0-9]+ rays traced, [0-9]+ gave a boundary point, and at least [0-9]+ are needed' /run1321/dim.txt \
  | awk '$4 >= $12 { print }' | wc -l)
if [ "$SAID" -ge 1 ] && [ "$WRONG" = "0" ]; then
  say "OK   $SAID doubles-stage ray counts, every one below the threshold printed beside it" ok
else say "FAIL $SAID ray-count sentences on a run about the doubles stage, $WRONG of them not below their own threshold" no; fi
# And the mask it traced into is quoted, because "the rays found nothing" and "there was
# nothing to find" are different faults and the reader cannot tell them apart otherwise.
if grep -qE '\(doubles mask [0-9]+ white pixels\)' /run1321/dim.txt; then
  say "OK   the sentence also says how full the mask it traced was" ok
else say "FAIL the ray count is printed without the mask it was traced on" no; fi

echo "=== 2. the darker run is refused at the BOARD stage, and says what it counted ==="
# It was the BULL stage until #1331 and the measurement did not change -- it moved. The
# red/green frame at 0.18 yields 0 contours, which is the same sentence and the same
# threshold it was refused on before; what is different is that the board is measured on
# the full frame at STEP 1 now, so the refusal happens there instead of three stages down.
# #1324's rule is obeyed rather than worked around: the stage this run is about is named,
# and the day something moves it again this line goes red rather than passing next door.
refused_at /run1321/dark.txt board "dark 0.18" 1 2 3
SAID=$(grep -cE 'yields [0-9]+ contours, and at least [0-9]+ region' /run1321/dark.txt || true)
WRONG=$(grep -ohE 'yields ([0-9]+) contours, and at least ([0-9]+) region' /run1321/dark.txt \
  | awk '$2 >= $7 { print }' | wc -l)
if [ "$SAID" -ge 1 ] && [ "$WRONG" = "0" ]; then
  say "OK   $SAID bull-stage contour counts, every one below the threshold printed beside it" ok
else say "FAIL $SAID contour-count sentences on a run about the bull stage, $WRONG of them not below their own threshold" no; fi

echo "=== 3. the three echoes are not ERROR, in either failing run ==="
for pattern in \
  'ERROR.*Invalid input data' \
  'ERROR.*Insufficient calibration data for intersection calculation' \
  'ERROR.*Failed to compute ring-wire intersections'
do
  for run in dim dark; do
    N=$(grep -cE "$pattern" /run1321/$run.txt || true)
    if [ "$N" = "0" ]; then say "OK   $run: no ERROR matching /$pattern/" ok
    else say "FAIL $run: $N ERROR lines matching /$pattern/" no; fi
  done
done

echo "=== 4. BOARD FAULTED names the camera AND the stage it fell off at ==="
grep -E 'BOARD FAULTED' /run1321/dim.txt /run1321/dark.txt | head -2 || true
if grep -qE 'BOARD FAULTED: camera [0-9]+ did not calibrate: the doubles ring could not be fitted' /run1321/dim.txt; then
  say "OK   the vigil says which camera, and that it was the doubles ring" ok
else say "FAIL BOARD FAULTED does not name the doubles stage on a run refused there" no; fi
if grep -qE 'BOARD FAULTED: camera [0-9]+ did not calibrate: there is no board in this frame' /run1321/dark.txt; then
  say "OK   the vigil says which camera, and that there was no board to measure" ok
else say "FAIL BOARD FAULTED does not name the board stage on a run refused there" no; fi
for run in dim dark; do
  if grep -qE 'BOARD FAULTED.*cameras did not come up, or' /run1321/$run.txt; then
    say "FAIL $run: BOARD FAULTED still offers both halves and commits to neither" no
  else say "OK   $run: it does not offer two alternatives" ok; fi
done

echo "=== 5. neither failing run claims a calibration ==="
for run in dim dark; do
  if grep -q 'Initial calibration completed successfully' /run1321/$run.txt; then
    say "FAIL $run: a calibration was claimed from a board that was refused" no
  else say "OK   $run: no calibration was claimed" ok; fi
done

echo "=== 6. the control calibrates and says nothing new ==="
if grep -q 'Initial calibration completed successfully' /run1321/control.txt; then
  say "OK   the mocks calibrate" ok
else say "FAIL the mocks did not calibrate" no; fi
grep -E '^\[(ERROR|WARN)\]' /run1321/control.txt || true
NOISE=$(grep -cE '^\[(ERROR|WARN)\]' /run1321/control.txt || true)
if [ "$NOISE" = "0" ]; then say "OK   the control prints no ERROR and no WARN" ok
else say "FAIL the control prints $NOISE ERROR/WARN lines" no; fi

echo "CHECK_RC=$FAILED"
exit $FAILED
