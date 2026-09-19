set -u
# #1372: a board that comes up on a CACHED calibration, and whether anything asks it
# whether it can score.
#
# Until #1372 nothing did. `GeometryDetector::initialize` had two halves: the cache branch
# loaded the file, applied the operator's anchors, set `calibrated = true` and RETURNED,
# so neither `whyNoEventIsPossible` (#1338, amended by #1348) nor
# `whyNoStateChangeIsPossible` (#1348) was reached on that path. A board restarted on a
# cache could beat READY while unable to move its own state, which is #1338's state
# exactly, reached through a file instead of through a camera.
#
# It was reachable the whole time, not latent: #1330 restored the read and put it behind
# --reuse-calibration, so every phase here is a supported command line and not a patched
# binary.
#
# Four phases, and the last two are what make the first two mean anything:
#
#   S  the seed. The shipped mocks, fresh, three of three -- which is what writes the
#      cache the rest of this script reads. It must calibrate and say nothing new.
#   A  the control: the same three cameras, on the cache. It CAN score, so it must be
#      admitted and it must beat READY. Without A, B passes on a guard that refuses
#      everything.
#   B  the same cache with two cameras dropped. One camera can vote, the vote takes two,
#      so it must be refused and must beat ERROR rather than READY -- and the sentence
#      must name the count against the threshold it fell short of (#1321's rule).
#
#      B is also what measures the half of #1372 that is not the gate. The cached file
#      says all three cameras saw a board, because they did on the start that wrote it;
#      if the census were taken off the file alone, `voting` would be 3 here and the gate
#      would admit this board. It reads 1 because the cache branch writes off a camera
#      that produced no frame TONIGHT, the way calibrateMultipleCameras already does for
#      an empty frame.
#
#   C  falsification: the same board, the same binary, the same cache, with
#      OD_CACHE_SKIPS_THE_GATE=1 -- which restores the cache branch to what it was before
#      this issue, admitted on the file with no arithmetic asked. It must beat READY
#      again, or B is a claim about a build rather than about a rule.
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
    grep -qaE "$needle" "$file" 2>/dev/null && { echo "$i"; return 0; }
    i=$((i + 1)); sleep 1
  done
  echo "TIMEOUT-$limit"; return 1
}

# ONE stub for the whole script: the credential a pairing issues lives in the stub's
# memory, and a stub restarted between phases answers every heartbeat 401, which reads in
# the transcript as a board that never beat.
TRANSCRIPT=/run1372/transcript.jsonl
STUB_TRANSCRIPT=$TRANSCRIPT STUB_INTERVAL_SECONDS=5 STUB_SILENCE_SECONDS=60 STUB_SAMPLE_SECONDS=0 \
  python3 /app/testers/turnaus_stub.py > /run1372/stub.out 2> /run1372/stub.err &
STUB=$!
sleep 1

# The phases share one transcript, so each is a slice of it, marked at both ends -- #1338's
# rule, and it was bought there: a slice open at the top end reads a later phase's READY as
# this phase's.
mark() { wc -l < "$TRANSCRIPT" 2>/dev/null || echo 0; }
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

echo "--- pairing, once, against the stub ---"
$BIN --pair 483920 --turnaus $STUB_URL --allow-plaintext > /run1372/pair.out 2> /run1372/pair.err
echo "PAIR_RC=$?"

echo "=== S: the seed -- the mocks, measured fresh, which is what writes cache/ ==="
OD_MAX_CYCLES=20 $BIN --cams $MOCKS --width 1280 --height 720 \
  --turnaus $STUB_URL --allow-plaintext > /run1372/s.out 2> /run1372/s.err
echo "S_RC=$?"
ls -l /run1372/cache/ 2>&1 | sed 's/^/    /'

echo "=== A: the control -- the same three cameras, on the cache ==="
A_FROM=$(mark)
$BIN --reuse-calibration --cams $MOCKS --width 1280 --height 720 \
  --turnaus $STUB_URL --allow-plaintext > /run1372/a.out 2> /run1372/a.err &
A=$!
echo "A reached its verdict after $(await /run1372/a.out 'Scorer running with|BOARD FAULTED' 120)s"
sleep 18
kill -TERM $A 2>/dev/null; wait $A 2>/dev/null
echo "A_RC=$?"
A_TO=$(mark)

