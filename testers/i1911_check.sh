#!/bin/bash
# turnaus#1911: OD_STRAIGHT_PX's widened sigma and the I1911 REFUSALS words, held pure. One
# compile of the tree plus four of a MUTATED SCRATCH COPY of the two headers, in
# i1783_check.sh's shape and for its reason: the tree is mounted read-only and never written;
# each mutation is a replacement in a copy under the container's /tmp, and its needle must
# occur exactly once before and not at all after.
#
#   1. the tree      i1911_sigma_check -- must pass whole
#   2. flat          straightSigmaScale returns 1.0: the widening is gone -- PREDICTION: exactly
#                    5 FAILs, `scale:` 1, `sigma:` 2 (the doubled sigmaPerp and the wider
#                    ellipse), `weight:` 1, `census:` 1 (scale=1.50 reads 1.00)
#   3. unweighed     the factor computed and never applied to sigmaPerp -- PREDICTION: exactly
#                    3 FAILs, `sigma:` 2 and `weight:` 1 (the arithmetic and the words hold)
#   4. counted twice a widened line counted as two constraints -- PREDICTION: exactly 1 FAIL,
#                    `count:` (#1505's phantom and #1535's re-report are what that guards)
#   5. default moved unset OD_STRAIGHT_PX reads 3.0 -- PREDICTION: exactly 2 FAILs, both `gate:`
#
# Then testers/i1911_census.py --self-test, on the host.
#
# The script ends on `exit`, never on an `echo`: #1463, #1479.
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"

if [ ! -d "$OD_TREE_ROOT/build/_deps" ]; then
  echo "i1911_check: $OD_TREE_ROOT/build/_deps is not there; build this worktree first" >&2
  exit 2
fi

od_run "1911-check" --network none \
  -v "$OD_TREE_ROOT":/app:ro -w /app "$OD_IMAGE" bash -c '
  set -u
  AXIS=src/detector/geometry/detection/shaft_axis.hpp
  ENTRY=src/detector/geometry/detection/entry_intersection.hpp
  compile() { # $1 include root for src, $2 output
    g++ -std=c++17 -O1 -Wall -Wextra -I "$1" -I "$1/utils" \
        -I build/_deps/nlohmann_json-src/include -I build/_deps/httplib-src \
        -o "$2" testers/i1911_sigma_check.cpp "$1/detector/geometry/calibration/wire_model.cpp" \
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
    "/tmp/b_$name" < /dev/null > "/tmp/$name.out" 2>&1
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
  /tmp/b_tree < /dev/null
  [ $? -eq 0 ] || { echo "FAIL the tree did not hold"; exit 1; }

  BAD=0
  run_mutation flat "$AXIS" "        return rmsPx / kStraightBasePx;" "        return 1.0;" \
    5 "scale:=1" "sigma:=2" "weight:=1" "census:=1" || BAD=1
  run_mutation unweighed "$ENTRY" "con.sigmaPerpMm = con.sigmaScale * std::sqrt(" "con.sigmaPerpMm = std::sqrt(" \
    3 "sigma:=2" "weight:=1" || BAD=1
  run_mutation counted_twice "$ENTRY" "        if ((int)usable.size() < 2)" \
    "        if ((int)usable.size() + ((usable.size() == 1 && usable[0]->sigmaScale > 1.0) ? 1 : 0) < 2)" \
    1 "count:=1" || BAD=1
  run_mutation default_moved "$AXIS" "            return kStraightBasePx;
        }
        char *end" "            return 3.0;
        }
        char *end" 2 "gate:=2" || BAD=1
  [ $BAD -eq 0 ] || exit 1
  echo "ALL FIVE HELD"
  exit 0
'
RC=$?
# The census reader's own parse, on planted lines of both kinds (the I1911 line and the
# PATH/LINES stories every older log carries). Host python, no container: it reads text.
python3 -I "$(dirname "${BASH_SOURCE[0]}")/i1911_census.py" --self-test || RC=1
exit $RC
