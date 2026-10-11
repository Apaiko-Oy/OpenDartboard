#!/bin/bash
# turnaus#1820: a takeout that reconciled CLEAN with darts still in the board, and the board
# publishing them again as they were pulled. One compile of the tree plus seven of a MUTATED
# SCRATCH COPY of dart_processing.hpp, in i1781_check.sh's shape and for its reason: the tree
# is mounted read-only and never written; each mutation is made on a copy under the
# container's /tmp, and its needle must be found exactly once.
#
# PREDICTIONS, written before the mutations were first run. Each names every FAIL line the
# mutation turns red, and nothing else may go red:
#
#   1. the tree      i1820_reread_check -- must pass whole
#   2. radius down   12 px -> 5 px: the S13 is 9.2 px from the S4 --
#                    `radius: live`, `sequence: 00:57 S13 held`                         (2)
#   3. radius up     12 px -> 60 px: 01:23:06's real S5 is 50.6 px from the visit before --
#                    `radius: real`, `sequence: 01:23 S5 stands`                        (2)
#   4. horizon down  6000 ms -> 4000 ms: the two second pulls came 4,494 and 4,292 ms after
#                    their ENDs -- `horizon: live`, `sequence: 00:57 S13 held`,
#                    `sequence: 01:00 T4 held`                                          (3)
#   5. horizon up    6000 ms -> 8000 ms: the two merged throws came 7,276 and 7,840 ms after
#                    theirs -- `horizon: merged`, `sequence: 00:56 T13 stands`,
#                    `sequence: 00:59 D20 stands`                                       (3)
#   6. arming        any END arms, not only a reversion END: the thrower's S1 1.4 px from
#                    the visit before, after an END under the ceiling -- `arming: grouping` (1)
#   7. disarm        a published dart no longer disarms the hold -- `disarm: sequence`  (1)
#   8. camera        a pixel is compared across cameras -- `camera: another camera`      (1)
#
# The script ends on `exit`, never on an `echo`: #1463, #1479.
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"

od_run "1820-check" --network none \
  -v "$OD_TREE_ROOT":/app:ro -w /app "$OD_IMAGE" bash -c '
  set -u
  HDR=src/detector/geometry/detection/dart_processing.hpp
  compile() { # $1 include root for src, $2 output
    g++ -std=c++17 -O1 -Wall -Wextra -I "$1" -I "$1/utils" \
        -o "$2" testers/i1820_reread_check.cpp $(pkg-config --cflags --libs opencv4)
  }
  mutate() { # $1 scratch dir, $2 needle (may span lines; must occur once), $3 replacement
    rm -rf "$1"; mkdir -p "$1"; cp -r src "$1/src"
    python3 - "$1/$HDR" "$2" "$3" <<PY
import sys
p, a, b = sys.argv[1:4]
a = a.replace("\\\\n", "\n"); b = b.replace("\\\\n", "\n")
s = open(p).read()
n = s.count(a)
print("mutation in %s: needle found %d time(s)" % (p, n))
if n != 1:
    sys.exit(1)
open(p, "w").write(s.replace(a, b))
PY
  }
  run_mutation() { # $1 name, $2 needle, $3 replacement, $4 count, $5.. the labels predicted red
    local name="$1" needle="$2" repl="$3" want="$4"; shift 4
    echo "==== mutation $name ==========================================================="
    echo "PREDICTION: fails, with exactly $want FAIL line(s): $*"
    mutate "/tmp/$name" "$needle" "$repl" || { echo "FAIL mutation $name did not land"; return 1; }
    compile "/tmp/$name/src" "/tmp/b_$name" || { echo "COMPILE_FAILED ($name)"; return 1; }
    "/tmp/b_$name" < /dev/null > "/tmp/$name.out" 2>&1
    local rc=$? fails named=0 label
    cat "/tmp/$name.out"
    # "^FAIL " with the space: FAILURES: n would match "^FAIL" (#1556).
    fails=$(grep -c "^FAIL " "/tmp/$name.out")
    for label in "$@"; do
      named=$((named + $(grep -c -F "FAIL $label" "/tmp/$name.out")))
    done
    echo "mutation $name rc=$rc fails=$fails named=$named"
    [ $rc -ne 0 ] && [ "$fails" -eq "$want" ] && [ "$named" -eq "$want" ] ||
      { echo "FAIL mutation $name did not fail as predicted"; return 1; }
  }

  compile src /tmp/b_tree || { echo "COMPILE_FAILED"; exit 2; }
  echo "==== 1. the tree rule ========================================================="
  /tmp/b_tree < /dev/null
  [ $? -eq 0 ] || { echo "FAIL the tree rule did not hold"; exit 1; }

  BAD=0
  run_mutation radius_dn  "        return 12.0;" "        return 5.0;"  2 \
    "radius: live" "sequence: 00:57 S13 held" || BAD=1
  run_mutation radius_up  "        return 12.0;" "        return 60.0;" 2 \
    "radius: real" "sequence: 01:23 S5 stands" || BAD=1
  run_mutation horizon_dn "        return 6000;" "        return 4000;" 3 \
    "horizon: live" "sequence: 00:57 S13 held" "sequence: 01:00 T4 held" || BAD=1
  run_mutation horizon_up "        return 6000;" "        return 8000;" 3 \
    "horizon: merged" "sequence: 00:56 T13 stands" "sequence: 00:59 D20 stands" || BAD=1
  run_mutation arming "armed = by_reversion && !closed.empty();" "armed = !closed.empty();" 1 \
    "arming: grouping" || BAD=1
  run_mutation disarm "            visit.push_back(p);\\n            armed = false;" \
    "            visit.push_back(p);" 1 "disarm: sequence" || BAD=1
  run_mutation camera "p.camera != camera || " "" 1 "camera: another camera" || BAD=1
  [ $BAD -eq 0 ] || exit 1
  echo "ALL EIGHT HELD"
  exit 0
'
exit $?