echo "=== B: the same cache, two cameras dropped, so one camera can vote ==="
B_FROM=$(mark)
OD_DROP_CAM=1,2 OD_DROP_EVERY=1 $BIN --reuse-calibration --cams $MOCKS --width 1280 --height 720 \
  --turnaus $STUB_URL --allow-plaintext > /run1372/b.out 2> /run1372/b.err &
B=$!
echo "B reached its verdict after $(await /run1372/b.out 'Scorer running with|BOARD FAULTED' 120)s"
sleep 18
kill -TERM $B 2>/dev/null; wait $B 2>/dev/null
echo "B_RC=$?"
B_TO=$(mark)

echo "=== C: falsification -- the same board with the gate made unreachable ==="
C_FROM=$(mark)
OD_CACHE_SKIPS_THE_GATE=1 OD_DROP_CAM=1,2 OD_DROP_EVERY=1 $BIN --reuse-calibration \
  --cams $MOCKS --width 1280 --height 720 \
  --turnaus $STUB_URL --allow-plaintext > /run1372/c.out 2> /run1372/c.err &
C=$!
echo "C reached its verdict after $(await /run1372/c.out 'Scorer running with|BOARD FAULTED' 120)s"
sleep 18
kill -TERM $C 2>/dev/null; wait $C 2>/dev/null
echo "C_RC=$?"
C_TO=$(mark)

kill $STUB 2>/dev/null; wait $STUB 2>/dev/null

# Colour codes are in every console line; strip them once and read the plain text.
for f in s a b c; do sed 's/\x1b\[[0-9;]*m//g' /run1372/$f.out > /run1372/$f.txt; done

FAILED=0
say() { echo "$1"; [ "$2" = ok ] || FAILED=1; }

echo
echo "=== 1. the seed wrote a cache, measuring all three, and said nothing new ==="
if grep -qa 'Initial calibration completed successfully on 3 of 3 cameras' /run1372/s.txt; then
  say "OK   the mocks calibrate, all three" ok
else say "FAIL the mocks did not calibrate, so every phase below reads an empty cache" no; fi
grep -aE '^\[(ERROR|WARN)\]' /run1372/s.txt || true
NOISE=$(grep -caE '^\[(ERROR|WARN)\]' /run1372/s.txt || true)
if [ "$NOISE" = "0" ]; then say "OK   the control prints no ERROR and no WARN" ok
else say "FAIL the control prints $NOISE ERROR/WARN lines" no; fi
if [ -s /run1372/cache/geometry_calibration.dat ]; then
  say "OK   cache/geometry_calibration.dat exists, so the phases below have a file to come up on" ok
else say "FAIL no calibration file was written; nothing below is about a cached start" no; fi

echo
echo "=== 2. the control: a board that comes up on a cache and CAN score still does ==="
grep -aE 'Using the cached calibration|Cached calibration accepted|cannot be scored with|Scorer running with' /run1372/a.txt | head -4 || true
if grep -qa 'Using the cached calibration for 3 cameras' /run1372/a.txt; then
  say "OK   it really came up on the file rather than measuring the board again" ok
else say "FAIL this phase did not read the cache, so it is not about a cached start" no; fi
if grep -qa 'Cached calibration accepted on 3 of 3 cameras' /run1372/a.txt; then
  say "OK   the gate admits it, and the verdict names where the geometry came from" ok
else say "FAIL a cached board that can score was not admitted" no; fi
if grep -qa 'Scorer running with 3 of 3 cameras' /run1372/a.txt; then
  say "OK   and the scorer repeats the census the detector decided (#1338)" ok
else say "FAIL the cached path leaves Scorer with no census to repeat" no; fi
echo "    beats A made: $(beats "$A_FROM" "$A_TO" | sort | uniq -c | tr '\n' ' ')"
if beats "$A_FROM" "$A_TO" | grep -qx READY; then
  say "OK   it beat READY, so the guard below is not one that refuses everything" ok
else say "FAIL a healthy cached board never beat READY" no; fi

echo
echo "=== 3. a cached board that cannot move its own state is refused ==="
grep -aE 'Using the cached calibration|Cached calibration accepted|cannot be scored with|BOARD FAULTED|Scorer running with' /run1372/b.txt | head -4 || true
if grep -qa 'Using the cached calibration for 3 cameras' /run1372/b.txt; then
  say "OK   it came up on the file, which is the path this issue is about" ok
