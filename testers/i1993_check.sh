#!/bin/bash
# turnaus#1993: OD_PLANE_DROP (off unset) and OD_PLANE_CENSUS -- fresh change that agrees in
# the board plane across cameras. testers/i1993_plane_check.cpp holds the assertions; this
# compiles it against the tree, then against five mutated scratch copies of
# plane_agreement.hpp, each PREDICTED to turn named assertions red:
#
#   self-compared      a camera is compared with itself too        9: cross:3 shadow:1 tol:1 self:2 fit:2
#   tolerance-ignored  only the same millimetre cell coincides     1: tol:1
#   default-moved      OD_PLANE_DROP is on unasked                 1: unset:1
#   gates-ignored      a retry is adopted whatever the gates said  1: adopt:1
#   any-refusal        every refused figure is fitted again        2: retry:2
#
# Then i1993_census.py --self-test. Compiles only; no footage.
#
#   testers/run_all.sh 1993-plane
#
# The script ends on `exit`, never on an `echo`: #1463, #1479.
set -u
T="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FAILED=0
. "$T/tester_paths.sh"
od_run "1993-check" --network none -v "$OD_TREE_ROOT":/app:ro -w /app "$OD_IMAGE" bash -c '
  set -u
  PLANE=src/detector/geometry/detection/plane_agreement.hpp
  BAD=0
  compile() { # $1 include root for src, $2 output
    g++ -std=c++17 -O1 -Wall -Wextra -I "$1" -I "$1/utils" \
        -o "$2" testers/i1993_plane_check.cpp $(pkg-config --cflags --libs opencv4)
  }
  mutate() { # $1 scratch dir, $2 file, $3 needle (exactly once), $4 replacement
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
  run_mutation() { # $1 name, $2 needle, $3 replacement, $4 total FAILs, $5.. label:count
    local name=$1 needle=$2 repl=$3 total=$4; shift 4
    echo "==== mutation $name ==========================================================="
    mutate "/tmp/$name" "$PLANE" "$needle" "$repl" || { echo "FAIL mutation $name: needle not exactly once"; BAD=1; return; }
    compile "/tmp/$name/src" "/tmp/b_$name" || { echo "FAIL mutation $name: COMPILE_FAILED"; BAD=1; return; }
    local out; out=$(/tmp/b_$name); echo "$out"
    local n; n=$(printf "%s\n" "$out" | awk "/^FAIL /{n++} END{print n+0}")
    local ok=1
    [ "$n" -eq "$total" ] || { echo "FAIL mutation $name: $n FAIL(s) where $total were predicted"; ok=0; }
    local pair label count got
    for pair in "$@"; do
      label=${pair%%:*}; count=${pair##*:}
      got=$(printf "%s\n" "$out" | awk -v l="$label:" "\$1==\"FAIL\" && \$2==l {n++} END{print n+0}")
      [ "$got" -eq "$count" ] || { echo "FAIL mutation $name: $got \`$label:\` FAIL(s) where $count were predicted"; ok=0; }
    done
    if [ $ok -eq 1 ]; then echo "OK   mutation $name turns exactly the predicted $total red: $*"; else BAD=1; fi
  }
  echo "==== 1. the tree ==============================================================="
  compile src /tmp/b_tree || { echo "FAIL COMPILE_FAILED"; exit 2; }
  /tmp/b_tree || BAD=1
  run_mutation self-compared "                if (j == k || spread[j].empty())" "                if (spread[j].empty())" 9 \
    cross:3 shadow:1 tol:1 self:2 fit:2
  run_mutation tolerance-ignored "        const int reach = std::max(0, (int)std::lround(toleranceMm / detail::kCellMm));" \
    "        const int reach = 0 * (int)toleranceMm;" 1 tol:1
  run_mutation default-moved "    inline bool dropFrom(const char *e) { return e != nullptr && std::string(e) == \"on\"; }" \
    "    inline bool dropFrom(const char *e) { return e == nullptr || std::string(e) == \"on\"; }" 1 unset:1
  run_mutation gates-ignored "{ return switchOn && retried && retryValid; }" "{ return switchOn && retried && (retryValid || true); }" 1 adopt:1
  run_mutation any-refusal "        return reason.find(\"not-straight\") != std::string::npos && a.others > 0 && a.count > 0 &&" \
    "        return (reason.empty() || true) && a.others > 0 && a.count > 0 &&" 2 retry:2
  exit $BAD
' || FAILED=1

echo "==== 2. the census reader ======================================================"
if python3 -I "$T/i1993_census.py" --self-test; then
  echo "OK   i1993_census.py --self-test passes whole"
else
  echo "FAIL i1993_census.py --self-test"
  FAILED=1
fi

if [ $FAILED -eq 0 ]; then
  echo "I1993 CHECK PASS"
else
  echo "I1993 CHECK FAIL"
fi
exit $FAILED
