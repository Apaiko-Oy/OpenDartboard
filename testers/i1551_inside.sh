set -u
# #1551, inside the container: camera admission at the coherence gate is DETERMINISTIC on
# recorded input, proven by running it, and the flip the issue was filed on is reproduced
# on demand and named.
#
# THE FINDING THIS ENFORCES. The "nondeterminism" #1551 measured -- rig-20260922 camera 1
# admitted at R=0.873343 in the cached baseline census, refused at R=0.577558 in every
# registry census -- was never run-to-run variance. It was TWO BINARIES: the registry
# builds with -DDEBUG_SEEK_VIDEO, whose seek starts a file source 3.0/2.8/2.633 s in, so
# the thirty frames the Scorer averages for calibration are a different picture from the
# release build's frames 0-29. Within either window every recorded run on disk is
# bit-identical -- 9 dev-build runs of rig-20260918 and 4 of rig-20260922 across four
# separate days and branches agree on every candidate count, tilt and R to the last
# printed digit, and the 2 release-family baseline runs agree with each other the same
# way. The CAPANCHOR lines carry the proof: first_pos_ms=3000/2800/2633 in every refused
# run (exactly 3 - 0.18*i seconds at 30 fps, the DEBUG_SEEK_VIDEO constant), 0 in the
# admitted one. Thread scheduling, iteration order, uninitialised state and wall-clock
# sampling are all refuted by the same observation: none of them can produce the same
# 6-decimal R on a contended box thirteen times.
#
# WHAT IS ASSERTED, and why each half is here:
#
#   1. DETERMINISM, 5 consecutive runs per fixture, both fixtures: the calibration
#      transcript (CAPANCHOR window, every GEOMETRY_CALIBRATION / WIRE_PROCESSING /
#      ELLIPSE / MASK / ORIENTATION line, the further looks included), normalised only by
#      stripping the host-clock fields, is byte-identical across all five -- so every
#      camera lands on the same side of the gate with the same printed margin every run.
#      A future nondeterminism source (a cv:: parallel path, an unordered container in
#      candidate scoring) turns this red by name.
#
#   2. THE MARGIN IS SAID every run: every wire-model line carries "(margin ", the
#      near-threshold visibility #1510 Phase 2 added and #1551's acceptance keeps.
#
#   3. THE FLIP, REPRODUCED, prediction stated first: the SAME dev binary with
#      OD_SEEK_VIDEO=off calibrates at the clip's opening (CAPANCHOR first_pos_ms=0
#      predicted) and rig-20260922 camera 1 is then ADMITTED (predicted, the other side
#      of #1551's gate) where phase 1 just refused it five times -- the "nondeterminism"
#      produced on demand by moving the calibration window and changing nothing else.
#      This is the mutation proof: the cause is the window, and only the window.
#
# The script ends on `exit`, never on an `echo`: #1463, #1479.

BIN=/app/build/opendartboard
RUN=/run1551
FAIL=0
note() { if [ "$1" -eq 0 ]; then echo "ok   $2"; else echo "FAIL $2"; FAIL=$((FAIL + 1)); fi; }

if [ ! -x $BIN ]; then
  echo "FAIL no $BIN -- a fresh worktree has none. Build it: make build (or testers/run_all.sh)."
  exit 1
fi
NEWEST=$(find /app/src /app/CMakeLists.txt -newer $BIN 2>/dev/null | head -5)
if [ -n "$NEWEST" ]; then
  echo "FAIL $BIN is OLDER than the source it is supposed to be measuring:"
  echo "$NEWEST"
  exit 1
fi

# The admission transcript of one run: the calibration-path lines, with the two
# host-clock fields stripped (they are the run's wall clock and nothing else varies).
transcript() {
  sed 's/\x1b\[[0-9;]*m//g' "$1" |
    grep -E '\[(GEOMETRY_CALIBRATION|WIRE_PROCESSING|ELLIPSE_PROCESSING|MASK_PROCESSING|ORIENTATION_PROCESSING|GEOMETRYDETECTOR)\]|CAPANCHOR|DEBUG_SEEK_VIDEO' |
    sed -E 's/ host_return_us=[0-9]+ anchor_us=[0-9]+//'
}

calibrate_once() { # $1 fixture dir, $2 out file, $3.. extra env
  local dir="$1" out="$2"; shift 2
  rm -rf $RUN/cache $RUN/debug_frames
  ( cd $RUN && env OD_MAX_CYCLES=1 "$@" timeout 300 $BIN \
      --cams "$dir/cam_1.mp4,$dir/cam_2.mp4,$dir/cam_3.mp4" \
      --width 1280 --height 720 > "$out" 2>&1 )
  return $?
}

