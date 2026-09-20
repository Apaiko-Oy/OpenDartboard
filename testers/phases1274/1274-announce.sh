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
#
# WHAT THIS SCRIPT'S EXIT STATUS CARRIES, which is #1463's standard: the count of the
# musts above that did not hold. Until #1479 it carried nothing -- its last line was
# `echo "PHASES_DONE"`, and an echo returns 0 whatever it printed, so i1274_run.sh exited
# 0 and 1274-announce could not go red however the three starts behaved. That is the
# defect #1412 fixed one directory over. The three phases already SAID what must hold, in
# the list above; what #1479 changed is that each one is now asked rather than printed,
# and nothing new is asserted. Every recording line the phases printed before is still
# printed, because what a reader wants when one goes red is the transcript.
#
# Run against the BASELINE tree, this is expected to be red: a board started with --listen
# before #1274 announced itself above the Scorer, so phase 1's board advertises a socket
# it never opens. That is the before/after the harness exists for, not a defect in it.
set -u
BIN="${BIN:-/app/build/opendartboard}"
ANN=/run1274/announce
SVC="$ANN/opendartboard.service"
NOPUSH="--turnaus http://127.0.0.1:9 --allow-plaintext"
mkdir -p "$ANN"

FAILED=0
say() { echo "$1"; [ "$2" = ok ] || FAILED=1; }

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
# "the announcement is gone" is only a finding where one could have been found, so each
# phase that asserts an absence plants the file first and this says the plant took. #708's
# shape: a needle proved present before its absence is allowed to mean anything.
planted() {
  case "$1" in
    yes*) say "OK   $2" ok ;;
    *)    say "FAIL $3" no ;;
  esac
}

echo "BIN=$BIN"
"$BIN" --version | head -2

echo "=== phase 1: a dark board with --listen ==="
cp /run1274/still.jpg /run1274/c1.jpg
cp /run1274/still.jpg /run1274/c2.jpg
cp /run1274/still.jpg /run1274/c3.jpg
printf 'stale announcement left by an earlier --listen run\n' > "$SVC"
P1_PLANT="$(announced)"
echo "p1 planted stale announcement: $P1_PLANT"
planted "$P1_PLANT" \
  "p1 the stale announcement really is on disk before the dark start" \
  "p1 no stale announcement was planted, so its absence below proves nothing"
"$BIN" --debug --cams /run1274/c1.jpg,/run1274/c2.jpg,/run1274/c3.jpg \
  --width 1280 --height 720 --listen --announce-dir "$ANN" --label "Kello 1274" $NOPUSH \
  > /run1274/dark.out 2> /run1274/dark.err &
DARK=$!
P1_VIGIL="$(await /run1274/dark.out 'BOARD FAULTED' 60)"
echo "p1 vigil reached after ${P1_VIGIL}s"
case "$P1_VIGIL" in
  TIMEOUT-*) say "FAIL p1 the dark board never reached the fault vigil, so the three answers below are about a board that was still starting" no ;;
  *)         say "OK   p1 the dark board reached #895's fault vigil" ok ;;
esac
sleep 3
P1_ANN="$(announced)"
P1_LISTEN="$(listening)"
echo "p1 announced while faulted: $P1_ANN"
echo "p1 score socket listening:  $P1_LISTEN"
case "$P1_ANN" in
  no) say "OK   p1 a board that cannot see is not announced, and the stale file is gone" ok ;;
  *)  say "FAIL p1 a board that cannot see is announced ($P1_ANN)" no ;;
esac
case "$P1_LISTEN" in
  yes) say "FAIL p1 something is listening on the score socket of a board that cannot see" no ;;
  *)   say "OK   p1 nothing is listening on the score socket" ok ;;
esac
kill -TERM $DARK 2>/dev/null
wait $DARK 2>/dev/null
echo "DARK_RC=$?"
P1_AFTER="$(announced)"
echo "p1 announced after stop:    $P1_AFTER"
case "$P1_AFTER" in
  no) say "OK   p1 still not announced after the dark board stops" ok ;;
  *)  say "FAIL p1 the dark board left an announcement behind ($P1_AFTER)" no ;;
esac
echo "p1 what the log says about the announcement:"
sed -E 's/\x1b\[[0-9;]*m//g' /run1274/dark.out | grep -a 'announce' | sed 's/^/    /'
# One line rather than two greps: "removed" on its own matches most of what a detector
# says about anything, and two independent matches are two facts that may belong to
# different lines (#1450 met exactly that).
if grep -qa 'not announced: this board cannot see.*removed' /run1274/dark.out; then
  say "OK   p1 the log says why it is not announced and that it removed the stale file" ok
