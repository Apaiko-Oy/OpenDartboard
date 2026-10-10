#!/bin/bash
# turnaus#1781: the takeout's arm, refused as a dart. One compile of the tree plus four of a
# MUTATED SCRATCH COPY of dart_processing.hpp, in i1773_check.sh's shape and for its reason:
# the tree is mounted read-only and never written; each mutation is a sed on a copy under
# the container's /tmp, and must find its needle exactly once.
#
#   1. the tree          i1781_body_check -- must pass whole
#   2. size up           the body figure raised to 30% of a board -- PREDICTION: fails with
#                        exactly 1 FAIL line, `size: live` (the D11 arm is at least 20.3% of
#                        camera 2's board, so it is no longer body-sized); nothing pure: or after: moves
#   3. size down         the figure lowered to 3% -- PREDICTION: exactly 1 FAIL, `size:
#                        fixture` (the largest fixture dart is 9.96% of its board)
#   4. after down        the horizon cut to 900 ms -- PREDICTION: exactly 1 FAIL, `after:
#                        live` (the S2 came 976 ms after the END)
#   5. after up          the horizon raised to 5000 ms, past the soonest fixture first dart
#                        after a reversion END (4433 ms) --
#                        PREDICTION: exactly 1 FAIL, `after: fixture`
#
# The script ends on `exit`, never on an `echo`: #1463, #1479.
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"

od_run "1781-check" --network none \
  -v "$OD_TREE_ROOT":/app:ro -w /app "$OD_IMAGE" bash -c '
  set -u
  HDR=src/detector/geometry/detection/dart_processing.hpp
  compile() { # $1 include root for src, $2 output
    g++ -std=c++17 -O1 -Wall -Wextra -I "$1" -I "$1/utils" \
        -o "$2" testers/i1781_body_check.cpp $(pkg-config --cflags --libs opencv4)
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
  run_mutation() { # $1 name, $2 needle, $3 replacement, $4 the one label predicted red
    echo "==== mutation $1 ==========================================================="
    echo "PREDICTION: fails, with exactly 1 FAIL line, and it is \`$4\`"
    mutate "/tmp/$1" "$2" "$3" || { echo "FAIL mutation $1 did not land"; return 1; }
    compile "/tmp/$1/src" "/tmp/b_$1" || { echo "COMPILE_FAILED ($1)"; return 1; }
    "/tmp/b_$1" < /dev/null > "/tmp/$1.out" 2>&1
    local rc=$? fails named
    cat "/tmp/$1.out"
    # "^FAIL " with the space: FAILURES: n would match "^FAIL" (#1556).
    fails=$(grep -c "^FAIL " "/tmp/$1.out")
    named=$(grep -c "^FAIL $4" "/tmp/$1.out")
    echo "mutation $1 rc=$rc fails=$fails named=$named"
    [ $rc -ne 0 ] && [ "$fails" -eq 1 ] && [ "$named" -eq 1 ] ||
      { echo "FAIL mutation $1 did not fail as predicted"; return 1; }
  }

  compile src /tmp/b_tree || { echo "COMPILE_FAILED"; exit 2; }
  echo "==== 1. the tree rule ========================================================="
  /tmp/b_tree < /dev/null
  RC_TREE=$?
  [ $RC_TREE -eq 0 ] || { echo "FAIL the tree rule did not hold"; exit 1; }

  BAD=0
  run_mutation size_up   "        return 15.0;" "        return 30.0;" "size: live"     || BAD=1
  run_mutation size_down "        return 15.0;" "        return 3.0;"  "size: fixture"  || BAD=1
  run_mutation after_dn  "        return 1500;" "        return 900;"  "after: live"    || BAD=1
  run_mutation after_up  "        return 1500;" "        return 5000;" "after: fixture" || BAD=1
  [ $BAD -eq 0 ] || exit 1
  echo "ALL FIVE HELD"
  exit 0
'
exit $?
