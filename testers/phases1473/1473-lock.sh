#!/bin/bash
# #1473: do two boards on one host both serve 13520?
#
# Six starts in one container, against the binary at $BIN. Every phase starts a REAL
# second process, because the thing under test is what two processes do to one port and
# no amount of reading socket options answers that.
#
#   1  #1295's REFUSAL, POSITIVE CONTROL. Something that is not a board holds 13520, so
#      the board's listen() fails. It must still take the lock -- nothing else is holding
#      one -- must not be announced, must say it could not listen, and must not claim to
#      be listening. This is the other-program case #1295 fixed, and #1473 must not have
#      taken it away.
#   2  TWO BOARDS. The first is announced and listening. The second is started, and
#      declines: exit 1, before it opens a camera, naming the port, naming the pid of the
#      board that really is running, and saying what to do. Exactly ONE listening socket
#      is left on 13520 and the announcement still names the first board.
#   3  THE LOCK IS RELEASED WITH THE BOARD. The first board is gone, so a third start
#      succeeds and is announced. A lock nothing can clear is a board nobody can restart.
#   4  THE DEFECT, ON THIS BINARY. OD_ONE_BOARD=off is one_board.hpp's falsifier: the same
#      binary, the lock not taken. Two boards started that way BOTH listen on 13520 -- two
#      listening sockets, both announced, nothing logged wrong -- which is the issue,
#      reproduced here rather than argued from httplib's defaults.
#
# Sockets are counted out of /proc, not inferred from a log line: "two boards both bound
# it" is a statement about the kernel's tables. portlisten() is #1295's portstate() one
# step over, and locksockets() is the same shape for the abstract lock, which lives in
# /proc/net/unix and in no filesystem.
#
# The probe closes with a RST (SO_LINGER 0) and the squatter closes last, because httplib
# asks for SO_REUSEPORT and NOT SO_REUSEADDR: the board cannot bind over a socket in
# TIME_WAIT, so a probe that left one would make the next phase fail in exactly the way
# the defect does. That is #1295's measurement, inherited whole.
#
# It ends on an exit status, not on an echo (#1463).
set -u
BIN="${BIN:-/app/build/opendartboard}"
ANN=/run1473/announce
SVC="$ANN/opendartboard.service"
NOPUSH="--turnaus http://127.0.0.1:9 --allow-plaintext"
MOCKS="--cams /app/mocks/cam_1.mp4,/app/mocks/cam_2.mp4,/app/mocks/cam_3.mp4 --width 1280 --height 720"
PORT=13520
LOCK="@opendartboard-score-$PORT"
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
# Whether a pid has finished, read out of its own /proc entry rather than matched against
# a process pattern. A child this shell has not reaped yet is a zombie, and a zombie
# answers `kill -0` as a living process -- which is exactly the pid being asked about, so
# the state letter is what has to be read.
stopped() { # stopped <pid>
  local st
  st="$(sed 's/.*) //' "/proc/$1/stat" 2> /dev/null | cut -d' ' -f1)"
  if [ -z "$st" ] || [ "$st" = "Z" ]; then echo yes; else echo no; fi
}
announcedAs() { sed -n 's:.*<name>\(.*\)</name>.*:\1:p' "$SVC" 2> /dev/null | head -1; }
plain() { sed -E 's/\x1b\[[0-9;]*m//g' "$1" 2> /dev/null; }
logsays() { # logsays <file> <ERE>
  if plain "$1" | grep -qaE "$2"; then echo yes; else echo no; fi
}

# #1295's probe, unchanged: who is on the port -- nobody, the squatter, or something
# speaking HTTP. It closes with a RST so it never leaves a TIME_WAIT socket on 13520.
cat > /run1473/who.py <<'PY'
import socket, struct, sys
s = socket.socket(); s.settimeout(3)
s.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack('ii', 1, 0))
try:
    s.connect(('127.0.0.1', 13520))
except OSError as e:
    print('nobody(errno=%d)' % e.errno); sys.exit(0)
try:
    s.settimeout(1.0)
    first = s.recv(64)
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

