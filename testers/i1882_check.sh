#!/bin/bash
# turnaus#1882: the heartbeat states the version this build prints, on every beat.
#
# Two halves, and the first is what makes the second a guard:
#
#   PURE   testers/i1882_body_check.cpp over src/communication/beat_body.hpp, compiled with
#          -DAPP_VERSION like the detector: the exact body with a census and without one,
#          the 64-character cut, the escape. Then three MUTATED SCRATCH COPIES of src
#          (i1793_check.sh's shape: the tree is mounted read-only; each needle must be
#          found exactly once), each predicted red on named labels:
#            no_version  the body loses its version member -- the pre-#1882 body.
#                        PREDICTION: exactly 4 FAIL -- the 3 `body:` and the one
#                        `escape:` that reads a whole body
#            no_cut      statedVersion keeps the whole string. PREDICTION: exactly 3 FAIL,
#                        `cut:` (the longer string, the wide one, the 80) and 1 `escape:`
#                        (the 70 quotes) -- 4 in all
#            no_escape   jsonString copies bytes. PREDICTION: exactly 2 FAIL, both `escape:`
#   WIRE   testers/phases1882/1882-version.sh: build/opendartboard against turnaus_stub.py
#          (which models StatedVersion), a board that cannot count and one that does --
#          every beat must state exactly what that binary's --version prints, and be kept.
#
# The script ends on `exit`, never on an `echo`: #1463, #1479.
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"

echo "################ PURE ################"
od_run "1882-pure" --network none \
  -v "$OD_TREE_ROOT":/app:ro -w /app "$OD_IMAGE" bash -c '
  set -u
  BB=src/communication/beat_body.hpp
  compile() { # $1 include root for src, $2 output
    g++ -std=c++17 -O1 -Wall -Wextra -I "$1" -DAPP_VERSION="\"1882.0.0-check\"" \
        -o "$2" testers/i1882_body_check.cpp
  }
  mutate() { # $1 scratch dir, $2 file, $3 needle (fixed string, must occur once), $4 replacement
    rm -rf "$1"; mkdir -p "$1"; cp -r src "$1/src"
    local before after
    before=$(grep -c -F -- "$3" "$1/$2")
    python3 - "$1/$2" "$3" "$4" <<PY
import sys
p, a, b = sys.argv[1:4]
s = open(p).read()
open(p, "w").write(s.replace(a, b))
PY
    after=$(grep -c -F -- "$3" "$1/$2")
    echo "mutation in $1: needle found $before time(s) before, $after after"
    [ "$before" -eq 1 ] && [ "$after" -eq 0 ]
  }
  run_mutation() { # $1 name, $2 file, $3 needle, $4 replacement, $5 FAIL count, $6.. label:count
    local name=$1 file=$2 needle=$3 repl=$4 want=$5
    shift 5
    echo "==== mutation $name ==========================================================="
    echo "PREDICTION: fails, with exactly $want FAIL line(s): $*"
    mutate "/tmp/$name" "$file" "$needle" "$repl" || { echo "FAIL mutation $name did not land"; return 1; }
    compile "/tmp/$name/src" "/tmp/b_$name" || { echo "COMPILE_FAILED ($name)"; return 1; }
    "/tmp/b_$name" < /dev/null > "/tmp/$name.out" 2>&1
    local rc=$? fails ok=1 spec label n
    cat "/tmp/$name.out"
    # "^FAIL " with the space: FAILURES: n would match "^FAIL" (#1556).
    fails=$(grep -c "^FAIL " "/tmp/$name.out")
    for spec in "$@"; do
      label=${spec%:*}; n=$(grep -c "^FAIL $label:" "/tmp/$name.out")
      [ "$n" -eq "${spec##*:}" ] || ok=0
    done
    echo "mutation $name rc=$rc fails=$fails"
    [ $rc -ne 0 ] && [ "$fails" -eq "$want" ] && [ $ok -eq 1 ] ||
      { echo "FAIL mutation $name did not fail as predicted"; return 1; }
  }

  compile src /tmp/b_tree || { echo "COMPILE_FAILED"; exit 2; }
  echo "==== the tree ==================================================================="
  /tmp/b_tree < /dev/null
  [ $? -eq 0 ] || { echo "FAIL the tree did not hold"; exit 1; }

  BAD=0
  run_mutation no_version "$BB" "out += \",\\\"version\\\":\" + jsonString(statedVersion(version));" "" \
    4 body:3 escape:1 || BAD=1
  run_mutation no_cut "$BB" "&& characters < kLongestVersion)" ")" 4 cut:3 escape:1 || BAD=1
  run_mutation no_escape "$BB" "if (c == '"'"'\"'"'"')" "if (false)" 2 escape:2 || BAD=1
  [ $BAD -eq 0 ] || exit 1
  echo "PURE: THE TREE AND ALL THREE MUTATIONS HELD"
  exit 0
'
PURE=$?
echo "PURE_RC=$PURE"

echo "################ WIRE ################"
BASE="$OD_RUNS_BASE/1882"
RUN="$BASE/wire"
mkdir -p "$BASE"
if [ -d "$RUN" ]; then
  od_run "1882-clean" --network none -v "$BASE":/base "$OD_IMAGE" rm -rf /base/wire > /dev/null 2>&1
fi
rm -rf "$RUN" 2> /dev/null
mkdir -p "$RUN/cfg"
cp "$OD_TREE_ROOT/testers/phases1882/1882-version.sh" "$RUN/inside.sh"
T0=$(date +%s)
od_run "1882-wire" --cpus=2 --network none -e HOME=/root \
  -v "$OD_TREE_ROOT":/app:ro \
  -v "$RUN":/run1882 -v "$RUN/cfg":/root/.config \
  -w /run1882 "$OD_IMAGE" bash /run1882/inside.sh
WIRE=$?
echo "WIRE_RC=$WIRE wall_s=$(( $(date +%s) - T0 )) load_at_end=$(cut -d' ' -f1 /proc/loadavg) dir=$RUN"

[ $PURE -eq 0 ] && [ $WIRE -eq 0 ]
exit $?
