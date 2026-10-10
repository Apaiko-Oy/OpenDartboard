#!/bin/bash
# #1797: the scheduled stop on a real replay of mocks/rig-20260918, twice, by hand.
#
# NOT IN run_all.sh, deliberately: it runs the detector unbounded on sixty seconds of
# three-camera footage (the bakeoff's shape, ~2-4 min a run on this box), and the pure
# checks in i1797_check.sh are what gates the rule. This is the proof that the stop goes
# out by the ordinary shutdown and changes nothing when it does not fire:
#
#   control   OD_SCHEDULED_RESTART unset, the container's real clock (UTC; the run says
#             which hour, and it must not be 05:xx or 06:xx for the control to mean
#             anything). The replay runs to the end of the footage: no `I1797 SCHEDULED
#             RESTART at` line, `SCORE:` lines counted, exit code 0.
#   forced    OD_SCHEDULED_CLOCK=<today 05:59:xx UTC as unix seconds>, an INJECTED clock
#             for the rule alone (scorer.cpp, scheduleClock): the schedule starts at that
#             instant when the schedule is ARMED (the start of scoring, where scheduleClock is
#             first read) and advances with the steady clock, so it meets 06:00 OFFSET
#             seconds after scoring begins. Two, so the first cycles after the arming meet
#             it before any dart has landed: with 40 and with 20 the first dart came 37 s
#             and 19 s after the arming on two runs of this box (the replay pace moves
#             with the host load) and the guard postponed to 06:10, which the footage
#             never reaches -- a measurement of the guard, not of the stop. This run is
#             PAIRED, to a credential planted here and a closed loopback port, because an
#             unpaired client stops silently and the two sentences below are a running
#             client's. The run must end with, in this order: the `I1797 SCHEDULED
#             RESTART at 06:00` sentence, #1787's log-upload sentence, `TURNAUS: client
#             stopped`; and exit 60. MEASURED 2026-10-11: control 24 SCORE: lines, exit
#             0; forced stop at 23:18:52.611, two seconds into scoring, lines 68/71/72 in
#             that order, exit 60; 157 s wall for the pair.
#
#   testers/i1797_replay.sh [offset-seconds]     default 2
#
# Both runs pass --log-file, because the log-upload sentence is only said about a log
# file, and the file is what the forced run's ordering is read from.
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"
OFFSET="${1:-2}"
RUN="$OD_RUNS_BASE/i1797"
rm -rf "$RUN" && mkdir -p "$RUN"

if [ ! -s "$OD_TREE_ROOT/build/opendartboard" ]; then  # -s, not -x: Git Bash answers no to -x on an ELF file
  echo "i1797_replay: no $OD_TREE_ROOT/build/opendartboard -- build this worktree first" >&2
  exit 2
fi