# #1295's portstate(): every socket the kernel holds on 13520, by state. A precondition
# rather than a curiosity -- the board binds with SO_REUSEPORT and without SO_REUSEADDR,
# so ANY socket lingering on that port stops it, and the board then fails exactly as it
# does when the port is really taken.
cat > /run1473/portstate.py <<'PY'
want = '%04X' % 13520
names = {'01': 'ESTABLISHED', '06': 'TIME_WAIT', '0A': 'LISTEN', '08': 'CLOSE_WAIT'}
seen = []
for f in ('/proc/net/tcp', '/proc/net/tcp6'):
    try:
        rows = open(f).read().splitlines()[1:]
    except OSError:
        continue
    for row in rows:
        p = row.split()
        if p[1].split(':')[1] == want or p[2].split(':')[1] == want:
            seen.append(names.get(p[3], p[3]))
print(','.join(sorted(set(seen))) or 'none')
PY

# HOW MANY sockets are LISTENING on 13520, which is the whole question. One is a host
# running one board; two is SO_REUSEPORT sharing the darts between them.
cat > /run1473/portlisten.py <<'PY'
want = '%04X' % 13520
n = 0
for f in ('/proc/net/tcp', '/proc/net/tcp6'):
    try:
        rows = open(f).read().splitlines()[1:]
    except OSError:
        continue
    for row in rows:
        p = row.split()
        if p[1].split(':')[1] == want and p[3] == '0A':
            n += 1
print(n)
PY

# The same count for the lock itself. An abstract socket has no filesystem entry, so
# /proc/net/unix is the only place it can be seen; SO_ACCEPTCON (flag 0x10000) is what
# tells the held lock from the connections a refused board's probe left in its backlog.
cat > /run1473/locksockets.py <<'PY'
import sys
want = sys.argv[1]
n = 0
try:
    rows = open('/proc/net/unix').read().splitlines()[1:]
except OSError:
    rows = []
for row in rows:
    p = row.split()
    if len(p) >= 8 and p[7] == want and (int(p[3], 16) & 0x10000):
        n += 1
print(n)
PY

# #1295's squatter: SO_REUSEADDR only, and deliberately NOT SO_REUSEPORT, so the board's
# bind fails whatever httplib asks for. It lets the PROBE close first and closes on what
# that leaves behind, so nothing of its own sits in TIME_WAIT on 13520.
cat > /run1473/squat.py <<'PY'
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
        c.sendall(b'SQUATTER-1473\n')
    except OSError:
        pass
    try:
        c.settimeout(3)
        c.recv(16)
    except OSError:
        pass
    c.close()
PY

who() { python3 /run1473/who.py; }
portstate() { python3 /run1473/portstate.py; }
portlisten() { python3 /run1473/portlisten.py; }
locksockets() { python3 /run1473/locksockets.py "$LOCK"; }

# Wait for a sentinel the detector itself prints, never for a process pattern. The needle
# matches BOTH outcomes of the decision under test, so a board that announces when it
# should not fails an assertion instead of timing out.
await() { # await <file> <ERE> <seconds>
  local file="$1" needle="$2" limit="$3" i=0
  while [ "$i" -lt "$limit" ]; do
    plain "$file" | grep -qaE "$needle" && { echo "$i"; return 0; }
    i=$((i + 1)); sleep 1
  done
  echo "TIMEOUT-$limit"; return 1
}
DECIDED='announced as|not announced'

echo "BIN=$BIN"
"$BIN" --version | head -2

# =====================================================================================
echo "=== phase 1: another PROGRAM holds the port -- #1295's refusal (positive control) ==="
python3 /run1473/squat.py > /run1473/squat.out 2> /run1473/squat.err &
SQUAT=$!
if ! await /run1473/squat.out 'SQUATTING' 15 > /dev/null; then
  echo "FAIL the squatter could not take $PORT, so this phase measures nothing:"
  sed 's/^/    /' /run1473/squat.err
  kill $SQUAT 2> /dev/null
  exit 3
fi
check "p1 who holds the port before the board starts"    "squatter" "$(who)"
check "p1 the only socket on the port is a listening one" "LISTEN"  "$(portstate)"
check "p1 nobody holds the board lock"                    "0"       "$(locksockets)"

printf 'stale announcement left by an earlier --listen run\n' > "$SVC"
check "p1 stale announcement planted" "yes" "$(announced)"

OD_MAX_CYCLES=400 "$BIN" --debug $MOCKS --listen --announce-dir "$ANN" --label "Kello 1473 A" $NOPUSH \
  > /run1473/p1.out 2>&1 &
