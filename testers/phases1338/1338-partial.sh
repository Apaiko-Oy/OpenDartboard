set -u
# #1338: a board that cannot form a dart event, and what it tells Turnaus about itself.
#
# Measured on the maintainer's rig on 2026-09-18 against a CI build of fe66d2c: two of
# three cameras delivered no frames (#1319), and the detector printed `Initial calibration
# completed successfully on 1 of 3 cameras`, then `Scorer running with 3 cameras`, then
# beat READY. `motion_processing.hpp` sets `min_cameras_for_event = 2`, so a dart event
# needs a motion spike seen by two cameras at once -- and a camera that produced no frame
# to calibrate on has an empty background for the life of the run, is skipped by
# detectMotion for ever, and reports 0.0 motion whatever it does later. One answering
# camera is therefore not a board that scores rarely. It is a board in which
# `cameras_that_spiked` is bounded above by 1 against a threshold of 2.
#
# #1353 moved that constant to 1, and #1348 moved the sentence this phase reads. The board
# phase A builds is unchanged and so is its verdict -- it must not beat READY -- but the
# arithmetic that refuses it is now the dart-state VOTE's rather than the event quorum's:
# one camera can vote, moving the board takes two, so it holds CLEAN for ever and cannot
# see a takeout either. The two quorums are counted off ONE population now (a frame AND a
# fitted board); #1348's own tester holds the whole of that sentence, and what is asked
# here is that this board is still refused and still states a count against the threshold
# it fell short of.
#
# Five phases, and the last two are the ones that make the first three mean anything:
#
#   A  one camera answering of three. It must not beat READY, and the sentence it beats
#      ERROR with must name the count and the threshold.
#   B  two answering, both seeing. It MAY score -- that is the decision -- and the two
#      lines that disagreed on the rig must now say the same number about the same thing.
#   C  a camera that answered and failed to calibrate. It must read differently from a
#      camera that produced nothing, because one is aim or lighting and the other is a
#      cable.
#   D  the control: the shipped mocks, three of three, nothing new said.
#   E  the arithmetic on its own, with the threshold moved under a fixed board -- the one
#      thing a whole-binary run cannot do without a second build.
#
# Every detector started here can end in #895's fault vigil, which never returns, so each
# is backgrounded and ended by its own recorded pid. Never by pattern.

BIN=/app/build/opendartboard
MOCKS=/app/mocks/cam_1.mp4,/app/mocks/cam_2.mp4,/app/mocks/cam_3.mp4
STUB_URL=http://127.0.0.1:8899

# Wait for a sentinel the detector itself prints, never for a process pattern.
await() {
  local file="$1" needle="$2" limit="$3" i=0
  while [ $i -lt "$limit" ]; do
    grep -qa "$needle" "$file" 2>/dev/null && { echo "$i"; return 0; }
    i=$((i + 1)); sleep 1
  done
  echo "TIMEOUT-$limit"; return 1
}

# ONE stub for the whole script, because the credential a pairing issues lives in the
# stub's memory: a stub restarted between phases holds `club_token` at None and answers
# every heartbeat 401, which reads in the transcript as a board that never beat.
TRANSCRIPT=/run1338/transcript.jsonl
STUB_TRANSCRIPT=$TRANSCRIPT STUB_INTERVAL_SECONDS=5 STUB_SILENCE_SECONDS=60 STUB_SAMPLE_SECONDS=0 \
  python3 /app/testers/turnaus_stub.py > /run1338/stub.out 2> /run1338/stub.err &
STUB=$!
sleep 1

# The phases share one transcript, so each is a slice of it, marked at both ends. Both
# ends matter: a slice open at the top end reads every later phase's beats as this
# phase's, which is how the first run of this script reported a faulted board beating
# READY -- the READY was phase B's, thirty seconds later, in a different process.
mark() { wc -l < "$TRANSCRIPT" 2>/dev/null || echo 0; }

# What conditions this board beat during one phase, in the order it beat them.
beats() {
  python3 -c "
import json, sys
start, end = int(sys.argv[2]), int(sys.argv[3])
for n, line in enumerate(open(sys.argv[1])):
    if n < start or n >= end:
        continue
    try: r = json.loads(line)
    except ValueError: continue
    if r.get('event') in ('beat', 'beat_refused'):
        print(r.get('condition', ''))
" "$TRANSCRIPT" "$1" "$2"
}

echo "--- the footage of a camera that opens and sees no dartboard (#1318's generator) ---"
g++ -std=c++17 -O1 -o /run1338/make_source /app/testers/i1318_make_source.cpp \
  $(pkg-config --cflags --libs opencv4) || exit 1
/run1338/make_source face /run1338/face.avi 1280 720 15 120 || exit 1

echo "--- pairing, once, against the stub ---"
$BIN --pair 483920 --turnaus $STUB_URL --allow-plaintext > /run1338/pair.out 2> /run1338/pair.err
echo "PAIR_RC=$?"