T0=$(date +%s)
od_run "1797-replay" --cpus=2 --network none -e HOME=/root -e OFFSET="$OFFSET" \
  -v "$OD_TREE_ROOT":/app -v "$RUN":/run1797 -w /run1797 "$OD_IMAGE" bash -c '
  set -u
  BIN=/app/build/opendartboard
  DIR=/app/mocks/rig-20260918
  CAMS="$DIR/cam_1.mp4,$DIR/cam_2.mp4,$DIR/cam_3.mp4"
  FAILED=0
  note() { if [ "$1" -eq 0 ]; then echo "ok   $2"; else echo "FAIL $2"; FAILED=$((FAILED + 1)); fi; }

  EXTRA_ARGS=""
  replay() { # replay <name> <env...>; EXTRA_ARGS is appended to the command line
    local name="$1"; shift
    rm -rf /run1797/cache /run1797/debug_frames
    env OD_MAX_CYCLES=0 "$@" timeout 900 $BIN --cams "$CAMS" --width 1280 --height 720 \
        --log-file "/run1797/$name.log" $EXTRA_ARGS > "/run1797/$name.out" 2>&1
    local rc=$?
    sed "s/\x1b\[[0-9;]*m//g" "/run1797/$name.out" > "/run1797/$name.txt"
    echo "replay $name rc=$rc lines=$(wc -l < /run1797/$name.txt) SCORE_lines=$(grep -c "SCORE:" "/run1797/$name.log")"
    return $rc
  }

  echo "==== control: the real clock, hour $(date -u +%H) UTC, pin unset ===================="
  replay control; RC=$?
  note $([ $RC -eq 0 ] && echo 0 || echo 1) "control: the replay ran to the end of the footage and exited 0 (rc=$RC)"
  grep -q "I1797 SCHEDULED RESTART armed" /run1797/control.log; note $? "control: the schedule was armed (the rule is on by default)"
  ! grep -q "I1797 SCHEDULED RESTART at" /run1797/control.log; note $? "control: and it never stopped the replay"
  ! grep -q "OD_SCHEDULED_RESTART=off" /run1797/control.log; note $? "control: and no pin was said"
  echo "control: I1797 lines:"; grep "I1797" /run1797/control.log | sed "s/^/     | /"
  echo "control: first SCORE line and SCORING STARTS, with the log clock:"
  grep -m1 "SCORING STARTS\|Scorer running" /run1797/control.log | sed "s/^/     | /"
  grep -m1 "SCORE:" /run1797/control.log | sed "s/^/     | /"
  grep -c "SCORE:" /run1797/control.log | sed "s/^/     control SCORE: lines = /"
  tail -3 /run1797/control.log | sed "s/^/     | /"

  echo
  echo "==== forced: OD_SCHEDULED_CLOCK puts the rule at 06:00 ${OFFSET}s after the start ===="
  FORCED=$(( $(date -u -d "$(date -u +%Y-%m-%d) 06:00:00" +%s) - OFFSET ))
  echo "OD_SCHEDULED_CLOCK=$FORCED ($(date -u -d @$FORCED +%H:%M:%S) UTC for the rule; the log stamps stay real)"
  # A credential, so the Turnaus client is PAIRED and runs its push thread: an unpaired
  # client stops silently, and the two shutdown sentences this run is about are only said
  # by a client that was running. The address is a closed port on loopback (--network
  # none), so every push and the final log post are refused at once and nothing leaves.
  printf '"'"'{"token":"od_i1797_not_a_real_token","device_id":1797,"base_url":"http://127.0.0.1:1"}\n'"'"' > /run1797/credentials.json
  chmod 600 /run1797/credentials.json
  EXTRA_ARGS="--credentials /run1797/credentials.json --turnaus http://127.0.0.1:1 --allow-plaintext"
  replay forced OD_SCHEDULED_CLOCK="$FORCED"; RC=$?
  note $([ $RC -eq 60 ] && echo 0 || echo 1) "forced: the detector exited 60, scheduled_stop::kExitCode (rc=$RC)"
  grep -q "I1797 SCHEDULED RESTART at 06:00: the launcher looks for an update and starts the board again" /run1797/forced.log
  note $? "forced: the stop sentence is in the log, verbatim"
  STOP=$(grep -n "I1797 SCHEDULED RESTART at" /run1797/forced.log | head -1 | cut -d: -f1)
  UPLOAD=$(grep -n "TURNAUS: log upload:" /run1797/forced.log | head -1 | cut -d: -f1)
  STOPPED=$(grep -n "TURNAUS: client stopped" /run1797/forced.log | head -1 | cut -d: -f1)
  echo "     line numbers: I1797 stop=$STOP  log upload=$UPLOAD  client stopped=$STOPPED"
  [ -n "$STOP" ] && [ -n "$UPLOAD" ] && [ -n "$STOPPED" ] && [ "$STOP" -lt "$UPLOAD" ] && [ "$UPLOAD" -lt "$STOPPED" ]
  note $? "forced: I1797, then the log-upload sentence, then client stopped, in that order"
  SCORER_STOPPED=$(grep -n "Scorer stopped" /run1797/forced.log | head -1 | cut -d: -f1)
  WS_STOPPED=$(grep -n "WebSocket service stopped" /run1797/forced.log | head -1 | cut -d: -f1)
  echo "     line numbers: Scorer stopped=$SCORER_STOPPED  WebSocket service stopped=$WS_STOPPED"
  [ -n "$SCORER_STOPPED" ] && [ -n "$WS_STOPPED" ] && [ "${STOP:-0}" -lt "$SCORER_STOPPED" ] && [ "$SCORER_STOPPED" -lt "${STOPPED:-0}" ] && [ "${STOPPED:-0}" -lt "$WS_STOPPED" ]
  note $? "forced: and the ordinary unwind around them: Scorer stopped before the client, the WebSocket service after it (#825, ~Scorer in member order)"
  SCORED_BEFORE=$(sed -n "1,${STOP:-1}p" /run1797/forced.log | grep -c "SCORE:")
  echo "     SCORE: lines before the stop: $SCORED_BEFORE (the guard would have postponed a stop inside 10 min of one)"
  grep -q "Scorer running with" /run1797/forced.log; note $? "forced: the stop came after scoring had started, not during calibration"
  echo "forced: the tail of the log:"
  sed -n "$((STOP > 3 ? STOP - 3 : 1)),\$p" /run1797/forced.log | sed "s/^/     | /"

  echo
  echo "$FAILED failed"
  exit $FAILED
'
RC=$?
echo "WALL $(( $(date +%s) - T0 )) s"
echo "CHECK_RC=$RC"
exit $RC
