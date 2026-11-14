#!/bin/bash
# turnaus#1997: OD_RING_REFIT, held pure -- and four mutations, each of a scratch copy of src/.
#
# testers/i1997_refit_check.cpp draws a board through a known map, hands fitBoardToCamera
# that board seen wrong the way #1992's census found the rig's cameras wrong, and asks
# ring_refit.hpp to refit the map through the drawn rings and board_model.hpp to score
# through it (thirteen labels, one assertion each; the file's header lists them). Then four
# mutations, each predicted BEFORE it runs:
#
#   not_applied   board_model.hpp computes nothing from the carried map (the refit computed
#                 and not applied): rings:, darts: and bloom: go red (3) -- refit: stays
#                 green, because the refit itself was computed
#   cache_accepts ring_refit.hpp's cacheRefusal lets every setting through (a cache from the
#                 other setting accepted): cache: (1)
#   default_on    unset reads on: parse: and default: (2)
#   any_gap       a ring may be missing any arc: clipped: (1)
#
# Five compiles of the check with wire_model.cpp, in $OD_IMAGE, the tree read-only, no
# network, no detector binary: OD_SKIP_BUILD changes nothing about it.
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"

od_run "1997-refit-check" --network none \
  -v "$OD_TREE_ROOT":/app:ro -w /app "$OD_IMAGE" bash -c '
  set -u
  compile() { # $1 include root for src, $2 output
    g++ -std=c++17 -O1 -I "$1" -I "$1/utils" -I "$1/detector/geometry/calibration" \
        -o "$2" testers/i1997_refit_check.cpp "$1/detector/geometry/calibration/wire_model.cpp" \
        $(pkg-config --cflags --libs opencv4)
  }
  run_mutation() { # $1 name, $2 file under src/, $3 needle, $4 replacement, $5 total FAILs, $6.. labels that must FAIL
    local name=$1 file=$2 needle=$3 repl=$4 total=$5; shift 5
    echo "==== mutation $name =========================================================="
    echo "PREDICTION: fails, with exactly $total FAIL line(s): $*"
    rm -rf "/tmp/$name"; mkdir -p "/tmp/$name"; cp -r src "/tmp/$name/src"
    python3 - "/tmp/$name/src/$file" "$needle" "$repl" <<PY || { echo "FAIL mutation $name did not land"; return 1; }
import sys
p, a, b = sys.argv[1:4]
s = open(p).read()
n = s.count(a)
open(p, "w").write(s.replace(a, b))
print("mutation in %s: needle found %d time(s)" % (p, n))
sys.exit(0 if n == 1 else 1)
PY
    compile "/tmp/$name/src" "/tmp/b_$name" || { echo "COMPILE_FAILED ($name)"; return 1; }
    env -u OD_RING_REFIT "/tmp/b_$name" < /dev/null > "/tmp/$name.out" 2>&1
    local rc=$? fails ok=1 want
    cut -c1-300 "/tmp/$name.out"
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
  env -u OD_RING_REFIT /tmp/b_tree < /dev/null | cut -c1-300
  [ ${PIPESTATUS[0]} -eq 0 ] || { echo "FAIL the tree did not hold"; exit 1; }

  BAD=0
  run_mutation not_applied detector/geometry/calibration/board_model.hpp \
    "fit.ringRefitted = ring_refit::applyTo(calib.ringRefit, fit.plane.H, fit.plane.Hinv);" \
    "fit.ringRefitted = false;" \
    3 rings: darts: bloom: || BAD=1
  run_mutation cache_accepts detector/geometry/calibration/ring_refit.hpp \
    "        if (written == now)" "        if (written == now || written != now)" \
    1 cache: || BAD=1
  run_mutation default_on detector/geometry/calibration/ring_refit.hpp \
    "        if (e != nullptr && std::strcmp(e, \"on\") == 0)" \
    "        if (e == nullptr || std::strcmp(e, \"on\") == 0)" \
    2 parse: default: || BAD=1
  run_mutation any_gap detector/geometry/calibration/ring_refit.hpp \
    "    inline constexpr double kMaxGapDeg = 90.0;" "    inline constexpr double kMaxGapDeg = 400.0;" \
    1 clipped: || BAD=1
  [ $BAD -eq 0 ] && echo "I1997 refit check: the tree green, four mutations red as predicted"
  exit $BAD
' < /dev/null
RC=$?
echo "RUN=1997-refit rc=$RC"
exit $RC
