#!/bin/bash
# #1274: does the announcement outlive - or precede - the score socket?
#
# Three starts in one container, against the binary at $BIN (the changed build by default;
# the baseline build of fork main is mounted at /app the same way, so the same script
# measures both). Each start is given --announce-dir inside the run directory, so the
# announcement is a file this harness can read without Avahi and without root; #1189's
# check_announce.py is what proves the responder publishes such a file, and that is not
# what is in question here.
#
#   1  a dark board (#895's three cameras that will not open) started with --listen.
#      It must not be announced, a stale file from an earlier --listen run must be gone,
#      and nothing must be listening on the score socket's port either way.
#   2  a healthy board started with --listen. It must be announced while it runs, with
#      something really listening, and not announced after it stops.
#   3  a loopback start with a stale announcement planted in front of it. The file must
#      be gone and the log must say it was removed.
#
# The socket is probed by connecting to it, not by reading a log line: "announced" and
# "listening" are the two halves whose disagreement is the defect.
set -u
BIN="${BIN:-/app/build/opendartboard}"
ANN=/run1274/announce
SVC="$ANN/opendartboard.service"
NOPUSH="--turnaus http://127.0.0.1:9 --allow-plaintext"
mkdir -p "$ANN"

announced() {
  if [ -f "$SVC" ]; then echo "yes ($(wc -c < "$SVC") bytes)"; else echo "no"; fi
}
listening() {
  python3 -c "
import socket
s = socket.socket(); s.settimeout(2)
rc = s.connect_ex(('127.0.0.1', 13520)); s.close()
print('yes' if rc == 0 else 'no (connect_ex=%d)' % rc)"
}
# Wait for a sentinel the detector itself prints, never for a process pattern.
await() {
  local file="$1" needle="$2" limit="$3" i=0
  while [ $i -lt "$limit" ]; do
    grep -qa "$needle" "$file" 2>/dev/null && { echo "$i"; return 0; }
    i=$((i + 1)); sleep 1
  done
  echo "TIMEOUT-$limit"; return 1
}

echo "BIN=$BIN"
"$BIN" --version | head -2

echo "=== phase 1: a dark board with --listen ==="
cp /run1274/still.jpg /run1274/c1.jpg
cp /run1274/still.jpg /run1274/c2.jpg
cp /run1274/still.jpg /run1274/c3.jpg
printf 'stale announcement left by an earlier --listen run\n' > "$SVC"
echo "p1 planted stale announcement: $(announced)"
"$BIN" --debug --cams /run1274/c1.jpg,/run1274/c2.jpg,/run1274/c3.jpg \
  --width 1280 --height 720 --listen --announce-dir "$ANN" --label "Kello 1274" $NOPUSH \
  > /run1274/dark.out 2> /run1274/dark.err &
DARK=$!
echo "p1 vigil reached after $(await /run1274/dark.out 'BOARD FAULTED' 60)s"
sleep 3
echo "p1 announced while faulted: $(announced)"
echo "p1 score socket listening:  $(listening)"
kill -TERM $DARK 2>/dev/null
wait $DARK 2>/dev/null
echo "DARK_RC=$?"
echo "p1 announced after stop:    $(announced)"
echo "p1 what the log says about the announcement:"
sed -E 's/\x1b\[[0-9;]*m//g' /run1274/dark.out | grep -a 'announce' | sed 's/^/    /'

echo "=== phase 2: a healthy board with --listen ==="
rm -f "$SVC"
OD_MAX_CYCLES=200 "$BIN" --debug \
  --cams /app/mocks/cam_1.mp4,/app/mocks/cam_2.mp4,/app/mocks/cam_3.mp4 \
  --width 1280 --height 720 --listen --announce-dir "$ANN" --label "Kello 1274" $NOPUSH \
  > /run1274/healthy.out 2> /run1274/healthy.err &
HEALTHY=$!
echo "p2 socket open after $(await /run1274/healthy.out 'WebSocket server listening' 120)s"
sleep 2
echo "p2 announced while scoring: $(announced)"
echo "p2 score socket listening:  $(listening)"
echo "p2 the announcement, in full:"
sed 's/^/    /' "$SVC" 2>/dev/null
wait $HEALTHY 2>/dev/null
echo "HEALTHY_RC=$?"
echo "p2 announced after stop:    $(announced)"
echo "p2 what the log says about the announcement:"
sed -E 's/\x1b\[[0-9;]*m//g' /run1274/healthy.out | grep -a 'announce' | sed 's/^/    /'

echo "=== phase 3: a loopback start in front of a stale announcement ==="
printf 'stale announcement left by an earlier --listen run\n' > "$SVC"
echo "p3 planted stale announcement: $(announced)"
OD_MAX_CYCLES=60 "$BIN" --debug \
  --cams /app/mocks/cam_1.mp4,/app/mocks/cam_2.mp4,/app/mocks/cam_3.mp4 \
  --width 1280 --height 720 --announce-dir "$ANN" --label "Kello 1274" $NOPUSH \
  > /run1274/loopback.out 2> /run1274/loopback.err
echo "LOOPBACK_RC=$?"
echo "p3 announced after the start: $(announced)"
echo "p3 what the log says about the announcement:"
sed -E 's/\x1b\[[0-9;]*m//g' /run1274/loopback.out | grep -a 'announce' | sed 's/^/    /'

echo "PHASES_DONE"
