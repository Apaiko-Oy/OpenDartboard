#!/bin/bash
# turnaus#1802: a body at the board, carried as three darts by the rim-only votes. One compile of
# the tree plus five of a MUTATED SCRATCH COPY of dart_processing.hpp, in i1781_check.sh's shape
# and for its reason: the tree is mounted read-only and never written; each mutation is a
# replacement on a copy under the container's /tmp, and must find its needle exactly once.
#
#   1. the tree      i1802_rimbody_check -- must pass whole
#   2. share up      the rim figure raised to #1781's 15% -- PREDICTION: exactly 2 FAIL lines,
#                    `usable: live 01:02:10` and `share: live 01:02:08` (camera 2's 25,835 px
#                    is 11.96% of its board, so #1781's own figure holds neither phantom)
#   3. share down    the figure lowered to 9% -- PREDICTION: exactly 1 FAIL, `share: fixture`
#                    (the largest fixture dart is 9.96% of its board); the surround dart (about
#                    5%) still stands
#   4. any clearing  the usable-line condition dropped, so a camera that cleared the scoring
#                    floor `not straight` keeps the dart -- PREDICTION: exactly 2 FAIL lines,
#                    `pure: a camera that cleared the floor with no usable line` and
#                    `usable: live 01:02:10`
#   5. no line kept  the usable-line exit removed, so a camera on a usable line keeps nothing --
#                    PREDICTION: exactly 2 FAIL lines, `pure: one camera on a usable line` and
#                    `usable: fixture` (#1821's S13)
#   6. not rim       the rim-carried condition removed -- PREDICTION: exactly 2 FAIL lines,
#                    `pure: the same figures in a window the rim did not carry` and
#                    `rim: scoring`
#
# The script ends on `exit`, never on an `echo`: #1463, #1479.
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"

od_run "1802-check" --network none \
  -v "$OD_TREE_ROOT":/app:ro -w /app "$OD_IMAGE" bash -c '
  set -u
  HDR=src/detector/geometry/detection/dart_processing.hpp
  compile() { # $1 include root for src, $2 output
    g++ -std=c++17 -O1 -Wall -Wextra -I "$1" -I "$1/utils" \
        -o "$2" testers/i1802_rimbody_check.cpp $(pkg-config --cflags --libs opencv4)
  }
  mutate() { # $1 scratch dir, $2 needle (fixed string, must occur once), $3 replacement
    rm -rf "$1"; mkdir -p "$1"; cp -r src "$1/src"
    local before after
    before=$(grep -c -F -- "$2" "$1/$HDR")
    python3 - "$1/$HDR" "$2" "$3" <<PY
import sys
p, a, b = sys.argv[1:4]
s = open(p).read()
open(p, "w").write(s.replace(a, b))
PY
    after=$(grep -c -F -- "$2" "$1/$HDR")
    echo "mutation in $1: needle found $before time(s) before, $after after"
    [ "$before" -eq 1 ] && [ "$after" -eq 0 ]
  }
  run_mutation() { # $1 name, $2 needle, $3 replacement, $4.. the labels predicted red
    local name="$1" needle="$2" repl="$3"; shift 3
    echo "==== mutation $name ==========================================================="
    echo "PREDICTION: fails, with exactly $# FAIL line(s):"
    local l; for l in "$@"; do echo "  $l"; done
    mutate "/tmp/$name" "$needle" "$repl" || { echo "FAIL mutation $name did not land"; return 1; }
    compile "/tmp/$name/src" "/tmp/b_$name" || { echo "COMPILE_FAILED ($name)"; return 1; }
    "/tmp/b_$name" < /dev/null > "/tmp/$name.out" 2>&1
    local rc=$? fails named=0
    cat "/tmp/$name.out"
    # "^FAIL " with the space: FAILURES: n would match "^FAIL" (#1556).
    fails=$(grep -c "^FAIL " "/tmp/$name.out")
    for l in "$@"; do
      [ "$(grep -c -F -- "FAIL $l" "/tmp/$name.out")" -eq 1 ] && named=$((named + 1))
    done
    echo "mutation $name rc=$rc fails=$fails named=$named"
    [ $rc -ne 0 ] && [ "$fails" -eq $# ] && [ "$named" -eq $# ] ||
      { echo "FAIL mutation $name did not fail as predicted"; return 1; }
  }

  compile src /tmp/b_tree || { echo "COMPILE_FAILED"; exit 2; }
  echo "==== 1. the tree rule ========================================================="
  /tmp/b_tree < /dev/null
  RC_TREE=$?
  [ $RC_TREE -eq 0 ] || { echo "FAIL the tree rule did not hold"; exit 1; }

  BAD=0
  LINE="            if (v.cleared_scoring_floor && v.axis_usable)"
  run_mutation share_up   "        return 11.0;" "        return 15.0;" \
    "usable: live 01:02:10" "share: live 01:02:08" || BAD=1
  run_mutation share_down "        return 11.0;" "        return 9.0;" \
    "share: fixture" || BAD=1
  run_mutation any_clear  "$LINE" "            if (v.cleared_scoring_floor)" \
    "pure: a camera that cleared the floor with no usable line" "usable: live 01:02:10" || BAD=1
  run_mutation no_line    "$LINE" "            if (false && v.cleared_scoring_floor && v.axis_usable)" \
    "pure: one camera on a usable line" "usable: fixture" || BAD=1
  run_mutation not_rim    "        if (!rim_carried)" "        if (false && !rim_carried)" \
    "pure: the same figures in a window the rim did not carry" "rim: scoring" || BAD=1
  [ $BAD -eq 0 ] || exit 1
  echo "ALL SIX HELD"
  exit 0
'
exit $?
