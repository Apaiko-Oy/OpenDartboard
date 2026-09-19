set -u
# #1388 / ADR-0080: a bump stops being fatal, a move stops being laundered, and the number
# that tells them apart is measured here rather than chosen.
#
# SIX PHASES, AND THE ORDER MATTERS. Phase 1 produces the budget. Phases 2 and 3 are the
# two arms it separates -- a knock and a move -- and they differ in exactly one thing, what
# is behind the camera paths when the second measurement is taken. Phase 4 is the half
# ADR-0080 section 4 adds, which is only observable in a SECOND start, so it must run after
# phase 3 and in the same directory. Phase 5 makes the code adopt and watches the guard
# fire, because a guard that cannot be made to fire is not evidence (#708). Phase 6 is
# #1321's control: the untouched mocks, which must still calibrate and say nothing.
#
# Every board is ended by its own recorded pid, never by pattern, and every board is
# bounded -- #895's vigil does not exit and OD_MAX_CYCLES does not bound it.

RUN=/run1388
FAILED=0
say() { echo "$1"; [ "$2" = ok ] || FAILED=1; }
plain() { sed 's/\x1b\[[0-9;]*m//g' "$1"; }

INC="-I/app/src -I/app/src/utils -I/app/src/detector/geometry/calibration -I/app/src/detector/geometry/detection"
CVFLAGS="$(pkg-config --cflags --libs opencv4)"

