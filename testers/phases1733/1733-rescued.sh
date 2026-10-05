set -u
# #1733: a camera a later look rescued is not the board's fault.
#
# #1445 gives a camera refused on the averaged frame further looks, and keeps the fault
# record clean of what the LOOKS say (#1457's "a look is not a fault"). What it kept was
# the fault the FIRST PASS recorded -- and the first pass's refusal is about exactly the
# camera the looks are for. `board_sight::recordFault` is first-fault-wins, so a camera
# refused on the averaged frame and sealed by look 1 left `camera N did not calibrate` in
# the slot for the life of the process, and every later fault -- the gate's own, a BOARD
# MOVED at nine o'clock -- was told in that camera's words instead of its own.
#
# What carries that sentence, measured on fork 778dd19 before this was written:
#   - the log's BOARD FAULTED line, and its once-a-minute reminder;
#   - the supervisor's STATUS= (#1383), "this board cannot see -- <sentence>";
#   - NOT the beat. The heartbeat body is {"condition", "cameras"} and nothing else, and
#     the condition is ERROR only when board_sight::faulted() is set, which nothing in
#     the looks touches. So a rescued board that scores beats READY; one that faults
#     beats ERROR for its real fault, and the wrong sentence is local to the box.
#
# Three phases, each with the stub (what reaches Turnaus) and the notify listener (what
# reaches systemd) attached:
#
#   A  RESCUED, then refused for something else. 1331 §5's "edge" clip (the shipped
#      mock's camera 1 shifted 300,0), alone: the averaged frame is refused at 0 px, a
#      look seals it, and the board is then refused by the vote's arithmetic -- one
#      camera of the two it takes. BOARD FAULTED must say THAT, not the rescued camera's
#      refusal. On 778dd19 it said "camera 1 did not calibrate: it is not looking at a
#      WHOLE dartboard", one line under the gate's "camera 1: sees the dartboard and can
#      vote".
#   B  REFUSED ON EVERY LOOK. 1331 §5's "cut" clip (430,0). The camera never calibrates,
#      and BOARD FAULTED carries the first pass's refusal, the same sentence as before:
#      held equal to the camera's own ERROR line, so it is today's words and not a copy
#      of them in this file.
#   C  RESCUED, SCORING, AND THEN MOVED -- the venue's case. rig-20260922: camera 1 is
#      refused on its averaged frame and sealed by a look (#1605). The board seals 3 of
#      3 and beats READY. It is blinded, and camera 1 comes back nudged 20 px across and
#      15 down (#899's warp), so the review refuses it: BOARD MOVED. BOARD FAULTED and
#      STATUS= must name the move, not the averaged frame camera 1 was refused on before
#      the look that scored the whole evening.
#
# Every detector run is ended by its own recorded pid, never by pattern: a board that
# cannot calibrate takes #895's vigil and stays up on purpose.
BIN="${OD_1733_BIN:-/app/build/opendartboard}"
STUB_URL=http://127.0.0.1:8899
CVFLAGS="$(pkg-config --cflags --libs opencv4)"
FAILED=0
say() { echo "$1"; [ "${2:-no}" = ok ] || FAILED=1; }
echo "binary: $BIN"

await() {
  local file="$1" needle="$2" limit="$3" i=0
  while [ $i -lt "$limit" ]; do
    grep -qaE "$needle" "$file" 2>/dev/null && { echo "$i"; return 0; }
    i=$((i + 1)); sleep 1
  done
  echo "TIMEOUT-$limit"; return 1
}
plain() { sed 's/\x1b\[[0-9;]*m//g' "$1"; }

# ONE stub for the script: the pairing's credential lives in its memory (1338's reason).
TRANSCRIPT=/run1733/transcript.jsonl
STUB_TRANSCRIPT=$TRANSCRIPT STUB_INTERVAL_SECONDS=2 STUB_SILENCE_SECONDS=60 STUB_SAMPLE_SECONDS=0 \
  python3 /app/testers/turnaus_stub.py > /run1733/stub.out 2> /run1733/stub.err &
STUB=$!
sleep 1
mark() { wc -l < "$TRANSCRIPT" 2>/dev/null || echo 0; }

