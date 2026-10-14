#!/bin/bash
# #1855: the to-MISS half of #1707's rim fallback reads the lone reading's radius -- MISS
# inside kRimMissBelowMm (134.5 mm, the outer single's middle), as read at or beyond it.
# One compile of the tree plus two of a MUTATED SCRATCH COPY of the header, in
# i1773_check.sh's shape and for its reason: the measurement is the set of runs on one tree.
# The mutations are made with sed on a copy of src/ under the container's /tmp; the tree is
# mounted and never written.
#
#   1. the tree                 i1855_rim_check -- must pass whole and print KEPT n= (the
#                                assertions only the radius keeps a score) and MISS n= (the
#                                ones only the line's position sends to MISS)
#   2. mutation A: no radius    the scratch header's `f.kept = f.radius_mm >= kRimMissBelowMm;`
#                                reads `f.kept = false;` -- #1707's rule as it was. Must FAIL,
#                                PREDICTION STATED FIRST: exactly k FAIL lines, all `kept:`,
#                                the 0.926 S20 (L1) among them by name; no `miss:`, no `pure:`.
#   3. mutation B: the line     the comparison reads kBull25WireMm (15.9) where the tree reads
#      moved in                  kRimMissBelowMm -- everything off the bull is kept. Must FAIL
#                                with exactly m FAIL lines, all `miss:`, the 0.562 miss (L2)
#                                among them by name; no `kept:`, no `pure:`.
#
# It links no extra translation unit: the rule is over strings and a float, in the header.
# The script ends on `exit`, never on an `echo`: #1463, #1479.
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"

if [ ! -d "$OD_TREE_ROOT/build/_deps" ]; then
  echo "i1855_check: $OD_TREE_ROOT/build/_deps is not there; build this worktree first" >&2
  exit 2
fi

od_run "1855-check" --network none \
  -v "$OD_TREE_ROOT":/app -w /app "$OD_IMAGE" bash -c '
  set -u
  HDR=src/detector/geometry/detection/score_processing.hpp
  compile() { # $1 include root for src, $2 output
    g++ -std=c++17 -O1 -Wall -Wextra \
        -I "$1" -I "$1/utils" \
        -I build/_deps/nlohmann_json-src/include \
        -I build/_deps/httplib-src \
        -o "$2" testers/i1855_rim_check.cpp \
        $(pkg-config --cflags --libs opencv4)
  }
  mutate() { # $1 scratch dir, $2 sed expression, $3 the needle it must find exactly once
    rm -rf "$1"; mkdir -p "$1"; cp -r src "$1/src"
    local before after
    before=$(grep -c -F -- "$3" "$1/$HDR")
    sed -i "$2" "$1/$HDR"
    after=$(grep -c -F -- "$3" "$1/$HDR")
    echo "mutation in $1: needle found $before time(s) before, $after after"
    [ "$before" -eq 1 ] && [ "$after" -eq 0 ]
  }

  compile src /tmp/i1855_tree || { echo "COMPILE_FAILED"; exit 2; }

  echo "==== 1. the tree rule ========================================================="
  /tmp/i1855_tree < /dev/null > /tmp/tree.out 2>&1
  RC_TREE=$?
  cat /tmp/tree.out
  K=$(sed -n "s/.*KEPT n=\([0-9]*\).*/\1/p" /tmp/tree.out | head -1)
  M=$(sed -n "s/.*MISS n=\([0-9]*\).*/\1/p" /tmp/tree.out | head -1)
  [ -n "$K" ] && [ -n "$M" ] || { echo "FAIL the check did not print its sensitive counts"; exit 1; }

  echo "==== 2. mutation A: the radius keeps nothing (#1707 as it was) ================="
  echo "PREDICTION: fails, with exactly $K FAIL lines, all kept: -- L1, the 0.926 S20, among"
  echo "            them -- and not one miss: or pure: assertion."
  mutate /tmp/mutA "s/f.kept = f.radius_mm >= kRimMissBelowMm;/f.kept = false;/" \
         "f.kept = f.radius_mm >= kRimMissBelowMm;" || { echo "FAIL mutation A did not land"; exit 1; }
  compile /tmp/mutA/src /tmp/i1855_mutA || { echo "COMPILE_FAILED (mutation A)"; exit 2; }
  /tmp/i1855_mutA < /dev/null > /tmp/mutA.out 2>&1
  RC_A=$?
  cat /tmp/mutA.out
  # "^FAIL " with the space: the summary line FAILURES: n would match "^FAIL" (#1556).
  A_FAILS=$(grep -c "^FAIL " /tmp/mutA.out)
  A_KEPT=$(grep -c "^FAIL kept:" /tmp/mutA.out)
  A_L1=$(grep -c "^FAIL kept: L1 " /tmp/mutA.out)

  echo "==== 3. mutation B: the line moved in to the 25s wire ========================="
  echo "PREDICTION: fails, with exactly $M FAIL lines, all miss: -- L2, the 0.562 miss, among"
  echo "            them -- and not one kept: or pure: assertion."
  mutate /tmp/mutB "s/f.kept = f.radius_mm >= kRimMissBelowMm;/f.kept = f.radius_mm >= kBull25WireMm;/" \
         "f.kept = f.radius_mm >= kRimMissBelowMm;" || { echo "FAIL mutation B did not land"; exit 1; }
  compile /tmp/mutB/src /tmp/i1855_mutB || { echo "COMPILE_FAILED (mutation B)"; exit 2; }
  /tmp/i1855_mutB < /dev/null > /tmp/mutB.out 2>&1
  RC_B=$?
  cat /tmp/mutB.out
  B_FAILS=$(grep -c "^FAIL " /tmp/mutB.out)
  B_MISS=$(grep -c "^FAIL miss:" /tmp/mutB.out)
  B_L2=$(grep -c "^FAIL miss: L2 " /tmp/mutB.out)

  echo
  echo "tree rc=$RC_TREE kept k=$K miss m=$M"
  echo "mutA rc=$RC_A fails=$A_FAILS predicted=$K kept=$A_KEPT L1=$A_L1"
  echo "mutB rc=$RC_B fails=$B_FAILS predicted=$M miss=$B_MISS L2=$B_L2"
  [ $RC_TREE -eq 0 ] || { echo "FAIL the tree rule did not hold"; exit 1; }
  [ $RC_A -ne 0 ] || { echo "FAIL mutation A PASSED -- the radius keeps nothing load-bearing"; exit 1; }
  [ "$A_FAILS" -eq "$K" ] && [ "$A_KEPT" -eq "$K" ] || { echo "FAIL mutation A failed $A_FAILS ($A_KEPT kept:) where the prediction was $K, all kept:"; exit 1; }
  [ "$A_L1" -eq 1 ] || { echo "FAIL mutation A left the 0.926 S20 a score"; exit 1; }
  [ $RC_B -ne 0 ] || { echo "FAIL mutation B PASSED -- the line position is not load-bearing"; exit 1; }
  [ "$B_FAILS" -eq "$M" ] && [ "$B_MISS" -eq "$M" ] || { echo "FAIL mutation B failed $B_FAILS ($B_MISS miss:) where the prediction was $M, all miss:"; exit 1; }
  [ "$B_L2" -eq 1 ] || { echo "FAIL mutation B left the 0.562 miss a MISS"; exit 1; }
  echo "ALL THREE HELD"
  exit 0
'
