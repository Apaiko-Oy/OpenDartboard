#!/bin/bash
# #1383, inside the container. Two halves that never touch each other:
#
#   A. A BLIND BOARD GIVEN A CYCLE BUDGET ENDS. A tester asking for a run with a known
#      end gets one whether or not the board can see, and the status it exits with says
#      which of the two it got.
#   B. A BLIND BOARD SAYS SO WHERE A SUPERVISOR CAN READ IT, and a board that CAN see
#      says something different, so the two can be told apart without opening a log.
#
# WHAT MUST NOT MOVE, and it is asserted before either half: #895's fault vigil. A blind
# board that was given no budget goes on waiting for a camera, for ever. Every phase
# below that ends a blind run ends one that was handed a number.
#
# THE FIXTURE IS PROVED BLIND BEFORE ANYTHING IS CONCLUDED FROM IT (phase 1). Three
# copies of one still JPEG where three cameras should be -- #892's fixture, the one that
# measured the SIGSEGV this path used to end in. A phase that asserts "a blind board does
# X" on a board that was quietly scoring asserts nothing, and the control is in this file
# rather than in a comment: no SCORE line, no `Scorer running with`, and BOARD FAULTED
# present.
#
# THE "BEFORE" IS THE SAME BINARY (phases 4 and 8). OD_BLIND_RUN=unbounded restores what
# this tree did before #1383 -- the vigil ignores OD_MAX_CYCLES and nothing is said to a
# supervisor -- so no phase here is a claim about a build (#815's rule).
set -u
BIN=/app/build/opendartboard
RUN=/run1383
MOCKS=/app/mocks/cam_1.mp4,/app/mocks/cam_2.mp4,/app/mocks/cam_3.mp4
BLIND=$RUN/c1.jpg,$RUN/c2.jpg,$RUN/c3.jpg
SOCK=$RUN/notify.sock
FAILED=0
fail() { echo "FAIL $*"; FAILED=1; }
ok()   { echo "ok   $*"; }

# How long a blind board is left running in the two phases that assert it does NOT end.
# It has to be longer than the fixture takes to reach the vigil, which is measured in
# phase 1 and printed, or "it was still running" would only mean "it was still
# calibrating". Both phases assert BOARD FAULTED is in the log as well as rc=124, so the
# number being too small is a red build and never a quiet pass.
WAIT_S=40
# And the ceiling on a run that IS supposed to end on its own. Twenty vigil cycles is
# four seconds after the fault; everything before that is the fixture calibrating.
END_S=180

cp "$RUN/still.jpg" "$RUN/c1.jpg"; cp "$RUN/still.jpg" "$RUN/c2.jpg"; cp "$RUN/still.jpg" "$RUN/c3.jpg"

blind_run() {   # <name> <timeout> <env...> -- runs the blind fixture, prints rc and wall
  local name="$1" limit="$2"; shift 2
  local t0 t1
  t0=$(date +%s)
  env "$@" timeout "$limit" "$BIN" --debug --cams "$BLIND" \
    --width 1280 --height 720 --allow-plaintext > "$RUN/$name.out" 2> "$RUN/$name.err"
  RC=$?
  t1=$(date +%s)
  WALL=$((t1 - t0))
  cat "$RUN/$name.out" "$RUN/$name.err" > "$RUN/$name.all"
  echo "     | rc=$RC wall=${WALL}s log=$RUN/$name.all"
}

seeing_run() {  # <name> <timeout> <env...> -- the same, on the shipped mocks
  local name="$1" limit="$2"; shift 2
  env "$@" timeout "$limit" "$BIN" --debug --cams "$MOCKS" \
    --width 1280 --height 720 --allow-plaintext > "$RUN/$name.out" 2> "$RUN/$name.err"
  RC=$?
  cat "$RUN/$name.out" "$RUN/$name.err" > "$RUN/$name.all"
  echo "     | rc=$RC log=$RUN/$name.all"
}

listen() {      # <transcript> -- bind the socket systemd would have bound
  # The old readiness file goes FIRST and the wait is for the word rather than for a
  # size. `cmd > f &` truncates f in the CHILD, so a parent that asks whether f is
  # non-empty can be answered by the PREVIOUS phase's copy of it, leave the wait at once,
  # and grep a file the child truncates a millisecond later. Measured: phase 7 failed
  # that way on the first run of this tester and phases 6 and 8 did not.
  rm -f "$SOCK" "$1" "$RUN/listener.out"
  : > "$1"
  python3 /app/testers/i1383_notify_listener.py "$SOCK" "$1" > "$RUN/listener.out" 2>&1 &
  LISTENER=$!
  local waited=0
  while ! grep -q READY "$RUN/listener.out" 2>/dev/null && [ "$waited" -lt 100 ]; do
    sleep 0.2; waited=$((waited + 1))
  done
  if ! grep -q READY "$RUN/listener.out" 2>/dev/null; then
    fail "the notify listener never bound $SOCK"
    sed 's/^/     | /' "$RUN/listener.out" 2>/dev/null
    return 1
  fi
}
hangup() { kill "$LISTENER" 2>/dev/null; wait "$LISTENER" 2>/dev/null; }

echo "=== phase 1: the fixture is blind, and that is measured rather than assumed ==="
blind_run fixture "$END_S" OD_MAX_CYCLES=20
if grep -q "BOARD FAULTED" "$RUN/fixture.all"; then
  ok "the fixture takes #895's vigil: $(grep -m1 'BOARD FAULTED' "$RUN/fixture.all" | cut -c1-120)"