echo "=== A: one camera answering of three (OD_DROP_CAM=1,2) ==="
A_FROM=$(mark)
OD_DROP_CAM=1,2 OD_DROP_EVERY=1 $BIN --cams $MOCKS --width 1280 --height 720 \
  --turnaus $STUB_URL --allow-plaintext > /run1338/a.out 2> /run1338/a.err &
A=$!
echo "A reached its verdict after $(await /run1338/a.out 'BOARD FAULTED' 120)s"
sleep 18
kill -TERM $A 2>/dev/null; wait $A 2>/dev/null
echo "A_RC=$?"
A_TO=$(mark)

echo "=== B: two answering, both looking at the board (OD_DROP_CAM=2) ==="
B_FROM=$(mark)
OD_DROP_CAM=2 OD_DROP_EVERY=1 $BIN --cams $MOCKS --width 1280 --height 720 \
  --turnaus $STUB_URL --allow-plaintext > /run1338/b.out 2> /run1338/b.err &
B=$!
echo "B started scoring after $(await /run1338/b.out 'Scorer running with' 180)s"
sleep 18
kill -TERM $B 2>/dev/null; wait $B 2>/dev/null
echo "B_RC=$?"
B_TO=$(mark)

echo "=== C: three answering, one of them not looking at a dartboard ==="
OD_MAX_CYCLES=20 $BIN --cams /run1338/face.avi,/app/mocks/cam_1.mp4,/app/mocks/cam_2.mp4 \
  --width 1280 --height 720 --turnaus $STUB_URL --allow-plaintext > /run1338/c.out 2> /run1338/c.err
echo "C_RC=$?"

echo "=== D: the control, the footage the detector is known to calibrate on ==="
OD_MAX_CYCLES=20 $BIN --cams $MOCKS --width 1280 --height 720 \
  --turnaus $STUB_URL --allow-plaintext > /run1338/d.out 2> /run1338/d.err
echo "D_RC=$?"

echo "=== E: the arithmetic, with min_cameras_for_event moved under a fixed board ==="
g++ -std=c++17 -O1 -I /app/src -I /app/src/utils -o /run1338/event_check \
  /app/testers/i1338_event_check.cpp /app/src/detector/geometry/detection/motion_processing.cpp \
  $(pkg-config --cflags --libs opencv4) || exit 1
/run1338/event_check > /run1338/e.txt 2>&1
echo "E_RC=$?"
sed 's/^/    /' /run1338/e.txt
kill $STUB 2>/dev/null; wait $STUB 2>/dev/null

# Colour codes are in every console line; strip them once and read the plain text.
for f in a b c d; do sed 's/\x1b\[[0-9;]*m//g' /run1338/$f.out > /run1338/$f.txt; done

FAILED=0
say() { echo "$1"; [ "$2" = ok ] || FAILED=1; }

echo "=== 1. the one-camera board does not beat READY, and beats ERROR instead ==="
echo "    beats A made: $(beats "$A_FROM" "$A_TO" | sort | uniq -c | tr '\n' ' ')"
if [ "$(beats "$A_FROM" "$A_TO" | wc -l)" -ge 2 ]; then
  say "OK   the board beat at all, so an absence of READY below is an absence and not a silence" ok
else say "FAIL fewer than two beats reached the stub, so this phase measured nothing" no; fi
if beats "$A_FROM" "$A_TO" | grep -qx READY; then
  say "FAIL a board that cannot form a dart event beat READY" no
else say "OK   no READY beat from a board that cannot form a dart event" ok; fi
if beats "$A_FROM" "$A_TO" | grep -qx ERROR; then
  say "OK   it beat ERROR, which is the word that sends somebody to the computer" ok
else say "FAIL it never beat ERROR either" no; fi

echo "=== 2. and it says, in one sentence, the count it has and the count it needs ==="
grep -aE 'Initial calibration failed|BOARD FAULTED: only' /run1338/a.txt | head -2 || true
if grep -qaE 'Initial calibration failed: only 1 of 3 cameras can vote on what is on the board.*it takes 2 of them to move the board' /run1338/a.txt; then
  say "OK   the refusal names 1 of 3 against the 2 it takes to move the board (#1348)" ok
else say "FAIL the refusal does not state the count against its threshold" no; fi
if grep -qa 'BOARD FAULTED: only 1 of 3 cameras can vote' /run1338/a.txt; then
  say "OK   and the vigil repeats that sentence rather than a choice of two" ok
else say "FAIL BOARD FAULTED does not carry the reason" no; fi
# The number printed must be on the wrong side of the threshold printed beside it
# (#1321's rule): a board refused for having MORE cameras than it needs did not fail here.
BAD=$(grep -oaE 'only ([0-9]+) of [0-9]+ cameras can vote on what is on the board.*it takes ([0-9]+) of them' /run1338/a.txt \
  | sed -E 's/^only ([0-9]+) of.*it takes ([0-9]+) of them.*/\1 \2/' \
  | awk '{ if ($1 >= $2) print }' | wc -l)
