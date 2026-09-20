#!/bin/bash
# #1295: is a board announced when its score socket never opened?
#
# Two starts in one container, against the binary at $BIN (the changed build by default;
# a baseline build of fork main is mounted at /app the same way, so the same script
# measures both). Each start is given --announce-dir inside the run directory, so the
# announcement is a file this harness can read without Avahi and without root; #1189's
# check_announce.py is what proves the responder publishes such a file, and that is not
# what is in question here.
#
#   1  REFUSAL. Something else is already bound to 13520, so the board's listen() fails.
#      The board can see -- #1274's question answers yes -- and it must still not be
#      announced, a stale file from an earlier --listen run must be gone, the log must not
#      claim to be listening, and what answers on 13520 must still be the squatter.
#   2  POSITIVE CONTROL. Nothing on the port. The same board, the same flags, must be
#      announced, and something must really be answering on 13520 -- and answering HTTP,
#      which the squatter does not. Not announced again after it stops.
#
# The socket is probed by CONNECTING to it, not by reading a log line: "announced" and
# "listening" are the two halves whose disagreement is the defect, so a log line cannot be
# the evidence for both. The probe goes one step further than #1274's and says WHO
# answered, because in phase 1 the port is open and the board is not the thing on it --
# a bare connect() would read as success.
#
# It ends on an exit status, not on an echo (#1463).
set -u
BIN="${BIN:-/app/build/opendartboard}"
ANN=/run1295/announce
SVC="$ANN/opendartboard.service"
NOPUSH="--turnaus http://127.0.0.1:9 --allow-plaintext"
MOCKS="--cams /app/mocks/cam_1.mp4,/app/mocks/cam_2.mp4,/app/mocks/cam_3.mp4 --width 1280 --height 720"
PORT=13520
FAILS=0
mkdir -p "$ANN"

check() { # check <what> <expected> <actual>
  if [ "$2" = "$3" ]; then
    printf 'ok   %s: %s\n' "$1" "$3"
  else
    printf 'FAIL %s: expected [%s], got [%s]\n' "$1" "$2" "$3"
    FAILS=$((FAILS + 1))
  fi
}

announced() { if [ -f "$SVC" ]; then echo yes; else echo no; fi; }
logsays() { # logsays <file> <ERE>
  if sed -E 's/\x1b\[[0-9;]*m//g' "$1" 2> /dev/null | grep -qaE "$2"; then echo yes; else echo no; fi
}

# Who is on the port: nobody, the squatter, or something speaking HTTP (the board's own
# REST listener, which the WebSocket upgrade shares).
cat > /run1295/who.py <<'PY'
import socket, sys
s = socket.socket(); s.settimeout(3)
try:
    s.connect(('127.0.0.1', 13520))
except OSError as e:
    print('nobody(errno=%d)' % e.errno); sys.exit(0)
try:
    s.settimeout(1.0)
    first = s.recv(64)          # the squatter speaks first; httplib waits to be asked
except OSError:
    first = b''
if first.startswith(b'SQUATTER'):
    print('squatter'); sys.exit(0)
try:
    s.sendall(b'GET /info HTTP/1.0\r\n\r\n')
    s.settimeout(3)
    answer = s.recv(64)
except OSError as e:
    print('open-but-mute(errno=%d)' % e.errno); sys.exit(0)
print('http' if answer.startswith(b'HTTP/') else 'other(%r)' % answer[:24])
PY

# The squatter. SO_REUSEADDR only, and deliberately NOT SO_REUSEPORT: Linux lets two
# sockets share a port only when EVERY one of them asked for SO_REUSEPORT, so without it
# here the board's bind fails whatever httplib asks for.
cat > /run1295/squat.py <<'PY'
import socket, sys
s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.bind(('0.0.0.0', 13520))
s.listen(8)
sys.stdout.write('SQUATTING\n'); sys.stdout.flush()
while True:
    try:
        c, _ = s.accept()
    except OSError:
        break
    try:
        c.sendall(b'SQUATTER-1295\n')
    except OSError:
        pass
    c.close()
PY