# The beats one phase made: "<condition> <sorted body keys>", one per line.
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
        print(r.get('condition', ''), ','.join(r.get('body_keys') or []))
" "$TRANSCRIPT" "$1" "$2"
}

# The supervisor half (#1383's listener), one transcript per phase.
SOCK=/run1733/notify.sock
LISTENER=""
listen() {
  rm -f "$SOCK" /run1733/listener.out; : > "$1"
  python3 /app/testers/i1383_notify_listener.py "$SOCK" "$1" > /run1733/listener.out 2>&1 &
  LISTENER=$!
  local waited=0
  while ! grep -q READY /run1733/listener.out 2>/dev/null && [ "$waited" -lt 100 ]; do
    sleep 0.2; waited=$((waited + 1))
  done
}
unlisten() { [ -n "$LISTENER" ] && { kill "$LISTENER" 2>/dev/null; wait "$LISTENER" 2>/dev/null; }; LISTENER=""; }
stop() { kill -TERM "$1" 2>/dev/null; wait "$1" 2>/dev/null; }

# The sentence BOARD FAULTED carries, without the vigil's own tail.
faulted_with() { grep -aoE 'BOARD FAULTED: .*\. It stays up and beats ERROR' "$1" | head -1 \
  | sed -E 's/^BOARD FAULTED: //; s/\. It stays up and beats ERROR$//'; }

echo "--- the footage ---"
g++ -std=c++17 -O1 -o /run1733/offaim /app/testers/i1323_offaim_footage.cpp $CVFLAGS || exit 2
/run1733/offaim /app/mocks/cam_1.mp4 /run1733/off_edge.avi 150 300 0 > /dev/null || exit 2
/run1733/offaim /app/mocks/cam_1.mp4 /run1733/off_cut.avi  150 430 0 > /dev/null || exit 2
g++ -std=c++17 -O1 -o /run1733/moved /app/testers/i899_moved_footage.cpp $CVFLAGS || exit 2
RIG=/app/mocks/rig-20260922
# Camera 1 comes back as a later stretch of its own clip, 20 px across and 15 down.
/run1733/moved "$RIG/cam_1.mp4" /run1733/nudged_1.avi 20 15 0 400 400 > /dev/null || exit 2

echo "--- pairing, once, against the stub ---"
$BIN --pair 483920 --turnaus $STUB_URL --allow-plaintext > /run1733/pair.out 2> /run1733/pair.err
echo "PAIR_RC=$?"

# A single-camera run that ends in the vigil: wait for BOARD FAULTED, let it beat, stop it.
one_camera() { # $1 name, $2 clip
  local from to P
  from=$(mark)
  listen "/run1733/$1.notify"
  ( mkdir -p "/run1733/$1" && cd "/run1733/$1" && NOTIFY_SOCKET=$SOCK exec $BIN --debug --cams "$2" \
      --width 1280 --height 720 --turnaus $STUB_URL --allow-plaintext > "/run1733/$1.out" 2>&1 ) &
  P=$!
  echo "$1 reached BOARD FAULTED after $(await "/run1733/$1.out" 'BOARD FAULTED' 180)s"
  sleep 6
  stop $P; unlisten
  to=$(mark)
  plain "/run1733/$1.out" > "/run1733/$1.txt"
  echo "$from $to" > "/run1733/$1.span"
}

echo
echo "=== A. edge: camera 1 rescued by a look, then the board refused by the vote ==="
one_camera edge /run1733/off_edge.avi
if grep -qE '^\[ERROR\].*Camera 1 did not calibrate: it is not looking at a WHOLE dartboard: the coloured region that is its doubles ring comes within 0 px' /run1733/edge.txt \
   && grep -qE 'LOOK AGAIN: camera 1 seals look [0-9]+ of' /run1733/edge.txt; then
  say "OK   edge: refused on the averaged frame at 0 px, and a look sealed camera 1 -- the case this issue is about" ok
else say "FAIL edge: not refused-then-rescued, so phase A is not the case #1733 measured" no; fi
if grep -qE 'Initial calibration failed: only 1 of 1 cameras can vote' /run1733/edge.txt; then
  say "OK   edge: the gate refuses the board on the vote's arithmetic, one camera of the two it takes" ok
