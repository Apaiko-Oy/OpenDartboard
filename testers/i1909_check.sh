#!/bin/bash
# turnaus#1909: the sisal slide's geometry, held pure, with its mutation proofs. On the host,
# python3 alone: no detector binary, no build, no container -- OD_SISAL_MM is NOT in the
# detector (the census argues against it, docs/rig.md "The sisal census"), so what is held
# here is the model the census measured it with, testers/i1909_census.py.
#
#   1. the tree      i1909_census.py --self-test must pass whole: the decomposition, the
#                    acceptance's plant (a known axis at 45 degrees and d = 2 moves the entry
#                    2.0 mm radially, the way the dart travels), and the same dart end to end
#                    through two planted pinhole cameras (centres and focal recovered from H
#                    alone, the axis from the camera planes, the slide from the axis)
#   2. sign flipped  sisal_shift slides the other way -- PREDICTION: exactly 3 FAILs,
#                    slide-45, slide-45-radial and slide-planted (slide-square holds: zero
#                    either way)
#   3. axis flipped  dart_axis orients the direction OUT of the board -- PREDICTION: exactly 2
#                    FAILs, axis and slide-planted (the plant slides 2.0 mm inward)
#
# Each mutation is a replacement in a scratch copy whose needle must occur exactly once
# before and not at all after; the tree is never written.
#
# The script ends on `exit`, never on an `echo`: #1463, #1479.
set -u
T="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SCRATCH="$(mktemp -d)"
trap 'rm -rf "$SCRATCH"' EXIT
FAILED=0

echo "==== 1. the tree ==============================================================="
if python3 -I "$T/i1909_census.py" --self-test; then
  echo "OK   the tree's self-test passes whole"
else
  echo "FAIL the tree's self-test"
  FAILED=1
fi

run_mutation() { # $1 name, $2 needle, $3 replacement, $4 total FAILs, $5.. names that must FAIL
  local name=$1 needle=$2 repl=$3 total=$4; shift 4
  echo "==== mutation $name ============================================================"
  rm -rf "$SCRATCH/m"; mkdir -p "$SCRATCH/m"
  cp "$T"/*.py "$SCRATCH/m/"   # i1909_census imports its siblings from its own directory
  if ! python3 - "$SCRATCH/m/i1909_census.py" "$needle" "$repl" <<'PY'
import sys
p, a, b = sys.argv[1:4]
s = open(p).read()
before = s.count(a)
s = s.replace(a, b)
after = s.count(a)
open(p, "w").write(s)
print("mutation: needle found %d time(s) before, %d after" % (before, after))
sys.exit(0 if (before == 1 and after == 0) else 1)
PY
  then
    echo "FAIL mutation $name: the needle is not in the file exactly once"
    FAILED=1
    return
  fi
  local out
  out=$(python3 -I "$SCRATCH/m/i1909_census.py" --self-test)
  local rc=$?
  echo "$out"
  local fails
  fails=$(printf '%s\n' "$out" | awk '/^FAIL /{n++} END{print n+0}')
  local ok=1
  [ "$rc" -ne 0 ] || { echo "FAIL mutation $name: the self-test exited 0"; ok=0; }
  [ "$fails" -eq "$total" ] || { echo "FAIL mutation $name: $fails FAIL(s) where $total were predicted"; ok=0; }
  local want
  for want in "$@"; do
    printf '%s\n' "$out" | awk -v w="$want" '$1=="FAIL" && $2==w {found=1} END{exit found?0:1}' ||
      { echo "FAIL mutation $name: $want did not fail"; ok=0; }
  done
  if [ $ok -eq 1 ]; then
    echo "OK   mutation $name turns exactly the predicted $total check(s) red: $*"
  else
    FAILED=1
  fi
}

run_mutation "sign-flipped" "    s = depth_mm / abs(axis[2])" "    s = -depth_mm / abs(axis[2])" 3 \
  slide-45 slide-45-radial slide-planted
run_mutation "axis-flipped" "    if d[2] > 0.0:" "    if d[2] < 0.0:" 2 axis slide-planted

if [ $FAILED -eq 0 ]; then
  echo "I1909 CHECK PASS"
else
  echo "I1909 CHECK FAIL"
fi
exit $FAILED