for FIX in rig-20260918 rig-20260922; do
  DIR=/app/mocks/$FIX
  echo
  echo "---- 1. $FIX: five consecutive runs of one binary ----"
  for n in 1 2 3 4 5; do
    calibrate_once "$DIR" "$RUN/$FIX-run$n.out"; RC=$?
    transcript "$RUN/$FIX-run$n.out" > "$RUN/$FIX-run$n.cal"
    ADMITTED=$(grep -c "of the twenty boundaries were placed by a candidate" "$RUN/$FIX-run$n.cal")
    REFUSED=$(grep -c "did not calibrate" "$RUN/$FIX-run$n.cal")
    WINDOW=$(grep -o "first_pos_ms=[0-9]*" "$RUN/$FIX-run$n.cal" | tr '\n' ' ')
    echo "     run $n rc=$RC window: $WINDOW trusted-fits=$ADMITTED refusal-lines=$REFUSED"
    [ "$RC" = 0 ]; note $? "  run $n ended cleanly"
    [ -s "$RUN/$FIX-run$n.cal" ]; note $? "  run $n produced a calibration transcript"
  done
  IDENTICAL=0
  for n in 2 3 4 5; do
    if ! diff -q "$RUN/$FIX-run1.cal" "$RUN/$FIX-run$n.cal" > /dev/null; then
      IDENTICAL=1
      echo "     run 1 and run $n disagree:"
      diff "$RUN/$FIX-run1.cal" "$RUN/$FIX-run$n.cal" | head -10
    fi
  done
  [ "$IDENTICAL" = 0 ]; note $? "$FIX: all five admission transcripts are byte-identical -- every camera lands on the same side of the gate, at the same R, every run"

  # The dev define is a precondition, not an assumption: phase 3's flip only means
  # something on a binary that seeks. A release-style build fails here by name instead
  # of passing phase 3 vacuously (its first_pos_ms is already 0).
  grep -q "DEBUG_SEEK_VIDEO: video 1 seeked forward" "$RUN/$FIX-run1.cal"
  note $? "$FIX: the binary carries the dev seek and SAYS so at INFO -- the window this census belongs to is in its own log"

  WIRE=$(grep -c "wire model: [0-9]* candidates" "$RUN/$FIX-run1.cal")
  NOMARGIN=$(grep "wire model: [0-9]* candidates" "$RUN/$FIX-run1.cal" | grep -vc "(margin ")
  echo "     wire-model lines=$WIRE without-margin=$NOMARGIN"
  [ "$WIRE" -gt 0 ] && [ "$NOMARGIN" = 0 ]; note $? "$FIX: every wire-model line says its margin against the gate"
done

echo
echo "---- 3. the flip, reproduced on the same binary (prediction stated first) ----"
echo "     PREDICTED: OD_SEEK_VIDEO=off moves every CAPANCHOR to first_pos_ms=0 and"
echo "     rig-20260922 camera 1 -- refused five times above -- is ADMITTED at the"
echo "     clip's opening. That is #1551's flip, produced by the window alone."
DIR=/app/mocks/rig-20260922
calibrate_once "$DIR" "$RUN/rig-20260922-open.out" OD_SEEK_VIDEO=off; RC=$?
transcript "$RUN/rig-20260922-open.out" > "$RUN/rig-20260922-open.cal"
[ "$RC" = 0 ]; note $? "the opening-window run ended cleanly"

WINDOWS=$(grep -o "first_pos_ms=[0-9]*" "$RUN/rig-20260922-open.cal" | sort -u | tr '\n' ' ')
echo "     window: $WINDOWS"
[ "$WINDOWS" = "first_pos_ms=0 " ]; note $? "every camera calibrated at the clip's opening (first_pos_ms=0)"

grep -q "held at the clip's opening (OD_SEEK_VIDEO=off)" "$RUN/rig-20260922-open.cal" \
  || grep -q "held at the clip's opening" "$RUN/rig-20260922-open.out"
note $? "and the run SAYS so, so this log cannot be compared to a registry run unknowingly"

CAM1_DEV=$(grep -m1 "Camera 1 wire model: [0-9]* candidates" "$RUN/rig-20260922-run1.cal")
CAM1_OPEN=$(grep -m1 "Camera 1 wire model: [0-9]* candidates" "$RUN/rig-20260922-open.cal")
echo "     dev window:  ${CAM1_DEV#*- }"
echo "     opening:     ${CAM1_OPEN#*- }"
grep -m1 "Camera 1 wire model: [0-9]* candidates" "$RUN/rig-20260922-run1.cal" |
  grep -q "this fit is not trusted"; note $? "camera 1 at the dev window: refused (the registry side of the flip)"
grep -m1 "Camera 1 wire model: [0-9]* candidates" "$RUN/rig-20260922-open.cal" |
  grep -q "of the twenty boundaries were placed by a candidate"
note $? "camera 1 at the opening: ADMITTED -- the flip, reproduced by moving the window and nothing else"
grep -m1 "Camera 1 wire model" "$RUN/rig-20260922-open.cal" | grep -q "(margin "
note $? "and the admitted side says its margin too"

echo
echo "---- census: the two windows, side by side (committed beside minimumCoherence) ----"
for f in rig-20260918 rig-20260922; do
  echo "  $f, dev window (run 1 of 5, all identical):"
  grep "wire model: [0-9]* candidates" "$RUN/$f-run1.cal" | sed 's/^/    /'
done
echo "  rig-20260922, opening window:"
grep "wire model: [0-9]* candidates" "$RUN/rig-20260922-open.cal" | sed 's/^/    /'

echo
if [ "$FAIL" -gt 0 ]; then echo "RESULT: $FAIL failure(s)"; exit 1; fi
echo "RESULT: admission is deterministic on recorded input, and the flip is the window"
exit 0
