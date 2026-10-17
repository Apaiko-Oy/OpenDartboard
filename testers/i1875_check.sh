#!/bin/bash
# turnaus#1875: a score one camera read alone (the vote's "No consensus" branch) publishes
# FLAGGED, with its first candidate as the alternative, even where LONE-WIRE / RING-WIRE say
# it is clear (flagAnUncheckedLoneReading). One compile of the tree plus one of a MUTATED
# SCRATCH COPY of the header, in i1867_check.sh's shape: the tree is mounted read-only and
# never written.
#
#   1. the tree                 i1875_lone_flag_check -- must pass whole, and prints the
#                                truth-line replay of device/2 2026-10-15 and -16
#   2. mutation A: no rule      `u.flag = true;` reads `u.flag = false;` -- a lone reading
#                                clear by its own ruler publishes unflagged again. PREDICTION:
#                                exactly 61 FAIL lines: 53 `lone:` (34 + 19 rows), 3 `named:`
#                                (the issue's 01M52GGA8RE8R1RGHTW2F6FP0Z and
#                                01M52HDF91B8AK0AP5QVG4PBH6, and #1822's 01M4KN6RGE535FXAG290F9THMQ),
#                                4 `count:` and 1 `pure:` (no candidate); no `kept:`.
#
# Truth-line replay only; the kept frames are stills, not footage. The script ends on
# `exit`, never on an `echo`: #1463, #1479.
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"

if [ ! -d "$OD_TREE_ROOT/build/_deps" ]; then
  echo "i1875_check: $OD_TREE_ROOT/build/_deps is not there; build this worktree first" >&2
  exit 2
fi

od_run "1875-check" --network none \
  -v "$OD_TREE_ROOT":/app:ro -w /app "$OD_IMAGE" bash -c '
  set -u
  HDR=src/detector/geometry/detection/score_processing.hpp
  NEEDLE="u.flag = true;"
  compile() { # $1 include root for src, $2 output
    g++ -std=c++17 -O1 -Wall -Wextra \
        -I "$1" -I "$1/utils" \
        -I build/_deps/nlohmann_json-src/include \
        -I build/_deps/httplib-src \
        -o "$2" testers/i1875_lone_flag_check.cpp \
        $(pkg-config --cflags --libs opencv4)
  }
  mutate() { # $1 scratch dir, $2 sed expression
    rm -rf "$1"; mkdir -p "$1"; cp -r src "$1/src"
    local before after
    before=$(grep -c -F -- "$NEEDLE" "$1/$HDR")
    sed -i "$2" "$1/$HDR"
    after=$(grep -c -F -- "$NEEDLE" "$1/$HDR")
    echo "mutation in $1: needle found $before time(s) before, $after after"
    [ "$before" -eq 1 ] && [ "$after" -eq 0 ]
  }
  fails() { grep -c "^FAIL " "$1"; }                   # the space: FAILURES: n is not one
  prefixed() { grep -c "^FAIL $2:" "$1"; }

  compile src /tmp/i1875_tree || { echo "COMPILE_FAILED"; exit 2; }
  echo "==== 1. the tree rule ========================================================="
  /tmp/i1875_tree < /dev/null > /tmp/tree.out 2>&1
  RC_TREE=$?
  cat /tmp/tree.out

  echo "==== 2. mutation A: a lone reading clear by its own ruler publishes unflagged =="
  echo "PREDICTION: exactly 61 FAIL lines: 53 lone:, 3 named:, 4 count:, 1 pure:, 0 kept:."
  mutate /tmp/mutA "s/u.flag = true;/u.flag = false;/" || { echo "FAIL mutation A did not land"; exit 1; }
  compile /tmp/mutA/src /tmp/i1875_mutA || { echo "COMPILE_FAILED (mutation A)"; exit 2; }
  /tmp/i1875_mutA < /dev/null > /tmp/mutA.out 2>&1
  RC_A=$?
  cat /tmp/mutA.out
  A_FAILS=$(fails /tmp/mutA.out)
  A_LONE=$(prefixed /tmp/mutA.out lone)
  A_NAMED=$(prefixed /tmp/mutA.out named)
  A_COUNT=$(prefixed /tmp/mutA.out count)
  A_PURE=$(prefixed /tmp/mutA.out pure)
  A_KEPT=$(prefixed /tmp/mutA.out kept)
  A_TWO=$(( $(grep -c "^FAIL named: .*01M52GGA8RE8R1RGHTW2F6FP0Z" /tmp/mutA.out) + $(grep -c "^FAIL named: .*01M52HDF91B8AK0AP5QVG4PBH6" /tmp/mutA.out) ))

  echo
  echo "tree rc=$RC_TREE"
  echo "mutA rc=$RC_A fails=$A_FAILS predicted=61 lone=$A_LONE named=$A_NAMED count=$A_COUNT pure=$A_PURE kept=$A_KEPT the-two=$A_TWO"
  [ $RC_TREE -eq 0 ] || { echo "FAIL the tree rule did not hold"; exit 1; }
  [ $RC_A -ne 0 ] && [ "$A_FAILS" -eq 61 ] && [ "$A_LONE" -eq 53 ] && [ "$A_NAMED" -eq 3 ] \
    && [ "$A_COUNT" -eq 4 ] && [ "$A_PURE" -eq 1 ] && [ "$A_KEPT" -eq 0 ] && [ "$A_TWO" -eq 2 ] \
    || { echo "FAIL mutation A did not fail as predicted"; exit 1; }
  echo "BOTH HELD"
  exit 0
'