# Wait for a sentinel the detector itself prints, never for a process pattern. The needle
# is an ERE matching BOTH outcomes of the decision under test, so a board that announces
# when it should not fails an assertion instead of timing out.
await() { # await <file> <ERE> <seconds>
  local file="$1" needle="$2" limit="$3" i=0
  while [ "$i" -lt "$limit" ]; do
    sed -E 's/\x1b\[[0-9;]*m//g' "$file" 2> /dev/null | grep -qaE "$needle" && { echo "$i"; return 0; }
    i=$((i + 1)); sleep 1
  done
  echo "TIMEOUT-$limit"; return 1
}
DECIDED='announced as|not announced'

echo "BIN=$BIN"
"$BIN" --version | head -2

echo "=== phase 1: the port is already taken ==="
python3 /run1295/squat.py > /run1295/squat.out 2> /run1295/squat.err &
SQUAT=$!
if ! await /run1295/squat.out 'SQUATTING' 15 > /dev/null; then
  echo "FAIL the squatter could not take $PORT, so this phase measures nothing:"
  sed 's/^/    /' /run1295/squat.err
  kill $SQUAT 2> /dev/null
  exit 3
fi
check "p1 who holds the port before the board starts" "squatter" "$(python3 /run1295/who.py)"

printf 'stale announcement left by an earlier --listen run\n' > "$SVC"
check "p1 stale announcement planted" "yes" "$(announced)"

OD_MAX_CYCLES=200 "$BIN" --debug $MOCKS --listen --announce-dir "$ANN" --label "Kello 1295" $NOPUSH \
  > /run1295/taken.out 2> /run1295/taken.err &
TAKEN=$!
echo "p1 the board decided after $(await /run1295/taken.out "$DECIDED" 300)s"
sleep 2
check "p1 announced while the socket is not the board's" "no"  "$(announced)"
check "p1 the log says it is not announced"              "yes" "$(logsays /run1295/taken.out 'not announced')"
check "p1 the log says the socket could not listen"      "yes" "$(logsays /run1295/taken.out 'could not listen')"
check "p1 the log does NOT claim to be listening"        "no"  "$(logsays /run1295/taken.out 'server listening on')"
check "p1 who holds the port while the board runs"       "squatter" "$(python3 /run1295/who.py)"
kill -TERM $TAKEN 2> /dev/null
wait $TAKEN 2> /dev/null
echo "TAKEN_RC=$?"
check "p1 announced after the board stopped" "no" "$(announced)"
kill $SQUAT 2> /dev/null
wait $SQUAT 2> /dev/null
echo "p1 what the log says about the announcement and the socket:"
sed -E 's/\x1b\[[0-9;]*m//g' /run1295/taken.out | grep -aE 'announce|listen' | sed 's/^/    /'

echo "=== phase 2: the port is free (positive control) ==="
rm -f "$SVC"
check "p2 who holds the port before the board starts" "nobody(errno=111)" "$(python3 /run1295/who.py)"
OD_MAX_CYCLES=200 "$BIN" --debug $MOCKS --listen --announce-dir "$ANN" --label "Kello 1295" $NOPUSH \
  > /run1295/free.out 2> /run1295/free.err &
FREE=$!
echo "p2 the board decided after $(await /run1295/free.out "$DECIDED" 300)s"
sleep 2
check "p2 announced while the socket is open" "yes"  "$(announced)"
check "p2 the log says it is listening"       "yes"  "$(logsays /run1295/free.out 'server listening on')"
check "p2 who holds the port while the board runs" "http" "$(python3 /run1295/who.py)"
echo "p2 the announcement, in full:"
sed 's/^/    /' "$SVC" 2> /dev/null
wait $FREE 2> /dev/null
echo "FREE_RC=$?"
check "p2 announced after the board stopped" "no" "$(announced)"
echo "p2 what the log says about the announcement and the socket:"
sed -E 's/\x1b\[[0-9;]*m//g' /run1295/free.out | grep -aE 'announce|listen' | sed 's/^/    /'

echo "---"
if [ "$FAILS" -eq 0 ]; then
  echo "1295: every check passed"
else
  echo "1295: $FAILS check(s) failed"
fi
exit "$FAILS"