echo "=============== PHASE 1: the budget, measured ==============="
# ADR-0080 section 1 leaves one figure to the implementing slice and says what to measure
# it against: a dart struck against the frame during a run. The only observable that event
# has is the board's own witness measurement, so that is what is taken -- by the same two
# calls attemptRecovery makes, on the mean of the same thirty frames, once a second across
# both fixtures. What decides a budget is the LONGEST RUN OF CONSECUTIVE DISAGREEING
# SAMPLES: how long a board asking repeatedly would go on being told the rig had moved
# while nobody had touched it.
g++ -std=c++17 -O2 -o $RUN/disturbance /app/testers/i1388_disturbance.cpp \
  /app/src/detector/geometry/calibration/*.cpp $INC $CVFLAGS -lpthread || exit 1

WORST=0
WORST_CLIP=""
for CLIP in /app/mocks/cam_1.mp4 /app/mocks/cam_2.mp4 /app/mocks/cam_3.mp4 \
            /app/mocks/rig-20260918/cam_1.mp4 /app/mocks/rig-20260918/cam_2.mp4 \
            /app/mocks/rig-20260918/cam_3.mp4; do
  NAME=$(echo "$CLIP" | tr '/.' '__')
  # The camera index only names the slot in the log; the comparison is one clip against
  # itself, so any index is the same measurement. 0 keeps the lines readable.
  $RUN/disturbance "$CLIP" 0 60 30 58 2>/dev/null | plain /dev/stdin \
    | grep -E '^(CLIP|SAMPLE|SUMMARY)' > "$RUN/dist_$NAME.txt"
  LINE=$(grep SUMMARY "$RUN/dist_$NAME.txt")
  echo "$LINE"
  RUNLEN=$(echo "$LINE" | grep -oE 'longest_disagreeing_run=[0-9]+' | cut -d= -f2)
  RUNLEN=${RUNLEN:-0}
  if [ "$RUNLEN" -gt "$WORST" ]; then WORST=$RUNLEN; WORST_CLIP=$CLIP; fi
done
# Samples are one second apart: sample-every 30 frames on 30 fps footage.
echo "MEASURED longest disturbance on an untouched rig: ${WORST}.00 s, on $WORST_CLIP"

# The budget as the code spells it, read out of the code rather than retyped here, so this
# phase fails when somebody moves a constant without re-measuring.
ATTEMPTS=$(grep -oE 'constexpr int kMovedAttempts = [0-9]+' /app/src/scorer/scorer.cpp | grep -oE '[0-9]+$')
WAIT=$(grep -oE 'constexpr long kMovedWaitSeconds = [0-9]+' /app/src/scorer/scorer.cpp | grep -oE '[0-9]+$')
echo "BUDGET in the code: attempts=$ATTEMPTS wait=${WAIT}s"
if [ -n "$ATTEMPTS" ] && [ -n "$WAIT" ]; then say "OK   the budget is two named constants" ok
else say "FAIL could not read the budget out of scorer.cpp" no; fi

# A budget spans (attempts - 1) waits plus (attempts - 1) measurements. The measurement's
# own cost is measured in phase 3, where a real board takes four of them; here the wait
# alone already has to outlast the disturbance, which is the conservative half of the
# question and the half that needs no timing.
SPAN=$(( (ATTEMPTS - 1) * WAIT ))
echo "BUDGET waits alone span ${SPAN}s; the disturbance to outlast is ${WORST}.00 s"
if [ "$WORST" -lt "$ATTEMPTS" ]; then
  say "OK   $ATTEMPTS measurements outnumber the ${WORST} consecutive disagreeing samples measured" ok
else
  say "FAIL a budget of $ATTEMPTS measurements cannot outlast $WORST consecutive disagreeing samples" no
fi

echo "=============== the footage the two arms are built from ==============="
g++ -std=c++17 -O1 -o $RUN/moved /app/testers/i899_moved_footage.cpp $CVFLAGS || exit 1
for i in 1 2 3; do
  /app/build/opendartboard --version > /dev/null 2>&1
  $RUN/moved /app/mocks/cam_$i.mp4 $RUN/held_$i.avi   0  0 0 400 0   || exit 1
  $RUN/moved /app/mocks/cam_$i.mp4 $RUN/later_$i.avi  0  0 0 400 400 || exit 1
  $RUN/moved /app/mocks/cam_$i.mp4 $RUN/nudged_$i.avi 20 15 0 400 400 || exit 1
done

# $1 = arm name, $2 = the directory it runs in, $3 = footage swapped in after calibration,
# $4 = footage swapped in once the first disagreement is logged (empty: leave $3 there),
# $5 = seconds to let it run after the swap, $6 = the binary
arm() {
  NAME=$1; DIR=$2; SWAP=$3; SETTLE=$4; SECONDS_AFTER=$5; BIN=$6
  mkdir -p "$DIR"
  for i in 1 2 3; do cp $RUN/held_$i.avi $DIR/src_$i.avi; done

  ( cd "$DIR" && OD_MAX_CYCLES=100000 OD_BLIND_AFTER=200 OD_BLIND_FOR_MS=8000 "$BIN" \
      --cams $DIR/src_1.avi,$DIR/src_2.avi,$DIR/src_3.avi \
      --width 1280 --height 720 > $RUN/$NAME.out 2> $RUN/$NAME.err ) &
  BOARD=$!

  for _ in $(seq 1 90); do
    grep -q 'Initial calibration completed successfully' $RUN/$NAME.out && break
    grep -q 'BOARD HOLDING A GEOMETRY FAULT' $RUN/$NAME.out && break
    sleep 1
  done
  # The exchange is a rename, so the running process keeps reading through the handle it
  # already holds and only the reopen meets the new file.
  for i in 1 2 3; do cp $RUN/${SWAP}_$i.avi $DIR/swap_$i.avi; mv $DIR/swap_$i.avi $DIR/src_$i.avi; done
  echo "$NAME SWAPPED=$SWAP"

  if [ -n "$SETTLE" ]; then
    # The knock. Put the rig back the moment the board has said, once, that it disagrees
    # -- which is what a knock IS: displaced during one measurement and back before the
    # next. Event-driven rather than timed, because the wait between measurements is two
    # seconds and a sleep that guessed would be measuring the guess.
    for _ in $(seq 1 400); do
      grep -q 'BOARD GEOMETRY: measurement 1 of' $RUN/$NAME.out && break
      sleep 0.2
    done
    for i in 1 2 3; do cp $RUN/${SETTLE}_$i.avi $DIR/swap_$i.avi; mv $DIR/swap_$i.avi $DIR/src_$i.avi; done
    echo "$NAME SETTLED_TO=$SETTLE"
  fi

  sleep "$SECONDS_AFTER"
  kill -TERM $BOARD 2>/dev/null
  wait $BOARD 2>/dev/null
  echo "${NAME}_RC=$?"
  plain $RUN/$NAME.out > $RUN/$NAME.txt
}

echo "=============== PHASE 2: a knock. It disagrees, then it settles ==============="
arm knock $RUN/knock nudged later 30 /app/build/opendartboard
grep -E 'BOARD SIGHT LOST|BOARD GEOMETRY|BOARD SETTLED|BOARD RECOVERED|BOARD MOVED|GEOMETRY FAULT' $RUN/knock.txt | head -20

echo "=== 2a. it said it disagreed, and named the measurement out of the budget ==="
if grep -qE 'BOARD GEOMETRY: measurement [0-9]+ of [0-9]+ disagrees' $RUN/knock.txt; then
  say "OK   a disagreement is counted against a budget and said out loud" ok
else say "FAIL nothing recorded a disagreement" no; fi

echo "=== 2b. the disagreement did NOT end the run ==="
if grep -q 'BOARD MOVED' $RUN/knock.txt; then
  say "FAIL a knock faulted the board, which is the defect this issue is about" no
else say "OK   a knock did not fault the board" ok; fi

echo "=== 2c. it settled, and said what it resumed on ==="
if grep -q 'BOARD SETTLED' $RUN/knock.txt; then say "OK   the board says the disagreement went away" ok
else say "FAIL nothing says the board recovered from a disagreement" no; fi
if grep -q 'Scoring resumes on the calibration this board started with' $RUN/knock.txt; then
  say "OK   it resumed on the HELD calibration, never the fresh one (ADR-0080 section 2)" ok
else say "FAIL nothing says which calibration scoring resumed on" no; fi

echo "=== 2d. nothing was adopted: the seal the board scores against never moved ==="
if grep -q 'GEOMETRY SEALED' $RUN/knock.txt; then say "OK   the geometry was sealed at calibration" ok
else say "FAIL nothing sealed the geometry, so nothing could have noticed an adoption" no; fi
if grep -q 'BOARD ADOPTED GEOMETRY MID-RUN' $RUN/knock.txt; then
  say "FAIL the board replaced its geometry while running" no
else say "OK   the board scored the whole run on the geometry it sealed" ok; fi

echo "=== 2e. a knock leaves no fault behind for the next start to find ==="
if [ -f $RUN/knock/cache/geometry_moved.txt ]; then
  say "FAIL a knock recorded a geometry fault: $(cat $RUN/knock/cache/geometry_moved.txt)" no
else say "OK   no geometry fault was recorded" ok; fi

echo "=============== PHASE 3: a move. It disagrees every time, and faults ==============="
arm move $RUN/move nudged "" 40 /app/build/opendartboard
grep -E 'BOARD SIGHT LOST|BOARD GEOMETRY|BOARD SETTLED|BOARD RECOVERED|BOARD MOVED|GEOMETRY FAULT|BOARD FAULTED' $RUN/move.txt | head -20

echo "=== 3a. it spent the whole budget before deciding ==="
SPENT=$(grep -cE 'BOARD GEOMETRY DISAGREES' $RUN/move.txt || true)
echo "measurements that disagreed: $SPENT of a budget of $ATTEMPTS"
if [ "$SPENT" -ge "$ATTEMPTS" ]; then say "OK   the board measured $SPENT times before faulting" ok
else say "FAIL the board faulted after $SPENT measurement(s) of a budget of $ATTEMPTS" no; fi

echo "=== 3b. what one measurement really costs, which is the other half of the span ==="
# The seconds-blind figure each attempt prints is the one unit a reader can check the
# spacing in. The gap between consecutive disagreeing attempts is the wait plus the
# measurement, measured on a running board rather than asserted.
grep -oE 'attempt [0-9]+ after [0-9]+ seconds blind' $RUN/move.txt | tail -5
FIRST_MOVED=$(grep -oE 'attempt [0-9]+ after [0-9]+ seconds blind' $RUN/move.txt | awk '{print $4}' | head -1)
LAST_MOVED=$(grep -oE 'attempt [0-9]+ after [0-9]+ seconds blind' $RUN/move.txt | awk '{print $4}' | tail -1)
echo "MEASURED the run spent from ${FIRST_MOVED}s to ${LAST_MOVED}s blind reaching its verdict"

echo "=== 3c. the refusal names the camera and how far it moved ==="
if grep -qE 'BOARD MOVED: camera [0-9]+: the bull moved [0-9.]+ px' $RUN/move.txt; then
  say "OK   the refusal names the camera and the distance" ok
else say "FAIL nothing named a moved camera" no; fi
CONSISTENT=$(grep -oE 'BOARD MOVED: camera [0-9]+: the bull moved [0-9.]+ px and at most [0-9.]+ px is still the same rig' $RUN/move.txt \
  | awk '$7 > $12 { print }' | wc -l)
if [ "$CONSISTENT" -ge 1 ]; then say "OK   the figure it refused on really is past the tolerance beside it" ok
else say "FAIL the refusal's own numbers do not justify it" no; fi
if grep -qE 'the disagreement persisted through all [0-9]+ measurements' $RUN/move.txt; then
  say "OK   it says the disagreement persisted, which is what makes it a move" ok
else say "FAIL nothing says why this was a move rather than a knock" no; fi

echo "=== 3d. it did not resume, and it took the vigil rather than going quiet ==="
if grep -q 'BOARD RECOVERED' $RUN/move.txt; then say "FAIL a moved board was waved back into scoring" no
else say "OK   a moved board never said it recovered" ok; fi
if grep -q 'BOARD FAULTED: the cameras came back and the board is not where it was' $RUN/move.txt; then
  say "OK   the vigil says which camera moved, in #1321's sentence" ok
else say "FAIL the faulted board does not say the geometry moved" no; fi

echo "=== 3e. and it is recorded where a restart can find it ==="
if [ -f $RUN/move/cache/geometry_moved.txt ]; then
  say "OK   the verdict outlived the process: $(head -1 $RUN/move/cache/geometry_moved.txt)" ok
else say "FAIL nothing was written for the next start to read" no; fi
if grep -qE 'camera [0-9]+: the bull moved [0-9.]+ px' $RUN/move/cache/geometry_moved.txt 2>/dev/null; then
  say "OK   the record names the camera and the magnitude, not just that something happened" ok
else say "FAIL the record does not name a camera and a distance" no; fi

echo "=== 3f. the two arms differ, on the same code and the same flags ==="
# The only difference between phases 2 and 3 is whether the nudged footage was taken away
# again. If they said the same thing neither would be measuring anything.
K_SET=$(grep -c 'BOARD SETTLED' $RUN/knock.txt || true)
K_MOV=$(grep -c 'BOARD MOVED' $RUN/knock.txt || true)
M_SET=$(grep -c 'BOARD SETTLED' $RUN/move.txt || true)
M_MOV=$(grep -c 'BOARD MOVED' $RUN/move.txt || true)
echo "knock: SETTLED=$K_SET MOVED=$K_MOV   move: SETTLED=$M_SET MOVED=$M_MOV"
if [ "$K_SET" -ge 1 ] && [ "$K_MOV" = "0" ] && [ "$M_SET" = "0" ] && [ "$M_MOV" -ge 1 ]; then
  say "OK   one arm settled and the other faulted" ok
else say "FAIL the two arms did not separate" no; fi

echo "=============== PHASE 4: the record survives the restart ==============="
# ADR-0080 section 4. Before #1388 this is the whole defect: Moved exited, Restart=always
# started the board again, #1330 means the cache is not read, and the board calibrated on
# the rig as it now is -- the adoption #899 refused, arriving through the unit file. So
# this phase starts a SECOND board in the directory phase 3 faulted in.
( cd $RUN/move && timeout 60 /app/build/opendartboard --cams $RUN/move/src_1.avi,$RUN/move/src_2.avi,$RUN/move/src_3.avi \
    --width 1280 --height 720 > $RUN/restart.out 2> $RUN/restart.err ) &
RESTART=$!
sleep 25
kill -TERM $RESTART 2>/dev/null
wait $RESTART 2>/dev/null
plain $RUN/restart.out > $RUN/restart.txt
grep -E 'BOARD HOLDING A GEOMETRY FAULT|Initial calibration|Calibrating camera|BOARD FAULTED' $RUN/restart.txt | head -10

echo "=== 4a. the restart read the record and said so ==="
if grep -q 'BOARD HOLDING A GEOMETRY FAULT' $RUN/restart.txt; then
  say "OK   the second start knows the first one found the rig somewhere else" ok
else say "FAIL the restart came up as though nothing had happened" no; fi

echo "=== 4b. and it calibrated on NOTHING, which is the adoption being refused ==="
if grep -q 'Initial calibration completed successfully' $RUN/restart.txt; then
  say "FAIL the restart calibrated on the rig as it now is -- ADR-0080 section 4 exactly" no
else say "OK   the restart took no fresh calibration" ok; fi
if grep -q 'Calibrating camera' $RUN/restart.txt; then
  say "FAIL the restart looked at the board before refusing" no
else say "OK   it did not even look: no camera was opened and nothing was measured" ok; fi

echo "=== 4c. it stays up and says which camera, rather than exiting into Restart=always ==="
if grep -qE 'BOARD FAULTED: camera [0-9]+: the bull moved [0-9.]+ px' $RUN/restart.txt; then
  say "OK   the vigil names the camera and the distance the first run measured" ok
else say "FAIL the refusing board does not say which camera moved" no; fi

echo "=== 4d. an operator clears it, and only an operator ==="
( cd $RUN/move && /app/build/opendartboard --clear-geometry-fault > $RUN/clear.out 2>&1 )
CLEAR_RC=$?
plain $RUN/clear.out
if [ "$CLEAR_RC" = "0" ] && [ ! -f $RUN/move/cache/geometry_moved.txt ]; then
  say "OK   --clear-geometry-fault removed the record" ok
else say "FAIL the record survived --clear-geometry-fault (rc=$CLEAR_RC)" no; fi
if grep -qE 'camera [0-9]+: the bull moved' $RUN/clear.out; then
  say "OK   it printed what it was clearing, so it cannot be done blind" ok
else say "FAIL clearing said nothing about what it cleared" no; fi

echo "=== 4e. and the board calibrates again afterwards ==="
for i in 1 2 3; do cp $RUN/held_$i.avi $RUN/move/src_$i.avi; done
( cd $RUN/move && OD_MAX_CYCLES=40 timeout 120 /app/build/opendartboard \
    --cams $RUN/move/src_1.avi,$RUN/move/src_2.avi,$RUN/move/src_3.avi \
    --width 1280 --height 720 > $RUN/after.out 2> $RUN/after.err )
plain $RUN/after.out > $RUN/after.txt
if grep -q 'Initial calibration completed successfully' $RUN/after.txt; then
  say "OK   a cleared board calibrates and scores again" ok
else say "FAIL the board did not come back after the fault was cleared" no; fi

echo "=============== PHASE 5: make it adopt, and watch the guard fire ==============="
# "Nothing adopts fresh geometry mid-run" is asserted here rather than left as a property
# of the control flow. The proof is #1330's and #708's: build a tree in which the recovery
# DOES write the fresh calibration over the held one, run the arm that recovers, and
# require the guard to say so. An absence nothing can make present is not a finding.
mkdir -p $RUN/mut
tar -C /app --exclude=./mocks --exclude=./.git --exclude=./build -cf - . | tar -C $RUN/mut -xf -
python3 - <<'PY'
path = "/run1388/mut/src/detector/geometry/geometry_detector.cpp"
src = open(path).read()
anchor = """        const geometry_agreement::Movement movement =
            geometry_agreement::measure(calibrations[i], fresh);"""
assert anchor in src, "the mutation's anchor is not in geometry_detector.cpp any more"
src = src.replace(anchor, anchor + """
        calibrations[i] = fresh; // #1388's mutation: the adoption ADR-0080 section 2 forbids""", 1)
open(path, "w").write(src)
print("MUTATED: reviewGeometry now writes the fresh calibration over the held one")
PY
cmake -S $RUN/mut -B $RUN/mut/build -DCMAKE_PREFIX_PATH=/usr/local \
  -DCMAKE_CXX_FLAGS="-DDEBUG_SEEK_VIDEO -DDEBUG_VIA_VIDEO_INPUT" \
  -DAPP_VERSION=0.0.0-dev > $RUN/mut_cmake.log 2>&1 \
  && cmake --build $RUN/mut/build -j 4 > $RUN/mut_build.log 2>&1
MUT_RC=$?
if [ "$MUT_RC" = "0" ]; then say "OK   the adopting tree builds, so what follows is about behaviour" ok
else say "FAIL the mutated tree did not build; see mut_build.log" no; tail -20 $RUN/mut_build.log; fi

if [ "$MUT_RC" = "0" ]; then
  arm adopts $RUN/adopts later "" 30 $RUN/mut/build/opendartboard
  grep -E 'GEOMETRY SEALED|BOARD ADOPTED GEOMETRY MID-RUN|BOARD RECOVERED' $RUN/adopts.txt | head -6
  echo "=== 5a. the guard fired, and named both geometries ==="
  if grep -q 'BOARD ADOPTED GEOMETRY MID-RUN' $RUN/adopts.txt; then
    say "OK   the adopting board is refused by name" ok
    grep -o 'BOARD ADOPTED GEOMETRY MID-RUN.\{0,200\}' $RUN/adopts.txt | head -1
  else say "FAIL the board adopted fresh geometry mid-run and nothing noticed" no; fi
  echo "=== 5b. and it stopped scoring rather than carrying on ==="
  if grep -q 'It refuses to score rather than go on' $RUN/adopts.txt; then
    say "OK   it refuses to score" ok
  else say "FAIL the adopting board went on scoring" no; fi
fi

echo "=== 5c. the same arm on the real tree does NOT fire it ==="
# The positive control's negative half. Phase 2 already ran a recovery on this tree and
# said nothing; this is that stated as the comparison it is.
if grep -q 'BOARD ADOPTED GEOMETRY MID-RUN' $RUN/knock.txt; then
  say "FAIL the guard fires on the unmutated tree, so it is measuring something else" no
else say "OK   one tree adopts and fires it, the other does not" ok; fi

echo "=============== PHASE 6: CONTROL -- the untouched mocks ==============="
mkdir -p $RUN/control
( cd $RUN/control && OD_MAX_CYCLES=20 /app/build/opendartboard \
    --cams /app/mocks/cam_1.mp4,/app/mocks/cam_2.mp4,/app/mocks/cam_3.mp4 \
    --width 1280 --height 720 > $RUN/control.out 2> $RUN/control.err )
echo "CONTROL_RC=$?"
plain $RUN/control.out > $RUN/control.txt
if grep -q 'Initial calibration completed successfully' $RUN/control.txt; then
  say "OK   the mocks calibrate" ok
else say "FAIL the mocks did not calibrate" no; fi
grep -E '^\[(ERROR|WARN)\]' $RUN/control.txt || true
NOISE=$(grep -cE '^\[(ERROR|WARN)\]' $RUN/control.txt || true)
if [ "$NOISE" = "0" ]; then say "OK   the control prints no ERROR and no WARN" ok
else say "FAIL the control prints $NOISE ERROR/WARN lines" no; fi
QUIET=$(grep -cE 'BOARD GEOMETRY|BOARD SETTLED|BOARD MOVED|BOARD ADOPTED|GEOMETRY FAULT' $RUN/control.txt || true)
if [ "$QUIET" = "0" ]; then say "OK   a board nothing happened to says nothing about its geometry" ok
else say "FAIL the control says $QUIET things about geometry" no; fi
if grep -q 'GEOMETRY SEALED' $RUN/control.txt; then
  say "OK   it did seal, so the silence above is a board with a seal and no breach" ok
else say "FAIL the control never sealed its geometry" no; fi

echo "CHECK_RC=$FAILED"
exit $FAILED