P1=$!
echo "p1 the board decided after $(await /run1473/p1.out "$DECIDED" 300)s"
sleep 2
check "p1 the board took the lock"                        "yes" "$(logsays /run1473/p1.out 'one board per host: holding the abstract lock')"
check "p1 the lock is held once"                          "1"   "$(locksockets)"
check "p1 announced while the socket is not the board's"  "no"  "$(announced)"
check "p1 the log says it is not announced"               "yes" "$(logsays /run1473/p1.out 'not announced')"
check "p1 the log says the socket could not listen"       "yes" "$(logsays /run1473/p1.out 'could not listen')"
check "p1 the log does NOT claim to be listening"         "no"  "$(logsays /run1473/p1.out 'server listening on')"
check "p1 the log does NOT refuse a second board"         "no"  "$(logsays /run1473/p1.out 'not starting: another opendartboard')"
check "p1 who holds the port while the board runs"        "squatter" "$(who)"
kill -TERM $P1 2> /dev/null
wait $P1 2> /dev/null
echo "P1_RC=$?"
kill $SQUAT 2> /dev/null
wait $SQUAT 2> /dev/null
check "p1 the lock is released with the board" "0" "$(locksockets)"
echo "p1 what the log says about the announcement and the socket:"
plain /run1473/p1.out | grep -aE 'one board|announce|listen' | sed 's/^/    /'

# =====================================================================================
echo "=== phase 2: another BOARD -- the second one declines ==="
rm -f "$SVC"
check "p2 nothing at all is holding the port" "none" "$(portstate)"
check "p2 nobody holds the board lock"        "0"    "$(locksockets)"

OD_MAX_CYCLES=900 "$BIN" --debug $MOCKS --listen --announce-dir "$ANN" --label "Kello 1473 FIRST" $NOPUSH \
  > /run1473/first.out 2>&1 &
FIRST=$!
echo "p2 the first board decided after $(await /run1473/first.out "$DECIDED" 300)s"
sleep 2
check "p2 the first board is announced"        "yes"   "$(announced)"
check "p2 the announcement names the first board" "Kello 1473 FIRST" "$(announcedAs)"
check "p2 who holds the port"                  "http"  "$(who)"
check "p2 listening sockets on the port"       "1"     "$(portlisten)"
check "p2 the lock is held once"               "1"     "$(locksockets)"

# The real second process. It is started in the BACKGROUND and measured while it is at
# its liveliest, because the question is what the kernel holds WHILE two boards are up:
# run in the foreground it would be gone before anything could be counted, and a tree
# where it does not decline would be counted after it had finished too, so the headline
# number would read 0 for two opposite reasons.
#
# The sentinel matches BOTH outcomes of the decision under test -- refused, or listening --
# so a board that does not decline fails an assertion rather than spending this phase's
# whole budget on a timeout.
SECOND_T0=$(date +%s)
OD_MAX_CYCLES=900 "$BIN" --debug $MOCKS --listen --announce-dir "$ANN" --label "Kello 1473 SECOND" $NOPUSH \
  > /run1473/second.out 2>&1 &
SECOND=$!
echo "p2 the second board decided after $(await /run1473/second.out 'not starting: another opendartboard|server listening on' 300)s"
check "p2 it declined before it opened a camera"            "no" "$(logsays /run1473/second.out '^Configuration:')"
check "p2 it says it is not starting, and why"              "yes" \
  "$(logsays /run1473/second.out 'not starting: another opendartboard .* is already running on this host and holds the score port 13520')"
check "p2 it says what would happen if it did"              "yes" \
  "$(logsays /run1473/second.out 'darts would be split between them')"
check "p2 it says what to do about it"                      "yes" \
  "$(logsays /run1473/second.out "one host runs one board: stop it first")"
# The pid in the refusal is read from SO_PEERCRED on the lock itself, so it is a claim
# about WHICH process holds the port, not a number that happens to be printed.
NAMED_PID="$(plain /run1473/second.out | sed -n 's/.*another opendartboard (pid \([0-9]*\)).*/\1/p' | head -1)"
check "p2 the pid it names is the board that is running"    "$FIRST" "${NAMED_PID:-none}"
check "p2 it did NOT claim to be listening"                 "no"  "$(logsays /run1473/second.out 'server listening on')"
check "p2 it did NOT announce itself"                       "no"  "$(logsays /run1473/second.out 'announced as')"

# What the issue is about, measured in the kernel's own tables rather than in a log, and
# measured with the second board started.
check "p2 listening sockets on the port, with the second board started" "1" "$(portlisten)"
check "p2 the announcement still names the first board" "Kello 1473 FIRST" "$(announcedAs)"
check "p2 who holds the port now"                       "http" "$(who)"
check "p2 the lock is still held once"                  "1"    "$(locksockets)"

