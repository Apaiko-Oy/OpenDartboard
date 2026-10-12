#!/bin/bash
# #1782: a flagged solve within its sigma of a ring wire AND a wedge wire offers the corner --
# the three other cells by the solve's own covariance, an unused camera's clear reading of
# one of them first -- and the published score never moves. One compile of the tree plus
# three of a MUTATED SCRATCH COPY of the header, in i1773_check.sh's shape and for its
# reason: the measurement is the set of runs on one tree. The mutations are made with sed on
# a copy of src/ under the container's /tmp; the tree is mounted and never written.
#
#   1. the tree               i1782_corner_check -- must pass whole and print SENSITIVE
#                             reach= clear= diag= (the assertions each mutation can reach)
#   2. mutation A: reach      the wedge half of the corner threshold reads `<= 2.0 * k` --
#                             PREDICTION STATED FIRST: exactly the reach: assertions fail
#                             (the 8 mm dart, 1.51 sigma from its wedge wire, becomes a
#                             corner and offers S19), and no other.
#   3. mutation B: clear      the unused camera's promotion is disabled -- exactly the
#                             clear: assertions fail, and no other: the covariance order
#                             is untouched.
#   4. mutation C: diagonal   the cell across both wires is never a cell -- exactly the
#                             diag: AND clear: assertions fail (the live unused camera read
#                             S19, which is the diagonal, so nothing is left to promote),
#                             and no reach: or pure: one.
#
# It links no extra translation unit: the rule is over primitives, in score_processing.hpp.
# The script ends on `exit`, never on an `echo`: #1463, #1479.
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"

if [ ! -d "$OD_TREE_ROOT/build/_deps" ]; then
  echo "i1782_check: $OD_TREE_ROOT/build/_deps is not there; build this worktree first" >&2
  exit 2
fi

od_run "1782-check" --network none \
  -v "$OD_TREE_ROOT":/app -w /app "$OD_IMAGE" bash -c '
  set -u
  HDR=src/detector/geometry/detection/score_processing.hpp
  compile() { # $1 include root for src, $2 output
    g++ -std=c++17 -O1 -Wall -Wextra \
        -I "$1" -I "$1/utils" \
        -I build/_deps/nlohmann_json-src/include \
        -I build/_deps/httplib-src \
        -o "$2" testers/i1782_corner_check.cpp \
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
  run() { # $1 binary, $2 output; prints the counts "fails reach clear diag pure"
    "$1" < /dev/null > "$2" 2>&1
    echo "$? $(grep -c "^FAIL " "$2") $(grep -c "^FAIL reach:" "$2") $(grep -c "^FAIL clear:" "$2") $(grep -c "^FAIL diag:" "$2") $(grep -c "^FAIL pure:" "$2")"
  }

  compile src /tmp/i1782_tree || { echo "COMPILE_FAILED"; exit 2; }
  echo "==== 1. the tree rule ========================================================="
  read RC_T F_T _ _ _ _ <<< "$(run /tmp/i1782_tree /tmp/tree.out)"
  cat /tmp/tree.out
  R=$(sed -n "s/^SENSITIVE reach=\([0-9]*\).*/\1/p" /tmp/tree.out)
  C=$(sed -n "s/^SENSITIVE.* clear=\([0-9]*\).*/\1/p" /tmp/tree.out)
  D=$(sed -n "s/^SENSITIVE.* diag=\([0-9]*\).*/\1/p" /tmp/tree.out)
  [ -n "$R" ] && [ -n "$C" ] && [ -n "$D" ] || { echo "FAIL the check did not print its sensitive counts"; exit 1; }

  echo "==== 2. mutation A: the wedge half of the corner threshold doubled ============"
  echo "PREDICTION: exactly $R FAIL lines, all reach: -- the 8 mm dart becomes a corner."
  mutate /tmp/mutA "s#cells.wedgeMm / cells.sigmaWedgeMm <= k;#cells.wedgeMm / cells.sigmaWedgeMm <= 2.0 * k;#" \
         "cells.wedgeMm / cells.sigmaWedgeMm <= k;" || { echo "FAIL mutation A did not land"; exit 1; }
  compile /tmp/mutA/src /tmp/i1782_mutA || { echo "COMPILE_FAILED (mutation A)"; exit 2; }
  read RC_A F_A R_A C_A D_A P_A <<< "$(run /tmp/i1782_mutA /tmp/mutA.out)"
  grep "^FAIL " /tmp/mutA.out

  echo "==== 3. mutation B: no unused camera is promoted ==============================="
  echo "PREDICTION: exactly $C FAIL lines, all clear:."
  mutate /tmp/mutB "s#if (clearCamera >= 0 \&\& clearAt > 0)#if (false \&\& clearCamera >= 0 \&\& clearAt > 0)#" \
         "if (clearCamera >= 0 && clearAt > 0)" || { echo "FAIL mutation B did not land"; exit 1; }
  compile /tmp/mutB/src /tmp/i1782_mutB || { echo "COMPILE_FAILED (mutation B)"; exit 2; }
  read RC_B F_B R_B C_B D_B P_B <<< "$(run /tmp/i1782_mutB /tmp/mutB.out)"
  grep "^FAIL " /tmp/mutB.out

  echo "==== 4. mutation C: the diagonal is never a cell =============================="
  echo "PREDICTION: exactly $((D + C)) FAIL lines -- the $D diag: and the $C clear: -- and no reach: or pure:."
  mutate /tmp/mutC "s#{cells.diagonal, 1.0 - pa - pb + both}#{std::string(), 1.0 - pa - pb + both}#" \
         "{cells.diagonal, 1.0 - pa - pb + both}" || { echo "FAIL mutation C did not land"; exit 1; }
  compile /tmp/mutC/src /tmp/i1782_mutC || { echo "COMPILE_FAILED (mutation C)"; exit 2; }
  read RC_C F_C R_C C_C D_C P_C <<< "$(run /tmp/i1782_mutC /tmp/mutC.out)"
  grep "^FAIL " /tmp/mutC.out

  echo
  echo "tree rc=$RC_T fails=$F_T sensitive reach=$R clear=$C diag=$D"
  echo "mutA rc=$RC_A fails=$F_A predicted=$R (reach=$R_A clear=$C_A diag=$D_A pure=$P_A)"
  echo "mutB rc=$RC_B fails=$F_B predicted=$C (reach=$R_B clear=$C_B diag=$D_B pure=$P_B)"
  echo "mutC rc=$RC_C fails=$F_C predicted=$((D + C)) (reach=$R_C clear=$C_C diag=$D_C pure=$P_C)"
  [ "$RC_T" -eq 0 ] && [ "$F_T" -eq 0 ] || { echo "FAIL the tree rule did not hold"; exit 1; }
  [ "$RC_A" -ne 0 ] && [ "$F_A" -eq "$R" ] && [ "$R_A" -eq "$R" ] || { echo "FAIL mutation A: $F_A fails, $R_A reach:, predicted $R"; exit 1; }
  [ "$RC_B" -ne 0 ] && [ "$F_B" -eq "$C" ] && [ "$C_B" -eq "$C" ] || { echo "FAIL mutation B: $F_B fails, $C_B clear:, predicted $C"; exit 1; }
  [ "$RC_C" -ne 0 ] && [ "$F_C" -eq $((D + C)) ] && [ "$D_C" -eq "$D" ] && [ "$C_C" -eq "$C" ] || { echo "FAIL mutation C: $F_C fails ($D_C diag:, $C_C clear:), predicted $((D + C))"; exit 1; }
  echo "ALL FOUR HELD"
  exit 0
'
exit $?
