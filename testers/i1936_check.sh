#!/bin/bash
# turnaus#1936: OD_TREBLE_BORROW=on, a camera with no inner treble edge of its own judging the
# solved entry at the median of the edges the other cameras measured, held pure. One compile of
# the tree plus three of a MUTATED SCRATCH COPY of board_model.hpp, i1908_check.sh's shape and
# for its reason: the tree is mounted read-only and never written; each mutation is a
# replacement in a copy under the container's /tmp, and its needle must occur exactly once
# before and not at all after.
#
#   1. the tree        i1936_borrow_check -- must pass whole: a synthetic board whose camera A
#                      traces no treble ring and whose camera B sees the inner edge 2.5 mm
#                      inward; trebles planted at 97.0/98.0/98.9 mm read S1 through A with the
#                      switch off and T1 with it on; 95.5/100.0/106.5 mm unmoved either way; the
#                      outer edge never borrowed; measured and anti-bloom cameras borrow nothing
#   2. borrow ignored  trebleBloomAdjustMm never returns the borrowed edge (the switch does
#                      nothing) -- PREDICTION: exactly 3 FAILs, `issue:` 3 (the three planted
#                      trebles read S1 with the switch on)
#   3. default moved   unset OD_TREBLE_BORROW reads on -- PREDICTION: exactly 2 FAILs, `parse:` 2
#   4. measured borrow a camera that measured its own edge borrows too -- PREDICTION: exactly 3
#                      FAILs, `measured:` 1, `median:` 1 (three fits borrowed, not one) and
#                      `refused:` 1 (the anti-bloom camera borrows B's edge)
#
# The script ends on `exit`, never on an `echo`: #1463, #1479.
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"

od_run "1936-check" --network none \
  -v "$OD_TREE_ROOT":/app:ro -w /app "$OD_IMAGE" bash -c '
  set -u
  HDR=src/detector/geometry/calibration/board_model.hpp
  compile() { # $1 include root for src, $2 output
    g++ -std=c++17 -O1 -Wall -Wextra -I "$1" -I "$1/utils" -I "$1/detector/geometry/calibration" \
        -o "$2" testers/i1936_borrow_check.cpp "$1/detector/geometry/calibration/wire_model.cpp" \
        $(pkg-config --cflags --libs opencv4)
  }
  mutate() { # $1 scratch dir, $2 header, $3 needle (exactly once), $4 replacement
    rm -rf "$1"; mkdir -p "$1"; cp -r src "$1/src"
    python3 - "$1/$2" "$3" "$4" <<PY
import sys
p, a, b = sys.argv[1:4]
s = open(p).read()
before = s.count(a)
s = s.replace(a, b)
after = s.count(a)
open(p, "w").write(s)
print("mutation in %s: needle found %d time(s) before, %d after" % (p, before, after))
sys.exit(0 if (before == 1 and after == 0) else 1)
PY
  }
  run_mutation() { # $1 name, $2 header, $3 needle, $4 replacement, $5 total FAILs, $6.. label:count
    local name=$1 hdr=$2 needle=$3 repl=$4 total=$5; shift 5
    echo "==== mutation $name =========================================================="
    echo "PREDICTION: fails, with exactly $total FAIL line(s): $*"
    mutate "/tmp/$name" "$hdr" "$needle" "$repl" || { echo "FAIL mutation $name did not land"; return 1; }
    compile "/tmp/$name/src" "/tmp/b_$name" || { echo "COMPILE_FAILED ($name)"; return 1; }
    env -u OD_TREBLE_BORROW "/tmp/b_$name" < /dev/null > "/tmp/$name.out" 2>&1
    local rc=$? fails ok=1
    cat "/tmp/$name.out"
    # "^FAIL " with the space: FAILURES: n would match "^FAIL" (#1556).
    fails=$(grep -c "^FAIL " "/tmp/$name.out")
    for want in "$@"; do
      local label=${want%%=*} n=${want##*=} got
      got=$(grep -c "^FAIL $label" "/tmp/$name.out")
      echo "mutation $name label $label predicted=$n got=$got"
      [ "$got" -eq "$n" ] || ok=0
    done
    echo "mutation $name rc=$rc fails=$fails"
    [ $rc -ne 0 ] && [ "$fails" -eq "$total" ] && [ $ok -eq 1 ] ||
      { echo "FAIL mutation $name did not fail as predicted"; return 1; }
  }

  compile src /tmp/b_tree || { echo "COMPILE_FAILED"; exit 2; }
  echo "==== 1. the tree ============================================================="
  env -u OD_TREBLE_BORROW /tmp/b_tree < /dev/null
  [ $? -eq 0 ] || { echo "FAIL the tree did not hold"; exit 1; }

  BAD=0
  run_mutation borrow_ignored "$HDR" \
    "            return ring == ellipse_processing::kInnerTriple ? fit.borrowedInnerTrebleMm : 0.0;" \
    "            return 0.0 * fit.borrowedInnerTrebleMm;" \
    3 "issue:=3" || BAD=1
  run_mutation default_moved "$HDR" \
    "        return value != nullptr && std::string(value) == \"on\";" \
    "        return value == nullptr || std::string(value) == \"on\";" \
    2 "parse:=2" || BAD=1
  run_mutation measured_borrow "$HDR" \
    "            if (!fits[i].planeBuilt || trebleEdgeMeasured(fits[i], kIn))" \
    "            if (!fits[i].planeBuilt)" \
    3 "measured:=1" "median:=1" "refused:=1" || BAD=1
  [ $BAD -eq 0 ] || exit 1
  echo "ALL FOUR HELD"
  exit 0
'
exit $?