# And it has to go away by itself. Nothing here waits on a process pattern: the pid this
# shell started is the pid it asks about.
GONE=no
for _ in $(seq 1 25); do
  [ "$(stopped $SECOND)" = yes ] && { GONE=yes; break; }
  sleep 1
done
check "p2 the second board stopped on its own" "yes" "$GONE"
if [ "$GONE" = yes ]; then
  wait $SECOND 2> /dev/null
  SECOND_RC=$?
else
  kill -TERM $SECOND 2> /dev/null
  wait $SECOND 2> /dev/null
  SECOND_RC="still-running"
fi
echo "p2 the second board was done after $(( $(date +%s) - SECOND_T0 ))s"
check "p2 the second board's exit status" "1" "$SECOND_RC"
echo "p2 what the second board said:"
plain /run1473/second.out | grep -aE 'not starting|one host runs' | sed 's/^/    /'

kill -TERM $FIRST 2> /dev/null
wait $FIRST 2> /dev/null
echo "FIRST_RC=$?"

# =====================================================================================
echo "=== phase 3: the lock is released with the board that held it ==="
check "p3 nobody holds the board lock now" "0"    "$(locksockets)"
check "p3 nothing is holding the port"     "none" "$(portstate)"
check "p3 the first board withdrew its announcement" "no" "$(announced)"

OD_MAX_CYCLES=400 "$BIN" --debug $MOCKS --listen --announce-dir "$ANN" --label "Kello 1473 THIRD" $NOPUSH \
  > /run1473/third.out 2>&1 &
THIRD=$!
echo "p3 the third board decided after $(await /run1473/third.out "$DECIDED" 300)s"
sleep 2
check "p3 a later board takes the lock"      "yes"  "$(logsays /run1473/third.out 'one board per host: holding the abstract lock')"
check "p3 it is not refused by a stale lock" "no"   "$(logsays /run1473/third.out 'not starting: another opendartboard')"
check "p3 it is announced"                   "Kello 1473 THIRD" "$(announcedAs)"
check "p3 who holds the port"                "http" "$(who)"
check "p3 listening sockets on the port"     "1"    "$(portlisten)"
kill -TERM $THIRD 2> /dev/null
wait $THIRD 2> /dev/null
echo "THIRD_RC=$?"

# =====================================================================================
echo "=== phase 4: the defect, on this same binary (OD_ONE_BOARD=off) ==="
# one_board.hpp's falsifier. Without it this tester could not tell a fix from a box on
# which two boards never happened to overlap; with it, the defect is a measurement.
rm -f "$SVC"
check "p4 nothing is holding the port" "none" "$(portstate)"
check "p4 nobody holds the board lock" "0"    "$(locksockets)"

OD_ONE_BOARD=off OD_MAX_CYCLES=900 "$BIN" --debug $MOCKS --listen --announce-dir "$ANN" --label "Kello 1473 D" $NOPUSH \
  > /run1473/d.out 2>&1 &
D=$!
echo "p4 D decided after $(await /run1473/d.out "$DECIDED" 300)s"
check "p4 D took no lock"                "yes" "$(logsays /run1473/d.out 'OD_ONE_BOARD=off: no lock was taken')"
check "p4 nobody holds the board lock"   "0"   "$(locksockets)"

OD_ONE_BOARD=off OD_MAX_CYCLES=900 "$BIN" --debug $MOCKS --listen --announce-dir "$ANN" --label "Kello 1473 E" $NOPUSH \
  > /run1473/e.out 2>&1 &
E=$!
echo "p4 E decided after $(await /run1473/e.out "$DECIDED" 300)s"
sleep 2
check "p4 E was NOT refused"                    "no"  "$(logsays /run1473/e.out 'not starting: another opendartboard')"
check "p4 E claims to be listening too"         "yes" "$(logsays /run1473/e.out 'server listening on')"
check "p4 TWO boards are listening on the port" "2"   "$(portlisten)"
check "p4 and the announcement now names the second of them" "Kello 1473 E" "$(announcedAs)"
kill -TERM $D $E 2> /dev/null
wait $D 2> /dev/null; wait $E 2> /dev/null

echo "---"
if [ "$FAILS" -eq 0 ]; then
  echo "1473: every check passed"
else
  echo "1473: $FAILS check(s) failed"
fi
exit "$FAILS"
