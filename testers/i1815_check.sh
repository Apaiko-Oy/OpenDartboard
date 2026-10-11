#!/bin/bash
# turnaus#1815: six in seven flagged darts were right as published. Under OD_FLAG_SIGMAS=<K>
# (0 < K < 1, default off) a geometric flag is kept only where the solve is within K of its
# own across-wire sigmas of the wire; the rest publish clear and no score moves. One compile
# of the tree plus three of a MUTATED SCRATCH COPY of the header, in i1782_check.sh's shape
# and for its reason: the measurement is the set of runs on one tree. The mutations are made
# with sed on a copy of src/ under the container's /tmp; the tree is mounted and never written.
#
#   1. the tree               i1815_flag_check -- must pass whole and print SENSITIVE
#                             edge= keep= cut= parse= pure= (what each mutation can reach)
#   2. mutation A: the edge   the keep comparison reads `<= k - 0.2` (K = 0.7 acts as 0.5) --
#                             PREDICTION STATED FIRST: exactly the edge: assertions fail
#                             (board 20's S20/T20 at z 0.524 and rig-20260929 dev v9.3 at
#                             0.569, the two real true flags between 0.5 and 0.7), no other.
#   3. mutation B: no cut     a flag beyond K stays flagged -- exactly the cut: assertions
#                             fail, and no other: nothing kept or parsed moves.
#   4. mutation C: loosening  the parse's upper bound is gone, so 1.5 is read as 1.5 --
#                             exactly one parse: assertion fails (the 1.5 one), and no other:
#                             tightenBoundaryCall's own k < 1 guard still leaves K >= 1 alone.
#
# It links no extra translation unit: the rule is over primitives, in score_processing.hpp.
# The script ends on `exit`, never on an `echo`: #1463, #1479.
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"

if [ ! -d "$OD_TREE_ROOT/build/_deps" ]; then
  echo "i1815_check: $OD_TREE_ROOT/build/_deps is not there; build this worktree first" >&2
  exit 2
fi

od_run "1815-check" --network none \
  -v "$OD_TREE_ROOT":/app -w /app "$OD_IMAGE" bash -c '
  set -u
  HDR=src/detector/geometry/detection/score_processing.hpp
  compile() { # $1 include root for src, $2 output
    g++ -std=c++17 -O1 -Wall -Wextra \
        -I "$1" -I "$1/utils" \
        -I build/_deps/nlohmann_json-src/include \
        -I build/_deps/httplib-src \
        -o "$2" testers/i1815_flag_check.cpp \
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
  run() { # $1 binary, $2 output; prints "rc fails edge keep cut parse pure"
    "$1" < /dev/null > "$2" 2>&1
    echo "$? $(grep -c "^FAIL " "$2") $(grep -c "^FAIL edge:" "$2") $(grep -c "^FAIL keep:" "$2") $(grep -c "^FAIL cut:" "$2") $(grep -c "^FAIL parse:" "$2") $(grep -c "^FAIL pure:" "$2")"
  }

  compile src /tmp/i1815_tree || { echo "COMPILE_FAILED"; exit 2; }
  echo "==== 1. the tree rule ========================================================="
  read RC_T F_T _ _ _ _ _ <<< "$(run /tmp/i1815_tree /tmp/tree.out)"
  cat /tmp/tree.out
  E=$(sed -n "s/^SENSITIVE edge=\([0-9]*\).*/\1/p" /tmp/tree.out)
  C=$(sed -n "s/^SENSITIVE.* cut=\([0-9]*\).*/\1/p" /tmp/tree.out)
  [ -n "$E" ] && [ -n "$C" ] || { echo "FAIL the check did not print its sensitive counts"; exit 1; }

  echo "==== 2. mutation A: K acts 0.2 lower ==========================================="
  echo "PREDICTION: exactly $E FAIL lines, all edge: -- the two true flags between z 0.5 and 0.7."
  mutate /tmp/mutA "s#crossingSigmas < 0.0 || crossingSigmas <= k)#crossingSigmas < 0.0 || crossingSigmas <= k - 0.2)#" \
         "crossingSigmas < 0.0 || crossingSigmas <= k)" || { echo "FAIL mutation A did not land"; exit 1; }
  compile /tmp/mutA/src /tmp/i1815_mutA || { echo "COMPILE_FAILED (mutation A)"; exit 2; }
  read RC_A F_A E_A K_A C_A P_A U_A <<< "$(run /tmp/i1815_mutA /tmp/mutA.out)"
  grep "^FAIL " /tmp/mutA.out

  echo "==== 3. mutation B: a flag beyond K stays flagged =============================="
  echo "PREDICTION: exactly $C FAIL lines, all cut:."
  mutate /tmp/mutB "s#out.flagged = false;#out.flagged = true;#" \
         "out.flagged = false;" || { echo "FAIL mutation B did not land"; exit 1; }
  compile /tmp/mutB/src /tmp/i1815_mutB || { echo "COMPILE_FAILED (mutation B)"; exit 2; }
  read RC_B F_B E_B K_B C_B P_B U_B <<< "$(run /tmp/i1815_mutB /tmp/mutB.out)"
  grep "^FAIL " /tmp/mutB.out

  echo "==== 4. mutation C: the parse accepts a K above 1 =============================="
  echo "PREDICTION: exactly 1 FAIL line, parse: (1.5 read as 1.5), and no other."
  mutate /tmp/mutC "s#|| !(k > 0.0) || !(k < 1.0))#|| !(k > 0.0))#" \
         "|| !(k > 0.0) || !(k < 1.0))" || { echo "FAIL mutation C did not land"; exit 1; }
  compile /tmp/mutC/src /tmp/i1815_mutC || { echo "COMPILE_FAILED (mutation C)"; exit 2; }
  read RC_C F_C E_C K_C C_C P_C U_C <<< "$(run /tmp/i1815_mutC /tmp/mutC.out)"
  grep "^FAIL " /tmp/mutC.out

  echo
  echo "tree rc=$RC_T fails=$F_T sensitive edge=$E cut=$C"
  echo "mutA rc=$RC_A fails=$F_A predicted=$E (edge=$E_A keep=$K_A cut=$C_A parse=$P_A pure=$U_A)"
  echo "mutB rc=$RC_B fails=$F_B predicted=$C (edge=$E_B keep=$K_B cut=$C_B parse=$P_B pure=$U_B)"
  echo "mutC rc=$RC_C fails=$F_C predicted=1 (edge=$E_C keep=$K_C cut=$C_C parse=$P_C pure=$U_C)"
  [ "$RC_T" -eq 0 ] && [ "$F_T" -eq 0 ] || { echo "FAIL the tree rule did not hold"; exit 1; }
  [ "$E" -gt 0 ] && [ "$C" -gt 0 ] || { echo "FAIL a sensitive count is zero, so a mutation would prove nothing"; exit 1; }
  [ "$RC_A" -ne 0 ] && [ "$F_A" -eq "$E" ] && [ "$E_A" -eq "$E" ] || { echo "FAIL mutation A: $F_A fails, $E_A edge:, predicted $E"; exit 1; }
  [ "$RC_B" -ne 0 ] && [ "$F_B" -eq "$C" ] && [ "$C_B" -eq "$C" ] || { echo "FAIL mutation B: $F_B fails, $C_B cut:, predicted $C"; exit 1; }
  [ "$RC_C" -ne 0 ] && [ "$F_C" -eq 1 ] && [ "$P_C" -eq 1 ] || { echo "FAIL mutation C: $F_C fails, $P_C parse:, predicted 1"; exit 1; }
  echo "ALL FOUR HELD"
  exit 0
'
exit $?