else say "FAIL edge: the gate did not refuse on the vote, so there is no second fault to be told" no; fi
EDGE_FAULT="$(faulted_with /run1733/edge.txt)"
echo "    BOARD FAULTED: ${EDGE_FAULT:-<nothing>}" | cut -c1-220
case "$EDGE_FAULT" in
  "only 1 of 1 cameras can vote"*) say "OK   edge: BOARD FAULTED is the gate's sentence, the count against the threshold" ok ;;
  "camera 1 did not calibrate"*)   say "FAIL edge: BOARD FAULTED names the averaged frame's refusal of a camera a look calibrated" no ;;
  *)                               say "FAIL edge: BOARD FAULTED says neither (${EDGE_FAULT:-nothing})" no ;;
esac
if grep -aqE '^STATUS=ERROR.*this board cannot see -- only 1 of 1 cameras can vote' /run1733/edge.notify; then
  say "OK   edge: the supervisor's STATUS= carries the same sentence" ok
else
  grep -a '^STATUS=ERROR' /run1733/edge.notify | head -1 | cut -c1-200
  say "FAIL edge: STATUS= does not carry the gate's sentence" no
fi
read -r EF ET < /run1733/edge.span
echo "    beats: $(beats "$EF" "$ET" | sort | uniq -c | tr '\n' ' ')"
if beats "$EF" "$ET" | grep -q '^ERROR'; then
  say "OK   edge: it beat ERROR -- this board really cannot score, so ERROR is the right word" ok
else say "FAIL edge: no ERROR beat reached the stub, so phase A says nothing about the wire" no; fi
if [ -z "$(beats "$EF" "$ET" | awk '{print $2}' | tr ',' '\n' | grep -vxE 'condition|cameras|' || true)" ]; then
  say "OK   edge: every beat body is {condition, cameras} at most -- no fault sentence crosses to Turnaus" ok
else say "FAIL edge: a beat carried a key other than condition and cameras" no; fi

echo
echo "=== B. cut: refused on the averaged frame and on every look ==="
one_camera cut /run1733/off_cut.avi
CUT_ERR="$(grep -aoE '^\[ERROR\]\[GEOMETRY_CALIBRATION\] - Camera 1 did not calibrate: .*' /run1733/cut.txt | head -1 \
  | sed -E 's/^\[ERROR\]\[GEOMETRY_CALIBRATION\] - Camera/camera/; s/\.$//')"
CUT_FAULT="$(faulted_with /run1733/cut.txt)"
echo "    BOARD FAULTED: ${CUT_FAULT:-<nothing>}" | cut -c1-220
if grep -qE 'refused on the averaged frame and on all [0-9]+ further looks' /run1733/cut.txt; then
  say "OK   cut: camera 1 was refused on every further look" ok
else say "FAIL cut: camera 1 was not refused on every look, so B is not the other half" no; fi
if [ -n "$CUT_FAULT" ] && [ "$CUT_FAULT" = "$CUT_ERR" ]; then
  say "OK   cut: BOARD FAULTED is the camera's own refusal, word for word -- the sentence it has always been" ok
else
  echo "    the camera's ERROR line: ${CUT_ERR:-<nothing>}" | cut -c1-220
  say "FAIL cut: BOARD FAULTED is not the refused camera's own sentence" no
fi
if grep -aqF "STATUS=ERROR" /run1733/cut.notify && grep -aqF "this board cannot see -- $CUT_ERR" /run1733/cut.notify; then
  say "OK   cut: and STATUS= carries it" ok
else say "FAIL cut: STATUS= does not carry the refused camera's sentence" no; fi

echo
echo "=== C. rig-20260922: camera 1 rescued, the board scores, then camera 1 is moved ==="
mkdir -p /run1733/rig
cp "$RIG/cam_1.mp4" /run1733/rig/slot_1.mp4
CF=$(mark)
listen /run1733/rig.notify
( cd /run1733/rig && NOTIFY_SOCKET=$SOCK OD_MAX_CYCLES=100000 OD_BLIND_AFTER=400 OD_BLIND_FOR_MS=12000 exec $BIN --debug \
    --cams "/run1733/rig/slot_1.mp4,$RIG/cam_2.mp4,$RIG/cam_3.mp4" \
    --width 1280 --height 720 --turnaus $STUB_URL --allow-plaintext > /run1733/rig.out 2>&1 ) &
