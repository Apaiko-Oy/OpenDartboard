#!/bin/bash
# turnaus#1992: OD_RING_CENSUS, held pure -- and three mutations of ring_census.hpp.
#
# testers/i1992_ring_check.cpp draws a board through a known map with the treble band 2 mm
# inward all round and the doubles band turned 3 mm with the angle, and asks ring_census.hpp
# to read both back (eight labels, one assertion each; the file's header lists them). Then
# three mutations of a scratch copy of the header, each predicted BEFORE it runs:
#
#   third_height  the edge taken at 0.3 of the band's height, not half: the band reads wider,
#                 so treble:, doubles:, width:, dim: and story: go red (5)
#   sign          the residual subtracted the other way: treble:, dim: and story: (3) -- the
#                 doubles' 0.0 all round and 3.0 turning are the same either way
#   default_on    unset reads on: parse: and default: (2)
#
# One compile of the header per run, in $OD_IMAGE, the tree read-only, no network, no
# detector binary: OD_SKIP_BUILD changes nothing about it.
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"

od_run "1992-ring-check" --network none \
  -v "$OD_TREE_ROOT":/app:ro -w /app "$OD_IMAGE" bash -c '
  set -u
  HDR=src/detector/geometry/calibration/ring_census.hpp
  compile() { # $1 include root for src, $2 output
    g++ -std=c++17 -O1 -Wall -Wextra -I "$1" -I "$1/utils" \
        -o "$2" testers/i1992_ring_check.cpp $(pkg-config --cflags --libs opencv4)
  }
  run_mutation() { # $1 name, $2 needle, $3 replacement, $4 total FAILs, $5.. labels that must FAIL
    local name=$1 needle=$2 repl=$3 total=$4; shift 4
    echo "==== mutation $name =========================================================="
    echo "PREDICTION: fails, with exactly $total FAIL line(s): $*"
    rm -rf "/tmp/$name"; mkdir -p "/tmp/$name/src/detector/geometry/calibration"
    cp "$HDR" "/tmp/$name/$HDR"
    python3 - "/tmp/$name/$HDR" "$needle" "$repl" <<PY || { echo "FAIL mutation $name did not land"; return 1; }
import sys
p, a, b = sys.argv[1:4]
s = open(p).read()
n = s.count(a)
open(p, "w").write(s.replace(a, b))
print("mutation in %s: needle found %d time(s)" % (p, n))
sys.exit(0 if n == 1 else 1)
PY
    compile "/tmp/$name/src" "/tmp/b_$name" || { echo "COMPILE_FAILED ($name)"; return 1; }
    env -u OD_RING_CENSUS "/tmp/b_$name" < /dev/null > "/tmp/$name.out" 2>&1
    local rc=$? fails ok=1 want
    cut -c1-240 "/tmp/$name.out"
    fails=$(grep -c "^FAIL " "/tmp/$name.out")
    for want in "$@"; do
      grep -q "^FAIL $want" "/tmp/$name.out" || { echo "mutation $name: $want did not fail"; ok=0; }
    done
    echo "mutation $name rc=$rc fails=$fails"
    [ $rc -ne 0 ] && [ "$fails" -eq "$total" ] && [ $ok -eq 1 ] ||
      { echo "FAIL mutation $name did not fail as predicted"; return 1; }
    echo "OK   mutation $name turns exactly the predicted $total label(s) red"
  }

  compile src /tmp/b_tree || { echo "COMPILE_FAILED"; exit 2; }
  echo "==== 1. the tree ============================================================="
  env -u OD_RING_CENSUS /tmp/b_tree < /dev/null | cut -c1-240
  [ ${PIPESTATUS[0]} -eq 0 ] || { echo "FAIL the tree did not hold"; exit 1; }

  BAD=0
  run_mutation third_height "    inline constexpr double kHalf = 0.5;" "    inline constexpr double kHalf = 0.3;" \
    5 treble: doubles: width: dim: story: || BAD=1
  run_mutation sign "std::sqrt(X * X + Y * Y) - ringMm[2 * band + e]);" "ringMm[2 * band + e] - std::sqrt(X * X + Y * Y));" \
    3 treble: dim: story: || BAD=1
  run_mutation default_on "        return e != nullptr && std::strcmp(e, \"on\") == 0;" \
    "        return e == nullptr || std::strcmp(e, \"on\") == 0;" \
    2 parse: default: || BAD=1
  [ $BAD -eq 0 ] && echo "I1992 ring check: the tree green, three mutations red as predicted"
  exit $BAD
' < /dev/null
RC=$?
echo "RUN=1992-ringline rc=$RC"
exit $RC
