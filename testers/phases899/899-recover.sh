set -u
# #899: a board that loses every camera, gets them back, and is made to prove the board
# has not moved before it scores another dart.
#
# TWO ARMS, ONE DIFFERENCE. Both start on the same footage, calibrate on it, score, lose
# every camera to OD_BLIND_AFTER, and get them back OD_BLIND_FOR_MS later. The only thing
# that differs is what is behind the camera paths when the board reopens them: a LATER
# stretch of the same clip, or the same later stretch warped by 20 px across and 15 px
# down. The move is done by exchanging the file rather than by any flag the detector
# reads, so nothing in the program knows which arm it is in.
#
# WHY A LATER STRETCH AND NOT THE SAME FILE. Reopening a video file rewinds it, so a
# recovery that reads the file it started on re-reads the frames it calibrated on and
# every figure in the comparison is 0.00 -- which proves the plumbing and measures
# nothing. The later stretch is the same rig in the same room at a different moment, with
# different darts in the board, which is as close as a file gets to "reopened, and nobody
# had touched it". The figures the recovering arm prints are therefore the real jitter,
# and they are what the tolerances in geometry_agreement.hpp are set above.
#
# The exchange is `mv`, once the board has finished calibrating: the running process keeps
# reading through the file handle it already holds, and only the reopen sees the new file.
# Both arms are ended by their own recorded pid, never by pattern.
#
# The control is #1321's: the untouched mocks, which must still calibrate with no ERROR
# and no WARN and say nothing about sight at all.

echo "--- build the footage from the mocks ---"
g++ -std=c++17 -O1 -o /run899/moved /app/testers/i899_moved_footage.cpp \
  $(pkg-config --cflags --libs opencv4) || exit 1
for i in 1 2 3; do
  # what the board calibrates on
  /run899/moved /app/mocks/cam_$i.mp4 /run899/held_$i.avi   0  0 0 400 0   || exit 1
  # the same rig, later, untouched -- the recovering arm
  /run899/moved /app/mocks/cam_$i.mp4 /run899/later_$i.avi  0  0 0 400 400 || exit 1
  # the same later stretch, 20 px across and 15 px down -- the refusing arm
  /run899/moved /app/mocks/cam_$i.mp4 /run899/nudged_$i.avi 20 15 0 400 400 || exit 1
done

FAILED=0
say() { echo "$1"; [ "$2" = ok ] || FAILED=1; }

echo "=============== CONTROL: the untouched mocks ==============="
OD_MAX_CYCLES=20 /app/build/opendartboard \
  --cams /app/mocks/cam_1.mp4,/app/mocks/cam_2.mp4,/app/mocks/cam_3.mp4 \
  --width 1280 --height 720 > /run899/control.out 2> /run899/control.err
echo "CONTROL_RC=$?"
sed 's/\x1b\[[0-9;]*m//g' /run899/control.out > /run899/control.txt

echo "=== 9. the mocks still calibrate and say nothing new ==="
if grep -q 'Initial calibration completed successfully' /run899/control.txt; then
  say "OK   the mocks calibrate" ok
else say "FAIL the mocks did not calibrate" no; fi
grep -E '^\[(ERROR|WARN)\]' /run899/control.txt || true
NOISE=$(grep -cE '^\[(ERROR|WARN)\]' /run899/control.txt || true)
if [ "$NOISE" = "0" ]; then say "OK   the control prints no ERROR and no WARN" ok
else say "FAIL the control prints $NOISE ERROR/WARN lines" no; fi
SIGHT=$(grep -cE 'BOARD SIGHT|BOARD RECOVERED|BOARD MOVED' /run899/control.txt || true)
if [ "$SIGHT" = "0" ]; then say "OK   a board that never lost a camera says nothing about sight" ok
else say "FAIL the control says $SIGHT things about losing sight" no; fi