P=$!
echo "rig sealed after $(await /run1733/rig.out 'GEOMETRY SEALED' 300)s"
echo "rig scoring after $(await /run1733/rig.out 'Scorer running with' 60)s"
# Let it beat while it scores, so the READY half is on the wire before the blind.
sleep 5
CM=$(mark)
echo "rig blinded after $(await /run1733/rig.out 'BLINDING' 240)s"
# A rename: the running process keeps its handle; only the reopen meets the new file.
cp /run1733/nudged_1.avi /run1733/rig/swap_1.mp4 && mv /run1733/rig/swap_1.mp4 /run1733/rig/slot_1.mp4
echo "rig reached its verdict after $(await /run1733/rig.out 'BOARD FAULTED|BOARD SETTLED|BOARD RECOVERED' 240)s"
sleep 6
stop $P; unlisten
CT=$(mark)
plain /run1733/rig.out > /run1733/rig.txt
grep -aE 'LOOK AGAIN: camera [0-9] seals|CAMERAS:|Scorer running with|BLINDING|GEOMETRY REVIEW|BOARD MOVED|BOARD SETTLED|BOARD RECOVERED' /run1733/rig.txt | cut -c1-200 | head -12

if grep -qE 'LOOK AGAIN: camera 1 seals look' /run1733/rig.txt && grep -q 'CAMERAS: 3 of 3 are looking at the dartboard' /run1733/rig.txt; then
  say "OK   rig: camera 1 was rescued by a look and the board sealed 3 of 3" ok
else say "FAIL rig: camera 1 was not rescued into a 3-of-3 board, so phase C is not the venue's case" no; fi
echo "    beats while scoring: $(beats "$CF" "$CM" | sort | uniq -c | tr '\n' ' ')"
if beats "$CF" "$CM" | grep -q '^READY' && ! beats "$CF" "$CM" | grep -q '^ERROR'; then
  say "OK   rig: the rescued board beat READY and never ERROR while it scored -- a rescued camera does not reach the beat" ok
else say "FAIL rig: the rescued board did not beat READY alone while scoring" no; fi
if grep -aq 'BOARD MOVED' /run1733/rig.txt; then
  say "OK   rig: the nudge is refused -- BOARD MOVED" ok
else say "FAIL rig: the nudge was not refused, so there is no later fault to be told" no; fi
RIG_FAULT="$(faulted_with /run1733/rig.txt)"
echo "    BOARD FAULTED: ${RIG_FAULT:-<nothing>}" | cut -c1-220
case "$RIG_FAULT" in
  "the cameras came back and the board is not where it was"*)
    say "OK   rig: BOARD FAULTED names the move" ok ;;
  "camera 1 did not calibrate"*)
    say "FAIL rig: BOARD FAULTED names the averaged frame camera 1 was refused on before the look that scored all evening" no ;;
  *) say "FAIL rig: BOARD FAULTED says neither (${RIG_FAULT:-nothing})" no ;;
esac
if grep -aqE '^STATUS=ERROR.*this board cannot see -- the cameras came back and the board is not where it was' /run1733/rig.notify; then
  say "OK   rig: STATUS= names the move too" ok
else
  grep -a '^STATUS=ERROR' /run1733/rig.notify | head -1 | cut -c1-200
  say "FAIL rig: STATUS= does not name the move" no
fi
if [ -z "$(beats "$CF" "$CT" | awk '{print $2}' | tr ',' '\n' | grep -vxE 'condition|cameras|' || true)" ]; then
  say "OK   rig: no beat carried anything but condition and cameras" ok
else say "FAIL rig: a beat carried a key other than condition and cameras" no; fi

kill $STUB 2>/dev/null; wait $STUB 2>/dev/null

echo
echo "=== the verdict ==="
if [ "$FAILED" = 0 ]; then echo "1733-rescued: PASS"; else echo "1733-rescued: FAIL"; fi
exit $FAILED