if [ "$BAD" = "0" ]; then say "OK   every count printed is below the threshold it is printed against" ok
else say "FAIL $BAD refusals print a count that is not below its own threshold" no; fi

echo "=== 3. the two lines that disagreed now say the same number about the same thing ==="
grep -aE 'Initial calibration completed successfully|Scorer running with' /run1338/b.txt || true
if grep -qa 'Initial calibration completed successfully on 2 of 3 cameras' /run1338/b.txt \
   && grep -qa 'Scorer running with 2 of 3 cameras' /run1338/b.txt; then
  say "OK   'successfully on 2 of 3' and 'running with 2 of 3', four lines apart" ok
else say "FAIL the calibration line and the scorer line do not agree" no; fi
# The control for that: the two numbers are read out of the run and compared, so a build
# that printed 'N of M' twice from the same wrong place would still pass line by line.
CAL=$(grep -oa 'Initial calibration completed successfully on [0-9]* of [0-9]*' /run1338/b.txt | grep -oE '[0-9]+ of [0-9]+' | head -1)
RUN=$(grep -oa 'Scorer running with [0-9]* of [0-9]*' /run1338/b.txt | grep -oE '[0-9]+ of [0-9]+' | head -1)
echo "    calibration says '$CAL'; the scorer says '$RUN'"
if [ -n "$CAL" ] && [ "$CAL" = "$RUN" ]; then say "OK   read out of the run, the two are the same string" ok
else say "FAIL '$CAL' is not '$RUN'" no; fi
if beats "$B_FROM" "$B_TO" | grep -qx READY; then
  say "OK   and a board that CAN form an event is allowed to say so" ok
else say "FAIL the two-camera board never beat READY, so the gate refuses everything" no; fi
if grep -qa 'Scoring on 2 of 3 cameras (1 produced no frame), not on all 3' /run1338/b.txt; then
  say "OK   it says out loud that it is scoring on fewer cameras than it has" ok
else say "FAIL a degraded board says nothing about being degraded" no; fi

echo "=== 4. a silent camera and a refused camera are two different sentences ==="
grep -a 'CAMERAS:' /run1338/a.txt /run1338/c.txt | head -2 || true
if grep -qa 'CAMERAS: 1 of 3 are looking at the dartboard (1); produced no frame: 2,3' /run1338/a.txt; then
  say "OK   the cameras that delivered nothing are named as having produced no frame" ok
else say "FAIL the silent cameras are not named as silent" no; fi
if grep -qa 'CAMERAS: 2 of 3 are looking at the dartboard (2,3); refused: 1' /run1338/c.txt \
   && ! grep -qa 'produced no frame' /run1338/c.txt; then
  say "OK   the camera that answered and failed is 'refused', and nothing is called silent" ok
else say "FAIL a camera that answered and failed to calibrate reads like one that did not answer" no; fi
if grep -qa 'Camera 2 produced no frame to calibrate on, so it was never looked at; that is a cable, a hub or the bandwidth it shares, not its aim' /run1338/a.txt; then
  say "OK   the remedy for a silent camera is named, and it is not its aim" ok
else say "FAIL nothing tells the reader where to go for a camera that produced no frame" no; fi
if grep -qaE '^\[ERROR\]\[GEOMETRY_CALIBRATION\] - Camera 1 .*is not looking at the dartboard' /run1338/c.txt; then
  say "OK   the remedy for a refused camera is named, and it IS its aim" ok
else say "FAIL the refused camera does not say what it failed on" no; fi

echo "=== 5. the control calibrates and says nothing new ==="
if grep -qa 'Initial calibration completed successfully on 3 of 3 cameras' /run1338/d.txt; then
  say "OK   the mocks calibrate, all three" ok
else say "FAIL the mocks did not calibrate" no; fi
if grep -qa 'Scorer running with 3 of 3 cameras' /run1338/d.txt; then
  say "OK   and the scorer repeats the same census" ok
else say "FAIL the scorer does not state the census on a healthy board" no; fi
grep -aE '^\[(ERROR|WARN)\]' /run1338/d.txt || true
NOISE=$(grep -caE '^\[(ERROR|WARN)\]' /run1338/d.txt || true)
if [ "$NOISE" = "0" ]; then say "OK   the control prints no ERROR and no WARN" ok
else say "FAIL the control prints $NOISE ERROR/WARN lines" no; fi

echo "=== 6. the gate is driven by min_cameras_for_event and can be made to pass ==="
if grep -q 'EVENT_CHECK_OK=0' /run1338/e.txt; then
  say "OK   the same one-camera board is refused at a threshold of 2 and admitted at 1" ok
else say "FAIL the arithmetic check did not pass; its own lines are above" no; fi

echo "CHECK_RC=$FAILED"
exit $FAILED
