set -u
# #1321: the failure and the control, in one run, with no --debug on either.
# #1324: two failures, at two named stages, because one of them stopped being the one
#        this tester was written about and the tester stayed green.
# #1362: the doubles stage gets a fixture that is about the doubles stage, because
#        dimming stopped reaching it and a stage nothing reaches is a stage nothing
#        guards.
#
# The failing inputs are made here rather than committed: clips built out of the mock
# footage by the two builders beside this file, a few hundred kB of build output apiece,
# giving the same failure on any checkout from the same source and the same arguments.
#
# WHY THERE ARE TWO BUILDERS, WHICH IS THE WHOLE OF #1362.
#
# #1321 made its input by DIMMING the mocks and #1324 measured which stage each scale
# reached. That was correct when it was measured. ADR-0079 then turned the first two
# stages around -- the board is found on the full frame and the region is drawn around
# what was found -- and a dim camera now gets FURTHER than it did: it traces its doubles
# ring and falls out at the WIRE stage. #1331's agent swept the ladder again on the new
# ordering, one camera per run, 120 frames, no --debug:
#
#   cam_1  0.40 bull | 0.42 bull | 0.45 wires(2) | 0.47 wires(2) | 0.50 wires(5)
#          0.52 wires(4) | 0.55 wires(13) | 0.60, 0.65, 0.70 calibrate
#   cam_2  0.40 bull | 0.45 wires(2) | 0.55 wires(17) | 0.60, 0.65, 0.70 calibrate
#   cam_3  0.45 wires(2) | 0.50 wires(3) | 0.55 wires(15)
#
# It goes from bull at 0.42 straight to wires at 0.45, on every camera tried. There is no
# brightness at which this footage is refused at the doubles stage, and that is not a gap
# in the sweep: dimming takes light from the bull, the triples and the doubles together,
# so a frame dark enough to break the doubles ring has already lost the bull -- and the
# bull stage is ABOVE the doubles stage, so it refuses first. #1331 left this section
# asserting `doubles` and left it RED rather than re-point it at the wire stage, which
# would have satisfied the tester and deleted the coverage. This is the repair.
#
# What fails AT the doubles stage is the other fault: a board whose ring is broken or
# covered over part of its circumference while the middle of the board is lit and whole.
# That is the fault the stage exists for -- #1340 measured a real rig whose ring stops
# closing wherever the light falls off it -- so i1362_broken_ring_footage greys over a
# sector of one camera's frame, at full exposure everywhere else.
#
# WHICH STAGE EACH INPUT REACHES, MEASURED (2026-09-19, dev build with the defines
# run_all.sh uses, one camera per run, no --debug):
#
#   the broken ring, cam_1, a 45-430 px annulus around the board at (612,338), one
#   sector from 0 deg, swept:
#
#     180 deg   DOUBLES   120 rays traced, 44 gave a boundary point (mask 15946 px)
#     200 deg   DOUBLES   ... 38 ...                                (mask 14301 px)
#     220 deg   DOUBLES   ... 31 ...                                (mask 13183 px)
#     240 deg   DOUBLES   ... 26 ...                                (mask 11276 px)
#
#   That is a plateau rather than a window, and it is why this fixture is built out of
#   geometry instead of out of exposure: the dimming ladder #1324 measured was neither
#   monotonic nor more than one step wide, and this is monotonic in the sweep and 60 deg
#   across before it stops being about this stage. 220 is taken, 19 rays clear of the
#   threshold that refuses.
#
#   the same footage dimmed, for the BULL/BOARD stage, all three cameras:
#
#     0.18      BOARD     the red/green frame yields 0 contours, at least 1 is needed
#
# ONE CAMERA ON THE BROKEN-RING RUN, and the reason survives #1362 unchanged even though
# the input is now geometry rather than exposure. The sector is drawn around each board's
# own centre, and the rays are cast from each board's own BULL, which on these three
# cameras sits 55, 66 and 100 px off that centre; so one sweep angle does not put three
# cameras in one stage. Measured, same build, same 45-430 px annulus:
#
#     cam_2  220 deg wires(17) | 260 deg bull | 280 deg bull
#     cam_3  220 deg wires(16) | 260 deg bull | 280 deg bull
#
# Pinning three cameras to one sweep would be one assertion about the branch this issue
# is for and two about wherever the other cameras' heuristics happen to land. The census
# that wants three cameras -- #1321's rule that one refused camera is one ERROR and no
# more -- is made on the dark run below, where all three are refused for the same reason
# and there is no cliff anywhere near them.
#
# THE DARK CLIPS STAY, at 0.18, all three cameras, for the BULL/BOARD stage. #1320's
# phases1320/1320-speck.sh asserts the same ERROR and the same fault, but on a board
# whose candidates were all refused -- a different sentence from this one, which is a
# frame with no contours in it at all. They are kept for that, and for a second reason
# worth more: they are the neighbouring sentence. Every assertion below names the stage
# it is about, and this run is what proves that naming bites -- put the dark clips into
# the broken-ring run and the doubles assertions go red instead of passing on a bull
# refusal, which is exactly what they did until #1324.
#
# The white-pixel branch of the doubles stage -- "the doubles mask holds N white pixels
# and this stage needs at least 1000" -- is still not asserted as a failure here, and
# #1362 turns it round into a guard instead. Nothing here reaches it: the broken ring
# leaves 13183 white pixels in the mask it traces, thirteen times the floor, which is the
# point -- there WAS a ring there and the rays did not find enough of it. A mask emptier
# than 1000 px comes from a red/green frame the stage above has already refused as
# unmeasurable, which is what the dark run measures.
#
# Since 74be46f a camera without a fitted doubles ring fails the whole calibration, so
# neither failing run reaches the scoring loop: each goes to #895's fault vigil and stays
# there. They are therefore run in the background and ended by their own recorded pid,
# never by pattern, and BOARD FAULTED is read as well as the calibration line.