else
  say "FAIL p1 the log does not say the stale announcement was removed and why" no
fi

echo "=== phase 2: a healthy board with --listen ==="
rm -f "$SVC"
OD_MAX_CYCLES=200 "$BIN" --debug \
  --cams /app/mocks/cam_1.mp4,/app/mocks/cam_2.mp4,/app/mocks/cam_3.mp4 \
  --width 1280 --height 720 --listen --announce-dir "$ANN" --label "Kello 1274" $NOPUSH \
  > /run1274/healthy.out 2> /run1274/healthy.err &
HEALTHY=$!
P2_OPEN="$(await /run1274/healthy.out 'WebSocket server listening' 120)"
echo "p2 socket open after ${P2_OPEN}s"
case "$P2_OPEN" in
  TIMEOUT-*) say "FAIL p2 the healthy board never said it opened the score socket" no ;;
  *)         say "OK   p2 the healthy board opened the score socket" ok ;;
esac
sleep 2
P2_ANN="$(announced)"
P2_LISTEN="$(listening)"
echo "p2 announced while scoring: $P2_ANN"
echo "p2 score socket listening:  $P2_LISTEN"
case "$P2_ANN" in
  yes*) say "OK   p2 a board that can see is announced while it runs" ok ;;
  *)    say "FAIL p2 a board that can see is not announced while it runs" no ;;
esac
case "$P2_LISTEN" in
  yes) say "OK   p2 and something really is listening behind the announcement" ok ;;
  *)   say "FAIL p2 the board is announced with nothing listening ($P2_LISTEN) -- #1274's defect" no ;;
esac
echo "p2 the announcement, in full:"
sed 's/^/    /' "$SVC" 2>/dev/null
wait $HEALTHY 2>/dev/null
echo "HEALTHY_RC=$?"
P2_AFTER="$(announced)"
echo "p2 announced after stop:    $P2_AFTER"
case "$P2_AFTER" in
  no) say "OK   p2 the announcement does not outlive the socket" ok ;;
  *)  say "FAIL p2 the announcement outlived the board that stopped ($P2_AFTER)" no ;;
esac
echo "p2 what the log says about the announcement:"
sed -E 's/\x1b\[[0-9;]*m//g' /run1274/healthy.out | grep -a 'announce' | sed 's/^/    /'
if grep -qa 'announcement withdrawn: removed' /run1274/healthy.out; then
  say "OK   p2 the log says the announcement was withdrawn" ok
else
  say "FAIL p2 the log does not say the announcement was withdrawn" no
fi

echo "=== phase 3: a loopback start in front of a stale announcement ==="
printf 'stale announcement left by an earlier --listen run\n' > "$SVC"
P3_PLANT="$(announced)"
echo "p3 planted stale announcement: $P3_PLANT"
planted "$P3_PLANT" \
  "p3 the stale announcement really is on disk before the loopback start" \
  "p3 no stale announcement was planted, so its absence below proves nothing"
OD_MAX_CYCLES=60 "$BIN" --debug \
  --cams /app/mocks/cam_1.mp4,/app/mocks/cam_2.mp4,/app/mocks/cam_3.mp4 \
  --width 1280 --height 720 --announce-dir "$ANN" --label "Kello 1274" $NOPUSH \
  > /run1274/loopback.out 2> /run1274/loopback.err
echo "LOOPBACK_RC=$?"
P3_AFTER="$(announced)"
echo "p3 announced after the start: $P3_AFTER"
case "$P3_AFTER" in
  no) say "OK   p3 a loopback start withdraws the stale announcement" ok ;;
  *)  say "FAIL p3 the stale announcement survived a loopback start ($P3_AFTER)" no ;;
esac
echo "p3 what the log says about the announcement:"
sed -E 's/\x1b\[[0-9;]*m//g' /run1274/loopback.out | grep -a 'announce' | sed 's/^/    /'
if grep -qa 'not announced: loopback only.*removed' /run1274/loopback.out; then
  say "OK   p3 the log says it is loopback only and that it removed the file" ok
else
  say "FAIL p3 the log does not say the stale announcement was removed and why" no
fi

echo "PHASES_DONE"
# The phase must exit on what it measured. i1274_run.sh exits on the container's status and
# run_all.sh reads that and nothing else, so ending on the echo above -- which returns 0
# whatever it printed -- left 1274-announce unable to go red (#1479, the repair #1412 made
# in phases1247/).
echo "CHECK_RC=$FAILED"
exit $FAILED