else
  fail "the fixture never faulted, so nothing below is about a blind board"
fi
if grep -qE "^SCORE: |Scorer running with" "$RUN/fixture.all"; then
  fail "the fixture scored or entered the scoring loop; it is not blind and every phase below it is empty"
  grep -m1 -E "^SCORE: |Scorer running with" "$RUN/fixture.all" | sed 's/^/     | /'
else
  ok "it never entered the scoring loop and never scored a dart"
fi
echo "     | it reached the vigil inside ${WALL}s; WAIT_S=$WAIT_S is the wait the two 'it does not end' phases use"

echo
echo "=== phase 2: #895's vigil, unchanged -- a blind board given no budget does not end ==="
blind_run vigil "$WAIT_S"
if [ "$RC" = 124 ] && grep -q "BOARD FAULTED" "$RUN/vigil.all"; then
  ok "it faulted and was still running ${WAIT_S}s later; the clock ended it, not the board"
elif [ "$RC" = 124 ]; then
  fail "it was still running but never faulted, so WAIT_S=$WAIT_S is too short to mean anything"
else
  fail "a blind board with no cycle budget EXITED (rc=$RC). #895's vigil is what this slice must not take away"
fi

echo
echo "=== phase 3: a blind board given a budget ends on it, and says what it is claiming ==="
blind_run budget "$END_S" OD_MAX_CYCLES=20
if [ "$RC" = 124 ]; then
  fail "the clock ended it after ${END_S}s; OD_MAX_CYCLES did not, which is the whole of #1383"
elif [ "$RC" = 75 ]; then
  ok "it ended itself and exited 75 -- EX_TEMPFAIL, 'I could not see', which is not 0 and not a crash"
else
  fail "it ended with rc=$RC; the claim a blind run makes is 75 and nothing else"
fi
if grep -q "BLIND RUN ENDED BY ITS CYCLE BUDGET" "$RUN/budget.all"; then
  ok "and it says so in its own words: $(grep -m1 'BLIND RUN ENDED' "$RUN/budget.all" | cut -c1-110)"
else
  fail "nothing in the log says the budget ended it, so the exit status is the only witness"
fi

echo
echo "=== phase 4: the same run on the same binary with #1383 switched off ==="
blind_run before "$WAIT_S" OD_MAX_CYCLES=20 OD_BLIND_RUN=unbounded
if [ "$RC" = 124 ] && grep -q "BOARD FAULTED" "$RUN/before.all"; then
  ok "before #1383 the budget was ignored: faulted, then still running when the clock took it"
elif [ "$RC" = 124 ]; then
  fail "it never faulted, so this phase measured a slow start rather than the old behaviour"
else
  fail "the falsifier did not restore the old behaviour (rc=$RC); phase 3 is then a claim about nothing"
fi

echo
echo "=== phase 5: a board that CAN see is untouched -- it ends on its budget and exits 0 ==="
seeing_run scoring "$END_S" OD_MAX_CYCLES=20
if [ "$RC" = 0 ] && grep -q "Scorer running with" "$RUN/scoring.all"; then
  ok "a scoring run still exits 0; 75 is a claim about a blind run and only about a blind run"
else
  fail "a scoring run answered rc=$RC (expected 0) or never reached the scoring loop"
fi

echo
echo "=== phase 6: what a supervisor is told about a board that cannot see ==="
if listen "$RUN/notify-blind.txt"; then
  blind_run notify-blind "$END_S" OD_MAX_CYCLES=20 NOTIFY_SOCKET="$SOCK"
  hangup
  sed 's/^/     | /' "$RUN/notify-blind.txt"
  if grep -q "^STATUS=ERROR: this board cannot see -- " "$RUN/notify-blind.txt"; then
    ok "it sent a STATUS naming the fault, on the wire systemd listens on"
  else
    fail "nothing the supervisor could read was sent by a board that cannot see"
  fi
fi

echo
echo "=== phase 7: and about a board that can -- the control that makes phase 6 mean something ==="
if listen "$RUN/notify-seeing.txt"; then
  seeing_run notify-seeing "$END_S" OD_MAX_CYCLES=20 NOTIFY_SOCKET="$SOCK"
  hangup
  sed 's/^/     | /' "$RUN/notify-seeing.txt"
  if grep -q "^STATUS=READY: scoring with " "$RUN/notify-seeing.txt"; then
    ok "a board that can see says something else entirely"
  else
    fail "a scoring board said nothing, so a silent Status line would mean 'old binary' as much as 'faulted'"
  fi
  if [ -s "$RUN/notify-blind.txt" ] && ! diff -q "$RUN/notify-blind.txt" "$RUN/notify-seeing.txt" > /dev/null; then
    ok "the two boards are told apart by what they say, which is the whole criterion"
  else
    fail "the two boards said the same thing; systemctl status could not tell them apart"
  fi
fi

echo
echo "=== phase 8: and what a supervisor was told before #1383 ==="
if listen "$RUN/notify-before.txt"; then
  blind_run notify-before "$WAIT_S" OD_MAX_CYCLES=20 OD_BLIND_RUN=unbounded NOTIFY_SOCKET="$SOCK"
  hangup
  if [ -s "$RUN/notify-before.txt" ]; then
    fail "the falsifier still spoke to the supervisor: $(head -1 "$RUN/notify-before.txt")"
  else
    ok "nothing at all -- which is what a reader of systemctl status had, on a board that could see nothing"
  fi
fi

echo
echo "load_at_end=$(cut -d' ' -f1-3 /proc/loadavg)"
exit $FAILED