echo "--- build the failing footage from the mocks ---"
CVFLAGS="$(pkg-config --cflags --libs opencv4)"
g++ -std=c++17 -O1 -o /run1321/i1362_broken_ring_footage \
  /app/testers/i1362_broken_ring_footage.cpp $CVFLAGS || exit 1
g++ -std=c++17 -O1 -o /run1321/i1321_dark_footage /app/testers/i1321_dark_footage.cpp \
  $CVFLAGS || exit 1

# The board camera 1 is looking at, measured on this tree with --debug: radius 291 px,
# centre (612,338). The annulus starts at 45 px, which is outside the bull and its ring,
# so the stage above still finds the bull -- at (616,283), where the untouched clip finds
# it -- and the stage this run is about is the one that fails.
/run1321/i1362_broken_ring_footage /app/mocks/cam_1.mp4 /run1321/broken_1.avi 300 \
  612 338 45 430 0,220 || exit 1
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
faulting_run broken /run1321/broken_1.avi

echo "--- the bull-stage refusal, the same footage dimmed instead, WITHOUT --debug ---"
faulting_run dark /run1321/dark_1.avi,/run1321/dark_2.avi,/run1321/dark_3.avi

echo "--- the control: the same footage the detector is known to calibrate on ---"
OD_MAX_CYCLES=20 /app/build/opendartboard \
  --cams /app/mocks/cam_1.mp4,/app/mocks/cam_2.mp4,/app/mocks/cam_3.mp4 \
  --width 1280 --height 720 > /run1321/control.out 2> /run1321/control.err
echo "CONTROL_RC=$?"

# Colour codes are in every console line; strip them once and read the plain text.
for f in broken dark control; do
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

echo "=== 1. the broken-ring run is refused at the DOUBLES stage, without --debug ==="
grep -E '^\[ERROR\]\[GEOMETRY_CALIBRATION\] - Camera' /run1321/broken.txt || true
refused_at /run1321/broken.txt doubles "broken ring 220 deg" 1