else say "FAIL this phase did not read the cache" no; fi
if grep -qa 'Scorer running with' /run1372/b.txt; then
  say "FAIL a board that can never change state started scoring off a cache" no
else say "OK   it did not start scoring" ok; fi
if grep -qa 'The cached calibration cannot be scored with: only 1 of 3 cameras can vote on what is on the board' /run1372/b.txt; then
  say "OK   the refusal names the voting population and says the geometry was cached" ok
else say "FAIL the cached path does not refuse with the vote's sentence" no; fi
if grep -qa 'it takes 2 of them to move the board' /run1372/b.txt; then
  say "OK   and the threshold it fell short of, in the same sentence (#1321)" ok
else say "FAIL the sentence does not state the count against its threshold" no; fi
if grep -qa 'BOARD FAULTED: only 1 of 3 cameras can vote' /run1372/b.txt; then
  say "OK   the vigil repeats the arithmetic, not where the numbers came from" ok
else say "FAIL BOARD FAULTED does not carry the reason" no; fi
# #1372's own half: the file says all three saw a board. The census must read 1 because a
# camera that produced no frame tonight is written off before it is counted.
# OD_DROP_CAM names slots, zero-based, so 1,2 is cameras 2 and 3 in the log's own numbering.
if grep -qa 'Camera 2 produced no frame this start, so the cached calibration for it cannot be scored with' /run1372/b.txt \
   && grep -qa 'Camera 3 produced no frame this start' /run1372/b.txt; then
  say "OK   the two silent cameras are written off by name, so the census is about tonight" ok
else say "FAIL a silent camera carried its cached board into the vote" no; fi
# #1321's rule read out of the run: the count printed is on the wrong side of its threshold.
BAD=$(grep -oaE 'only ([0-9]+) of [0-9]+ cameras can vote on what is on the board.*it takes ([0-9]+) of them' /run1372/b.txt \
  | sed -E 's/^only ([0-9]+) of.*it takes ([0-9]+) of them.*/\1 \2/' \
  | awk '{ if ($1 >= $2) print }' | wc -l)
if [ "$BAD" = "0" ]; then say "OK   every count printed is below the threshold it is printed against" ok
else say "FAIL $BAD refusals print a count that is not below its own threshold" no; fi
echo "    beats B made: $(beats "$B_FROM" "$B_TO" | sort | uniq -c | tr '\n' ' ')"
if [ "$(beats "$B_FROM" "$B_TO" | wc -l)" -ge 2 ]; then
  say "OK   the board beat at all, so an absence of READY is an absence and not a silence" ok
else say "FAIL fewer than two beats reached the stub, so this phase measured nothing" no; fi
if beats "$B_FROM" "$B_TO" | grep -qx READY; then
  say "FAIL a board that came up on a cache and cannot score beat READY" no
else say "OK   no READY beat from a cached board that cannot score" ok; fi
if beats "$B_FROM" "$B_TO" | grep -qx ERROR; then
  say "OK   it beat ERROR, which is the word that sends somebody to the computer" ok
else say "FAIL it never beat ERROR either" no; fi

echo
echo "=== 4. falsification: the same board, the same binary, the gate made unreachable ==="
grep -aE 'OD_CACHE_SKIPS_THE_GATE|Scorer running with|BOARD FAULTED' /run1372/c.txt | head -3 || true
if grep -qa 'OD_CACHE_SKIPS_THE_GATE is set' /run1372/c.txt; then
  say "OK   the hatch is open, and the run says so out loud" ok
else say "FAIL the falsification switch did nothing, so phase B is not attributable to the gate" no; fi
if grep -qa 'Scorer running with' /run1372/c.txt; then
  say "OK   with the gate unreachable the same board starts scoring again" ok
else say "FAIL it did not start scoring, so phase B's refusal is not the gate's doing" no; fi
echo "    beats C made: $(beats "$C_FROM" "$C_TO" | sort | uniq -c | tr '\n' ' ')"
if beats "$C_FROM" "$C_TO" | grep -qx READY; then
  say "OK   and it beats READY -- which is what #1372 was filed about, reproduced on this binary" ok
else say "FAIL the hatch did not restore the behaviour this issue changed" no; fi

echo "CHECK_RC=$FAILED"
exit $FAILED
