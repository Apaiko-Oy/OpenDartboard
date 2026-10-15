#!/bin/bash
# turnaus#1867: a rim-carried dart with no usable axis whose lone camera reads it inside
# 134.5 mm publishes as read, flagged with MISS, by default (rimContradictsTheReading).
# One compile of the tree plus two of a MUTATED SCRATCH COPY of the header, in
# i1855_check.sh's shape: the tree is mounted read-only and never written.
#
#   1. the tree                 i1867_contradiction_check -- must pass whole, and prints the
#                                issue's six with what each publishes now
#   2. mutation A: no rule      `c.flag = f.to_miss;` reads `c.flag = false;`. PREDICTION:
#                                exactly 8 FAIL lines, all `offer:`, the three misses 2787,
#                                2788 and 3002 among them by number; no other prefix.
#   3. mutation B: no radius    `c.flag = f.to_miss;` reads `c.flag = applies && unsupported;`
#                                -- every rim-carried unsupported lone reading offered MISS,
#                                doubles and outer singles too. PREDICTION: exactly 4 FAIL
#                                lines, `double:` 2728 and 2939 (the two whose RING-WIRE
#                                alternative was not already MISS) and `kept:` 2689 and 2828.
#
# The rows are read from the 2026-10-15 device/2 log; no replay, because the kept frames
# are stills and not footage. The script ends on `exit`, never on an `echo`: #1463, #1479.
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"

if [ ! -d "$OD_TREE_ROOT/build/_deps" ]; then
  echo "i1867_check: $OD_TREE_ROOT/build/_deps is not there; build this worktree first" >&2
  exit 2
fi

od_run "1867-check" --network none \
  -v "$OD_TREE_ROOT":/app:ro -w /app "$OD_IMAGE" bash -c '
  set -u
  HDR=src/detector/geometry/detection/score_processing.hpp
  NEEDLE="c.flag = f.to_miss;"
  compile() { # $1 include root for src, $2 output
    g++ -std=c++17 -O1 -Wall -Wextra \
        -I "$1" -I "$1/utils" \
        -I build/_deps/nlohmann_json-src/include \
        -I build/_deps/httplib-src \
        -o "$2" testers/i1867_contradiction_check.cpp \
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
  named() { grep -c "^FAIL $2: $3 " "$1"; }

  compile src /tmp/i1867_tree || { echo "COMPILE_FAILED"; exit 2; }
  echo "==== 1. the tree rule ========================================================="
  /tmp/i1867_tree < /dev/null > /tmp/tree.out 2>&1
  RC_TREE=$?
  cat /tmp/tree.out

  echo "==== 2. mutation A: the rule never fires ======================================"
  echo "PREDICTION: exactly 8 FAIL lines, all offer:, 2787 2788 3002 among them."
  mutate /tmp/mutA "s/c.flag = f.to_miss;/c.flag = false;/" || { echo "FAIL mutation A did not land"; exit 1; }
  compile /tmp/mutA/src /tmp/i1867_mutA || { echo "COMPILE_FAILED (mutation A)"; exit 2; }
  /tmp/i1867_mutA < /dev/null > /tmp/mutA.out 2>&1
  RC_A=$?
  cat /tmp/mutA.out
  A_FAILS=$(fails /tmp/mutA.out)
  A_OFFER=$(grep -c "^FAIL offer:" /tmp/mutA.out)
  A_SIX=$(( $(named /tmp/mutA.out offer 2787) + $(named /tmp/mutA.out offer 2788) + $(named /tmp/mutA.out offer 3002) ))

  echo "==== 3. mutation B: the radius ignored ========================================"
  echo "PREDICTION: exactly 4 FAIL lines: double: 2728, double: 2939, kept: 2689, kept: 2828."
  mutate /tmp/mutB "s/c.flag = f.to_miss;/c.flag = applies \&\& unsupported;/" || { echo "FAIL mutation B did not land"; exit 1; }
  compile /tmp/mutB/src /tmp/i1867_mutB || { echo "COMPILE_FAILED (mutation B)"; exit 2; }
  /tmp/i1867_mutB < /dev/null > /tmp/mutB.out 2>&1
  RC_B=$?
  cat /tmp/mutB.out
  B_FAILS=$(fails /tmp/mutB.out)
  B_NAMED=$(( $(named /tmp/mutB.out double 2728) + $(named /tmp/mutB.out double 2939) + $(named /tmp/mutB.out kept 2689) + $(named /tmp/mutB.out kept 2828) ))

  echo
  echo "tree rc=$RC_TREE"
  echo "mutA rc=$RC_A fails=$A_FAILS predicted=8 offer=$A_OFFER three-misses=$A_SIX"
  echo "mutB rc=$RC_B fails=$B_FAILS predicted=4 named=$B_NAMED"
  [ $RC_TREE -eq 0 ] || { echo "FAIL the tree rule did not hold"; exit 1; }
  [ $RC_A -ne 0 ] && [ "$A_FAILS" -eq 8 ] && [ "$A_OFFER" -eq 8 ] && [ "$A_SIX" -eq 3 ] \
    || { echo "FAIL mutation A did not fail as predicted"; exit 1; }
  [ $RC_B -ne 0 ] && [ "$B_FAILS" -eq 4 ] && [ "$B_NAMED" -eq 4 ] \
    || { echo "FAIL mutation B did not fail as predicted"; exit 1; }
  echo "ALL THREE HELD"
  exit 0
'