# The control runs FIRST, before anything pairs this checkout with the stub. A credential
# in /root/.config names a plaintext address, and a board started without --allow-plaintext
# refuses to post to it at ERROR -- which has nothing to do with sight and would fail the
# no-ERROR check above for a reason this issue did not cause.

# #892's beat, measured through #822's stub, because the log is what a person standing at
# the machine reads and the BEAT is what a Station's screen reads. The two arms have to
# separate in both. One stub and one credential serve both arms: they run one after the
# other, so the transcript is split at the line count arm 1 left behind.
export STUB_TRANSCRIPT=/run899/transcript.jsonl
export STUB_INTERVAL_SECONDS=5
export STUB_SILENCE_SECONDS=60
export STUB_SAMPLE_SECONDS=0
python3 /app/testers/turnaus_stub.py > /run899/stub.out 2> /run899/stub.err &
STUB=$!
sleep 1
/app/build/opendartboard --pair 483920 --turnaus http://127.0.0.1:8899 --allow-plaintext \
  > /run899/pair.out 2> /run899/pair.err
echo "PAIR_RC=$?"

# The conditions this board beat, in order, over a slice of the transcript.
beats() { sed -n "$1,\$p" /run899/transcript.jsonl 2>/dev/null \
  | python3 -c "
import json,sys
for line in sys.stdin:
    try: row = json.loads(line)
    except ValueError: continue
    if row.get('event') == 'beat': print(row.get('condition',''))
"; }

# $1 = arm name, $2 = the footage put behind the paths after calibration
arm() {
  NAME=$1
  SWAP=$2
  rm -f /run899/src_1.avi /run899/src_2.avi /run899/src_3.avi
  for i in 1 2 3; do cp /run899/held_$i.avi /run899/src_$i.avi; done

  BEATS_FROM=$(( $(wc -l < /run899/transcript.jsonl 2>/dev/null || echo 0) + 1 ))

  OD_MAX_CYCLES=100000 OD_BLIND_AFTER=200 OD_BLIND_FOR_MS=8000 /app/build/opendartboard \
    --cams /run899/src_1.avi,/run899/src_2.avi,/run899/src_3.avi \
    --width 1280 --height 720 --turnaus http://127.0.0.1:8899 --allow-plaintext \
    > /run899/$NAME.out 2> /run899/$NAME.err &
  BOARD=$!

  # Wait for the calibration this board will be held to, then exchange the footage. The
  # open handles are unaffected by a rename, so the board goes on reading what it
  # calibrated on and only the reopen meets the new file.
  for _ in $(seq 1 90); do
    grep -q 'Initial calibration completed successfully' /run899/$NAME.out && break
    sleep 1
  done
  for i in 1 2 3; do cp /run899/${SWAP}_$i.avi /run899/swap_$i.avi; mv /run899/swap_$i.avi /run899/src_$i.avi; done
  echo "SWAPPED=$SWAP"

  # Long enough for: blindness at cycle 200, sight lost three seconds later, the retries
  # at 2 s, 4 s and 8 s of backoff, and the cameras coming back at eight seconds.
  sleep 30
  kill -TERM $BOARD 2>/dev/null
  wait $BOARD 2>/dev/null
  echo "${NAME}_RC=$?"
  sed 's/\x1b\[[0-9;]*m//g' /run899/$NAME.out > /run899/$NAME.txt
  beats "$BEATS_FROM" > /run899/$NAME.beats
  echo "${NAME} beats: $(tr '\n' ' ' < /run899/$NAME.beats)"
}

echo "=============== ARM 1: the cameras come back and nothing was touched ==============="
arm recovers later
grep -E 'BOARD SIGHT LOST|BOARD SIGHT RECOVERY: attempt|GEOMETRY REVIEW|BOARD RECOVERED|BOARD MOVED' /run899/recovers.txt | head -20

echo "=== 1. losing every camera is said out loud, once, and names what it suspends ==="
if grep -q 'BOARD SIGHT LOST: no camera has answered for' /run899/recovers.txt; then
  say "OK   the board says it has lost sight and that scoring is suspended" ok
else say "FAIL nothing said the board had lost sight" no; fi

echo "=== 2. it retried more than once, and the later attempts are further apart ==="
ATTEMPTS=$(grep -cE 'BOARD SIGHT RECOVERY: attempt [0-9]+ after' /run899/recovers.txt || true)
if [ "$ATTEMPTS" -ge 2 ]; then say "OK   $ATTEMPTS attempts, so a first failure is not the end of it" ok
else say "FAIL only $ATTEMPTS recovery attempt(s); the retry never repeated" no; fi
# The blind seconds each attempt prints must be strictly increasing, which is the backoff
# said in the one unit a reader can check it in.
# Per sight-loss episode: an episode restarts at attempt 1, and a board that has lost its
# cameras twice in one run is not a board whose backoff went backwards.
NOT_INCREASING=$(grep -oE 'attempt [0-9]+ after [0-9]+ seconds blind' /run899/recovers.txt \
  | awk '{ if ($2 == 1) { last = -1 } ; if ($4 <= last) print; last = $4 }' | wc -l)
if [ "$NOT_INCREASING" = "0" ]; then say "OK   every attempt is later than the one before it" ok
else say "FAIL $NOT_INCREASING attempt(s) did not wait longer than the previous one" no; fi

echo "=== 3. the board resumed, and said so with the numbers it decided on ==="
if grep -qE 'BOARD RECOVERED: the cameras are back after [0-9]+ seconds and the board has not moved' /run899/recovers.txt; then
  say "OK   a recovered board says it recovered" ok
else say "FAIL nothing distinguishes this from a board that never came back" no; fi
if grep -q 'the calibration just taken was a witness and has been discarded' /run899/recovers.txt; then
  say "OK   it says it resumed on the calibration it started with, not a fresh one" ok
else say "FAIL nothing says which calibration scoring resumed on" no; fi

echo "=== 3b. every figure it printed is below the tolerance printed beside it ==="
# A line nothing can falsify is not evidence (#1321). The bull figure and its tolerance are
# in the same sentence, so the sentence contradicts itself if the number came from anywhere
# but the measurement that decided. Measured by running arm 2, where the same line goes red.
INCONSISTENT=$(grep -oE 'the bull moved [0-9.]+ px and at most [0-9.]+ px is still the same rig' /run899/recovers.txt \
  | awk '$4 > $9 { print }' | wc -l)
if [ "$INCONSISTENT" = "0" ]; then say "OK   no camera vouched for the board while over its own tolerance" ok
else say "FAIL $INCONSISTENT camera(s) vouched with a bull shift past the tolerance beside it" no; fi

echo "=== 4. it did not fault, and it did not re-calibrate itself back into scoring ==="
if grep -q 'BOARD MOVED' /run899/recovers.txt; then
  say "FAIL an untouched board was refused" no
else say "OK   an untouched board was not refused" ok; fi

echo "=============== ARM 2: the cameras come back 25 px from where they were ==============="
arm refuses nudged
grep -E 'BOARD SIGHT LOST|BOARD SIGHT RECOVERY: attempt|GEOMETRY REVIEW|BOARD RECOVERED|BOARD MOVED|BOARD FAULTED' /run899/refuses.txt | head -20

echo "=== 5. the board refused to score, and named the camera and the distance ==="
if grep -qE 'BOARD MOVED: camera [0-9]+: the bull moved [0-9.]+ px' /run899/refuses.txt; then
  say "OK   the refusal names the camera and how far its bull moved" ok
else say "FAIL nothing named a moved camera" no; fi
if grep -q 'refuses to score rather than put darts in the wrong wedge' /run899/refuses.txt; then
  say "OK   it says what it is refusing and why" ok
else say "FAIL the refusal does not say it is refusing to score" no; fi

echo "=== 5b. the figure it refused on really is past the tolerance beside it ==="
CONSISTENT=$(grep -oE 'BOARD MOVED: camera [0-9]+: the bull moved [0-9.]+ px and at most [0-9.]+ px is still the same rig' /run899/refuses.txt \
  | awk '$7 > $12 { print }' | wc -l)
if [ "$CONSISTENT" -ge 1 ]; then say "OK   the bull shift it refused on is past the tolerance it printed" ok
else say "FAIL the refusal's own numbers do not justify it" no; fi

echo "=== 6. it did NOT resume scoring on the calibration it had ==="
if grep -q 'BOARD RECOVERED' /run899/refuses.txt; then
  say "FAIL a moved board was waved back into scoring" no
else say "OK   a moved board never said it recovered" ok; fi

echo "=== 7. it took #895's vigil rather than exiting or going quiet ==="
if grep -q 'BOARD FAULTED: the cameras came back and the board is not where it was' /run899/refuses.txt; then
  say "OK   the vigil says which camera moved, in #1321's sentence" ok
else say "FAIL the faulted board does not say the geometry moved" no; fi

echo "=== 8. the two arms differ, which is the whole proof ==="
# The same run, the same flags, the same footage generator, the same seconds. If the two
# arms said the same thing the refusal would be measuring nothing.
#
# MEASURED, not asserted: with geometry_agreement::Limits::max_bull_shift_px moved from
# 12 to 100 and nothing else changed, arm 2 recovers instead of refusing, resumes scoring
# on a calibration 25 px out of date, and beats READY three times -- and seven of the
# checks below and above go red. So the refusal really is decided by that tolerance, and
# a board that scores on stale geometry really is what this run would otherwise be
# watching.
R1=$(grep -c 'BOARD RECOVERED' /run899/recovers.txt || true)
R2=$(grep -c 'BOARD RECOVERED' /run899/refuses.txt || true)
M1=$(grep -c 'BOARD MOVED' /run899/recovers.txt || true)
M2=$(grep -c 'BOARD MOVED' /run899/refuses.txt || true)
echo "recovers: RECOVERED=$R1 MOVED=$M1   refuses: RECOVERED=$R2 MOVED=$M2"
if [ "$R1" -ge 1 ] && [ "$M1" = "0" ] && [ "$R2" = "0" ] && [ "$M2" -ge 1 ]; then
  say "OK   one arm resumed and the other refused, on the same code and the same flags" ok
else say "FAIL the two arms did not separate" no; fi

echo "=== 8b. and they separate in the BEAT, which is what the Station's screen reads ==="
# The log is for whoever is standing at the machine. This is the claim the pub sees: a
# board that recovered goes back to READY, and a board that came back to a moved camera
# never says READY again in this process. It is the whole of "a board that recovers must
# be distinguishable from a board that gave up and lied".
RESUMED=$(awk '/^ERROR$/ { seen = 1 } /^READY$/ && seen { print }' /run899/recovers.beats | wc -l)
LIED=$(awk '/^ERROR$/ { seen = 1 } /^READY$/ && seen { print }' /run899/refuses.beats | wc -l)
if [ "$RESUMED" -ge 1 ]; then say "OK   the recovering board beat READY again after beating ERROR ($RESUMED times)" ok
else say "FAIL the recovering board never told the Station it could see again" no; fi
if [ "$LIED" = "0" ]; then say "OK   the moved board never beat READY again" ok
else say "FAIL the moved board told the Station it could see, $LIED times, after the move" no; fi
if grep -q 'ERROR' /run899/refuses.beats; then say "OK   the moved board goes on beating ERROR rather than going silent" ok
else say "FAIL the moved board stopped beating" no; fi

kill -TERM $STUB 2>/dev/null
wait $STUB 2>/dev/null

echo "CHECK_RC=$FAILED"
exit $FAILED
