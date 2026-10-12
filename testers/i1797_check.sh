#!/bin/bash
# #1797: the 06:00 scheduled stop and the launcher that follows it, in the Linux container.
#
# Three things, one container, no network, no detector binary:
#
#   1. testers/i1797_schedule_check.cpp   the clock rule and its one guard with a fake
#                                         clock; decideMoment()'s fourth door; endingOf()
#                                         on exit code 60. Pure; unit_check.sh 1797 runs
#                                         the same file alone.
#   2. testers/i1797_carry_check.cpp      the launcher's own carry() against a real child
#                                         (testers/i1797_stub.cpp) that returns 60 once and
#                                         0 the second time: two starts, identical
#                                         arguments, one look in between, the sentence.
#   3. the mutation proofs, on a COPY of src/ (never the worktree):
#        --mutate-clock    scheduled_stop.hpp's kStopHour 6 -> 7. Every clock case that
#                          expects a stop at 06:00 must go red naming the hour.
#        --mutate-ending   ending.hpp's `code == kScheduledStop` branch deleted, so 60
#                          falls through to Faulted. The launcher cases must go red.
#      A harness that survives either is not measuring this slice.
#
# The include paths are i1303_inside.sh's: the launcher shares nlohmann and httplib with
# the detector since #1306, so build/_deps of a Linux build of this worktree must exist;
# OD_SKIP_BUILD changes nothing else about it. The script ends on `exit`, never on an
# `echo` (#1463, #1479).
#
#   testers/i1797_check.sh [--mutate-clock | --mutate-ending]
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"
MODE="${1:-}"

if [ ! -d "$OD_TREE_ROOT/build/_deps/nlohmann_json-src" ]; then
  echo "i1797_check: no $OD_TREE_ROOT/build/_deps -- configure a Linux build of this worktree first" >&2
  exit 2
fi

T0=$(date +%s)
od_run "1797-check" --network none -v "$OD_TREE_ROOT":/app -w /app -e MODE="$MODE" "$OD_IMAGE" bash -c '
  set -u
  SRC=/app/src
  WORK=/tmp/i1797
  rm -rf "$WORK" && mkdir -p "$WORK"

  if [ "${MODE:-}" = "--mutate-clock" ]; then
    rm -rf /tmp/mutated && cp -r /app/src /tmp/mutated
    F=/tmp/mutated/utils/scheduled_stop.hpp
    grep -c "const int kStopHour = 6;" "$F" | grep -qx 1 || { echo "MUTATION_FAILED: kStopHour is not where the mutation expects it"; exit 2; }
    sed -i "s/const int kStopHour = 6;/const int kStopHour = 7;/" "$F"
    echo "MUTATED: the stop hour is now 07:00 (kStopHour = 7), so nothing stops at 06:00"
    SRC=/tmp/mutated
  elif [ "${MODE:-}" = "--mutate-ending" ]; then
    rm -rf /tmp/mutated && cp -r /app/src /tmp/mutated
    F=/tmp/mutated/launcher/ending.hpp
    grep -c "if (outcome.code == kScheduledStop)" "$F" | grep -qx 1 || { echo "MUTATION_FAILED: the kScheduledStop branch is not where the mutation expects it"; exit 2; }
    # Delete the four-line branch: the if, the brace, the return, the brace.
    sed -i "/if (outcome.code == kScheduledStop)/,+3d" "$F"
    grep -q "return Ending::Scheduled;" "$F" && { echo "MUTATION_FAILED: the branch is still there"; exit 2; }
    echo "MUTATED: endingOf() no longer maps exit code 60 to Scheduled; it falls through to Faulted"
    SRC=/tmp/mutated
  fi

  DEPS="-I /app/build/_deps/nlohmann_json-src/include -I /app/build/_deps/httplib-src"
  g++ -O1 -std=c++17 -Wall -Wextra -I "$SRC" $DEPS \
      /app/testers/i1797_schedule_check.cpp -o "$WORK/schedule" || { echo COMPILE_FAILED; exit 2; }
  g++ -O1 -std=c++17 -Wall -Wextra -I "$SRC" $DEPS \
      /app/testers/i1797_carry_check.cpp -o "$WORK/carry" || { echo COMPILE_FAILED; exit 2; }
  g++ -O1 -std=c++17 -Wall -Wextra \
      /app/testers/i1797_stub.cpp -o "$WORK/stub" || { echo COMPILE_FAILED; exit 2; }

  RC=0
  echo "==== i1797_schedule_check (zone: $(date +%Z)) ======================================"
  "$WORK/schedule" < /dev/null; rc=$?; echo "i1797_schedule_check rc=$rc"; [ $rc -eq 0 ] || RC=1
  echo "==== i1797_carry_check =========================================================="
  "$WORK/carry" "$WORK/stub" "$WORK" < /dev/null; rc=$?; echo "i1797_carry_check rc=$rc"; [ $rc -eq 0 ] || RC=1
  exit $RC
'
RC=$?
T1=$(date +%s)
echo "WALL $((T1 - T0)) s"
echo "CHECK_RC=$RC"
if [ -n "$MODE" ]; then
  if [ "$RC" -eq 0 ]; then
    echo "MUTATION NOT CAUGHT: the harness passed under $MODE."
    exit 1
  fi
  echo "MUTATION CAUGHT: the harness goes red under $MODE."
  exit 0
fi
# The harness must exit on what it measured: run_all.sh reads the exit code and
# nothing else, and an echo returns 0 whatever it printed (#1335).
exit $RC
