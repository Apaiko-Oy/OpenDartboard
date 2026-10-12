#!/bin/bash
# turnaus#1793: after "there was no dart there", the visit's real third dart is pushed.
# One compile of the tree plus five of a MUTATED SCRATCH COPY of src, in i1781_check.sh's
# shape and for its reason: the tree is mounted read-only and never written; each mutation
# is a replacement on a copy under the container's /tmp, and must find its needle exactly
# once. testers/i1793_round_check.cpp replays a round (a phantom the player removes, then
# three real darts) through the vote's pure decisions and plays Turnaus's #1281 rule.
#
#   1. the tree            must pass whole
#   2. no up vote          votesUp loses its past-three clause -- PREDICTION: exactly 2 FAIL
#                          lines, both `on:` (S1 is not pushed; the fourth arrival is not
#                          pushed either); `off:` holds
#   3. switch ignored      votesArrivalPastThree ignores OD_PAST_THREE -- PREDICTION: exactly
#                          2 FAIL lines, one `off:` (the switch-off board now pushes S1) and
#                          one `on:` (its full-round comparison has no switch-off turn of 3)
#   4. not published       windowPublishes back to `previous != current` -- PREDICTION:
#                          exactly 2 FAIL lines, both `on:`, as in 2 but one stage later
#   5. no quorum           a lone camera calls a dart past three -- PREDICTION: exactly 1
#                          FAIL, `quorum:`
#   6. dropped said always droppedAnswerIsSaid says every DROPPED -- PREDICTION: exactly 1
#                          FAIL, `dropped:`
#
# The script ends on `exit`, never on an `echo`: #1463, #1479.
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"

od_run "1793-check" --network none \
  -v "$OD_TREE_ROOT":/app:ro -w /app "$OD_IMAGE" bash -c '
  set -u
  DP=src/detector/geometry/detection/dart_processing.hpp
  TC=src/communication/push_answer.hpp
  compile() { # $1 include root for src, $2 output
    g++ -std=c++17 -O1 -Wall -Wextra -I "$1" -I "$1/utils" \
        -o "$2" testers/i1793_round_check.cpp $(pkg-config --cflags --libs opencv4)
  }
  mutate() { # $1 scratch dir, $2 file, $3 needle (fixed string, must occur once), $4 replacement
    rm -rf "$1"; mkdir -p "$1"; cp -r src "$1/src"
    local before after
    before=$(grep -c -F -- "$3" "$1/$2")
    python3 - "$1/$2" "$3" "$4" <<PY
import sys
p, a, b = sys.argv[1:4]
s = open(p).read()
open(p, "w").write(s.replace(a, b))
PY
    after=$(grep -c -F -- "$3" "$1/$2")
    echo "mutation in $1: needle found $before time(s) before, $after after"
    [ "$before" -eq 1 ] && [ "$after" -eq 0 ]
  }
  run_mutation() { # $1 name, $2 file, $3 needle, $4 replacement, $5 FAIL count, $6.. the labels predicted red, one per line
    local name=$1 file=$2 needle=$3 repl=$4 want=$5
    shift 5
    echo "==== mutation $name ==========================================================="
    echo "PREDICTION: fails, with exactly $want FAIL line(s): $*"
    mutate "/tmp/$name" "$file" "$needle" "$repl" || { echo "FAIL mutation $name did not land"; return 1; }
    compile "/tmp/$name/src" "/tmp/b_$name" || { echo "COMPILE_FAILED ($name)"; return 1; }
    "/tmp/b_$name" < /dev/null > "/tmp/$name.out" 2>&1
    local rc=$? fails ok=1 label n
    cat "/tmp/$name.out"
    # "^FAIL " with the space: FAILURES: n would match "^FAIL" (#1556).
    fails=$(grep -c "^FAIL " "/tmp/$name.out")
    for label in "$@"; do
      n=$(grep -c "^FAIL $label" "/tmp/$name.out")
      want_n=$(printf "%s\n" "$@" | grep -c -x -F -- "$label")
      [ "$n" -eq "$want_n" ] || ok=0
    done
    echo "mutation $name rc=$rc fails=$fails"
    [ $rc -ne 0 ] && [ "$fails" -eq "$want" ] && [ $ok -eq 1 ] ||
      { echo "FAIL mutation $name did not fail as predicted"; return 1; }
  }

  compile src /tmp/b_tree || { echo "COMPILE_FAILED"; exit 2; }
  echo "==== 1. the tree ==============================================================="
  /tmp/b_tree < /dev/null
  [ $? -eq 0 ] || { echo "FAIL the tree did not hold"; exit 1; }

  BAD=0
  run_mutation no_up "$DP" "detected > board || (arrived_past_three && board == DartBoardState::DART_3)" \
    "detected > board" 2 "on: the real third" "on: a fourth" || BAD=1
  run_mutation switch_ignored "$DP" "return switch_on && fresh_arrival" "return fresh_arrival" \
    2 "off: the board stops" "on: a fourth" || BAD=1
  run_mutation not_published "$DP" "return previous != current || windowCalledADart(previous, current, past_three);" \
    "return previous != current;" 2 "on: the real third" "on: a fourth" || BAD=1
  run_mutation no_quorum "$DP" "quorum_moves = moves_up >= quorum;" \
    "quorum_moves = moves_up >= quorum || (board == DartBoardState::DART_3 && moves_up > 0);" 1 "quorum:" || BAD=1
  run_mutation dropped_always "$TC" "|| !said_this_round;" "|| true;" 1 "dropped:" || BAD=1
  [ $BAD -eq 0 ] || exit 1
  echo "ALL SIX HELD"
  exit 0
'
exit $?
