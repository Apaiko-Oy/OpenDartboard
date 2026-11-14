#!/bin/bash
# turnaus#1994: OD_FUSE (off unset) -- a camera's placed tip as a weak point constraint in the
# geometric solve. testers/i1994_fuse_check.cpp holds the assertions; this compiles it against
# the tree, then against three mutated scratch copies of entry_intersection.hpp, each PREDICTED
# to turn named assertions red:
#
#   weights-ignored   a tip's weight is 1 whatever its sigma     3: weight:3
#   tip-dropped       no tip ever reaches the normal equations   9: enter:1 weight:2 check:2 all:1 oneline:1 tips:2
#   default-moved     Params fuses unasked                        9: unset:4 enter:1 check:2 floor:2
#
# Then i1994_census.py --self-test. Compiles only; no footage.
#
#   testers/run_all.sh 1994-fuse
#
# The script ends on `exit`, never on an `echo`: #1463, #1479.
set -u
T="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FAILED=0
. "$T/tester_paths.sh"
if [ ! -d "$OD_TREE_ROOT/build/_deps" ]; then
  echo "FAIL $OD_TREE_ROOT/build/_deps is not there; build this worktree first"
  exit 2
fi
od_run "1994-check" --network none -v "$OD_TREE_ROOT":/app:ro -w /app "$OD_IMAGE" bash -c '
  set -u
  ENTRY=src/detector/geometry/detection/entry_intersection.hpp
  BAD=0
  compile() { # $1 include root for src, $2 output
    g++ -std=c++17 -O1 -Wall -Wextra -I "$1" -I "$1/utils" \
        -I build/_deps/nlohmann_json-src/include -I build/_deps/httplib-src \
        -o "$2" testers/i1994_fuse_check.cpp "$1/detector/geometry/calibration/wire_model.cpp" \
        $(pkg-config --cflags --libs opencv4)
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
  run_mutation() { # $1 name, $2 file, $3 needle, $4 replacement, $5 total FAILs, $6.. label:count
    local name=$1 file=$2 needle=$3 repl=$4 total=$5; shift 5
    echo "==== mutation $name ==========================================================="
    mutate "/tmp/$name" "$file" "$needle" "$repl" || { echo "FAIL mutation $name: needle not exactly once"; BAD=1; return; }
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
  run_mutation weights-ignored "$ENTRY" \
    "                const double w = 1.0 / std::max(1e-6, con.tipSigmaMm * con.tipSigmaMm);" \
    "                const double w = 1.0;" 3 weight:3
  run_mutation tip-dropped "$ENTRY" "                if (!con.tipEntered)" "                if (true)" 9 \
    enter:1 weight:2 check:2 all:1 oneline:1 tips:2
  run_mutation default-moved "$ENTRY" "        bool fuseTips = false;" "        bool fuseTips = true;" 9 \
    unset:4 enter:1 check:2 floor:2
  exit $BAD
' || FAILED=1

echo "==== 2. the census reader ======================================================"
if python3 -I "$T/i1994_census.py" --self-test; then
  echo "OK   i1994_census.py --self-test passes whole"
else
  echo "FAIL i1994_census.py --self-test"
  FAILED=1
fi

if [ $FAILED -eq 0 ]; then
  echo "I1994 CHECK PASS"
else
  echo "I1994 CHECK FAIL"
fi
exit $FAILED
