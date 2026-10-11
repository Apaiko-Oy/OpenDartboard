#!/bin/bash
# turnaus#1779: a geometric publish from fewer lines than cameras names, at INFO, the camera
# that offered no line and its exclusion. One compile of the tree plus three of a MUTATED
# SCRATCH COPY of entry_intersection.hpp, in i1783_check.sh's shape and for its reason: the
# tree is mounted read-only and never written; each mutation is a replacement in a copy under
# the container's /tmp, and must find its needle exactly once.
#
#   1. the tree          i1779_lines_check -- must pass whole
#   2. unsolved speaks   `!sol.solved ||` taken out of the guard -- PREDICTION: exactly 2
#                        FAILs, `unsolved:` and `too-few:` (both are TOO-FEW refusals with
#                        fewer lines than cameras, so both would print the line)
#   3. wrong camera      the list numbers cameras 0-based (`con.camera` for `con.camera + 1`)
#                        -- PREDICTION: exactly 3 FAILs, `live:`, `solver:` and `too-few:` (the
#                        TOO-FEW story shares the list, which is what the hoist promises)
#   4. the liar unsaid   the list skips a line the solver excluded (`con.usable` for
#                        `con.usable && !con.excluded`) -- PREDICTION: exactly 1 FAIL,
#                        `solver:` (the list comes back empty and nothing is printed)
#
# The script ends on `exit`, never on an `echo`: #1463, #1479.
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"

od_run "1779-check" --network none \
  -v "$OD_TREE_ROOT":/app:ro -w /app "$OD_IMAGE" bash -c '
  set -u
  HDR=src/detector/geometry/detection/entry_intersection.hpp
  compile() { # $1 include root for src, $2 output
    g++ -std=c++17 -O1 -Wall -Wextra -I "$1" -I "$1/utils" \
        -o "$2" testers/i1779_lines_check.cpp "$1/detector/geometry/calibration/wire_model.cpp" \
        $(pkg-config --cflags --libs opencv4)
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
    local name=$1 needle=$2 repl=$3; shift 3
    echo "==== mutation $name ==========================================================="
    echo "PREDICTION: fails, with exactly $# FAIL line(s): $*"
    mutate "/tmp/$name" "$needle" "$repl" || { echo "FAIL mutation $name did not land"; return 1; }
    compile "/tmp/$name/src" "/tmp/b_$name" || { echo "COMPILE_FAILED ($name)"; return 1; }
    "/tmp/b_$name" < /dev/null > "/tmp/$name.out" 2>&1
    local rc=$? fails named=0 l
    cat "/tmp/$name.out"
    # "^FAIL " with the space: FAILURES: n would match "^FAIL" (#1556).
    fails=$(grep -c "^FAIL " "/tmp/$name.out")
    for l in "$@"; do named=$((named + $(grep -c "^FAIL $l" "/tmp/$name.out"))); done
    echo "mutation $name rc=$rc fails=$fails named=$named"
    [ $rc -ne 0 ] && [ "$fails" -eq $# ] && [ "$named" -eq $# ] ||
      { echo "FAIL mutation $name did not fail as predicted"; return 1; }
  }

  compile src /tmp/b_tree || { echo "COMPILE_FAILED"; exit 2; }
  echo "==== 1. the tree =============================================================="
  /tmp/b_tree < /dev/null
  [ $? -eq 0 ] || { echo "FAIL the tree did not hold"; exit 1; }

  BAD=0
  run_mutation unsolved "        if (!sol.solved || sol.usableConstraints >= sol.offeredConstraints)" \
    "        if (sol.usableConstraints >= sol.offeredConstraints)" "unsolved:" "too-few:" || BAD=1
  run_mutation wrongcam "                     std::to_string(con.camera + 1) + \": \" + con.exclusion;" \
    "                     std::to_string(con.camera) + \": \" + con.exclusion;" "live:" "solver:" "too-few:" || BAD=1
  run_mutation liar "                if (con.usable && !con.excluded)
                {
                    continue;
                }
                s += " "                if (con.usable)
                {
                    continue;
                }
                s += " "solver:" || BAD=1
  [ $BAD -eq 0 ] || exit 1
  echo "ALL FOUR HELD"
  exit 0
'
exit $?
