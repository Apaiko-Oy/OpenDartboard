#!/bin/bash
# turnaus#1783: a takeout of surround darts opens its event. One compile of the tree plus four
# of a MUTATED SCRATCH COPY of motion_processing.hpp, in i1781_check.sh's shape and for its
# reason: the tree is mounted read-only and never written; each mutation is a replacement in a
# copy under the container's /tmp, and must find its needle exactly once.
#
#   1. the tree          i1783_region_check -- must pass whole
#   2. rim none          the rim scale set to 1.0, so "to the rim" is the double -- PREDICTION:
#                        exactly 1 FAIL, `rim: surround` (the surround hand is counted nowhere)
#   3. rim wide          the scale set to 1.6, past the board -- PREDICTION: exactly 1 FAIL,
#                        `rim: beyond` (the patch past the rim is counted)
#   4. default to rim    the default count draws the rim too -- PREDICTION: exactly 1 FAIL,
#                        `double:` (the default no longer has the live fault it is measured on)
#   5. rim denominator   the share taken of the rim's area instead of the double's --
#                        PREDICTION: exactly 1 FAIL, `units:` (a dart inside the double becomes
#                        a smaller share when counted to the rim; the surround hand still
#                        clears 0.006 at 2,500 of ~380,000 px, 0.0066)
#
# The script ends on `exit`, never on an `echo`: #1463, #1479.
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"

od_run "1783-check" --network none \
  -v "$OD_TREE_ROOT":/app:ro -w /app "$OD_IMAGE" bash -c '
  set -u
  HDR=src/detector/geometry/detection/motion_processing.hpp
  compile() { # $1 include root for src, $2 output
    g++ -std=c++17 -O1 -Wall -Wextra -I "$1" -I "$1/utils" \
        -o "$2" testers/i1783_region_check.cpp $(pkg-config --cflags --libs opencv4)
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
  run_mutation rim_none  "        return 225.5 / 170.0;" "        return 1.0;" "rim: surround" || BAD=1
  run_mutation rim_wide  "        return 225.5 / 170.0;" "        return 1.6;" "rim: beyond"   || BAD=1
  run_mutation dflt_rim  "        if (!to_rim)"          "        if (false)"  "double:"       || BAD=1
  run_mutation rim_denom "        cv::ellipse(m.count, rim, cv::Scalar(255), cv::FILLED);" \
    "        cv::ellipse(m.count, rim, cv::Scalar(255), -1); m.denominator = cv::countNonZero(m.count);" \
    "units:" || BAD=1
  [ $BAD -eq 0 ] || exit 1
  echo "ALL FIVE HELD"
  exit 0
'
exit $?