echo "=== 1a. the bull above it was found, so this really is the doubles stage's failure ==="
# #1362: this fixture is only about the doubles stage while the stage above it passes. A
# sector wide enough to swallow the bull refuses this camera one stage up -- measured, on
# this input: at 260 deg cameras 2 and 3 are refused at the bull, not at the ring. Section
# 1 would say so, and this says the other half out loud: the centre the rays were cast
# from was found, so what fell short is the tracing and not the thing above it.
if grep -qE '^\[INFO\]\[GEOMETRY_CALIBRATION\] - Camera 1 bull at \([0-9]+,[0-9]+\), chosen on ' /run1321/broken.txt; then
  say "OK   the bull was found on the broken-ring clip, and the ring is what failed" ok
else say "FAIL no bull was found on the broken-ring clip, so this run is not about the doubles stage" no; fi

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
SAID=$(grep -cE '[0-9]+ rays traced, [0-9]+ gave a boundary point, and at least [0-9]+ are needed' /run1321/broken.txt || true)
WRONG=$(grep -ohE '[0-9]+ rays traced, [0-9]+ gave a boundary point, and at least [0-9]+ are needed' /run1321/broken.txt \
  | awk '$4 >= $12 { print }' | wc -l)
if [ "$SAID" -ge 1 ] && [ "$WRONG" = "0" ]; then
  say "OK   $SAID doubles-stage ray counts, every one below the threshold printed beside it" ok
else say "FAIL $SAID ray-count sentences on a run about the doubles stage, $WRONG of them not below their own threshold" no; fi
# And the mask it traced into is quoted, because "the rays found nothing" and "there was
# nothing to find" are different faults and the reader cannot tell them apart otherwise.
if grep -qE '\(doubles mask [0-9]+ white pixels\)' /run1321/broken.txt; then
  say "OK   the sentence also says how full the mask it traced was" ok
else say "FAIL the ray count is printed without the mask it was traced on" no; fi
# On this input it is the first of the two, and #1362 asserts which rather than leaving a
# reader to admire the number: the mask holds 13183 white pixels against a floor of 1000,
# so there was a ring there and the rays did not find enough of it. A fixture that slid
# onto the OTHER branch of this stage -- an empty mask -- would go on printing a ray count
# nobody had looked at, under a heading that says the rays fell short.
THIN=$(grep -ohE '\(doubles mask [0-9]+ white pixels\)' /run1321/broken.txt \
  | awk '$3 < 1000 { print }' | wc -l)
if [ "$THIN" = "0" ]; then
  say "OK   the mask it traced is over the 1000-pixel floor, so the rays are what fell short" ok
else say "FAIL $THIN of the masks traced hold under 1000 white pixels, which is this stage's other branch" no; fi

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
  for run in broken dark; do
    N=$(grep -cE "$pattern" /run1321/$run.txt || true)
    if [ "$N" = "0" ]; then say "OK   $run: no ERROR matching /$pattern/" ok
    else say "FAIL $run: $N ERROR lines matching /$pattern/" no; fi
  done
done

echo "=== 4. BOARD FAULTED names the camera AND the stage it fell off at ==="
grep -E 'BOARD FAULTED' /run1321/broken.txt /run1321/dark.txt | head -2 || true
if grep -qE 'BOARD FAULTED: camera [0-9]+ did not calibrate: the doubles ring could not be fitted' /run1321/broken.txt; then
  say "OK   the vigil says which camera, and that it was the doubles ring" ok
else say "FAIL BOARD FAULTED does not name the doubles stage on a run refused there" no; fi
if grep -qE 'BOARD FAULTED: camera [0-9]+ did not calibrate: there is no board in this frame' /run1321/dark.txt; then
  say "OK   the vigil says which camera, and that there was no board to measure" ok
else say "FAIL BOARD FAULTED does not name the board stage on a run refused there" no; fi
for run in broken dark; do
  if grep -qE 'BOARD FAULTED.*cameras did not come up, or' /run1321/$run.txt; then
    say "FAIL $run: BOARD FAULTED still offers both halves and commits to neither" no
  else say "OK   $run: it does not offer two alternatives" ok; fi
done

echo "=== 5. neither failing run claims a calibration ==="
for run in broken dark; do
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
