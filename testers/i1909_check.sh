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
# THEN THE DETECTOR'S TERM (OD_SISAL_MM, built opt-in after the census so the bakeoff rows
# could be measured; unset is the default and leaves every solve byte-identical), in a
# container: testers/i1909_sisal_check.cpp compiled against the tree and against four
# mutated scratch copies of src/, i1911_check.sh's shape:
#
#   4. the tree      i1909_sisal_check -- must pass whole: slideOf's arithmetic, three planted
#                    pinholes' centres recovered from their fitted boards, and the planted
#                    45-degree dart through solveEntry slid 2.0 mm radially at d = 2
#   5. sign flipped  slideOf slides the other way -- PREDICTION: exactly 2 FAILs, `slide:` 1
#                    (the 45-degree arithmetic) and `solve:` 1 (the radial move; the shift's
#                    magnitude and elevation hold)
#   6. axis flipped  axisOf orients the direction out of the board -- PREDICTION: exactly 1
#                    FAIL, `solve:` (slideOf reads |z|, so only the direction turns)
#   7. unapplied     solveEntry computes the slide and never moves X -- PREDICTION: exactly 1
#                    FAIL, `solve:` (the story still says it slid; the entry did not move)
#   8. default moved Params::sisalMm defaults to 2.0 -- PREDICTION: exactly 3 FAILs, `plant:` 1
#                    (the default solve is off the planted entry), `off:` 1 and `solve:` 1
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

# ---- the detector's term, in a container ------------------------------------------------
. "$T/tester_paths.sh"
if [ ! -d "$OD_TREE_ROOT/build/_deps" ]; then
  echo "FAIL $OD_TREE_ROOT/build/_deps is not there; build this worktree first"
  exit 2
fi
od_run "1909-check" --network none -v "$OD_TREE_ROOT":/app:ro -w /app "$OD_IMAGE" bash -c '
  set -u
  SLIDE=src/detector/geometry/detection/sisal_slide.hpp
  ENTRY=src/detector/geometry/detection/entry_intersection.hpp
  BAD=0
  compile() { # $1 include root for src, $2 output
    g++ -std=c++17 -O1 -Wall -Wextra -I "$1" -I "$1/utils" \
        -I build/_deps/nlohmann_json-src/include -I build/_deps/httplib-src \
        -o "$2" testers/i1909_sisal_check.cpp "$1/detector/geometry/calibration/wire_model.cpp" \
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
  echo "==== 4. the tree ==============================================================="
  compile src /tmp/b_tree || { echo "FAIL COMPILE_FAILED"; exit 2; }
  /tmp/b_tree || BAD=1
  run_mutation sign-flipped-cpp "$SLIDE" "const double s = depthMm / std::fabs(axis[2]);" \
    "const double s = -depthMm / std::fabs(axis[2]);" 2 slide:1 solve:1
  run_mutation axis-flipped-cpp "$SLIDE" "        if (best[2] > 0.0)" "        if (best[2] < 0.0)" 1 solve:1
  run_mutation unapplied "$ENTRY" "                X.x += (float)d.x;
                X.y += (float)d.y;" "" 1 solve:1
  run_mutation default-moved "$ENTRY" "        double sisalMm = 0.0;" "        double sisalMm = 2.0;" 3 plant:1 off:1 solve:1
  exit $BAD
' || FAILED=1

if [ $FAILED -eq 0 ]; then
  echo "I1909 CHECK PASS"
else
  echo "I1909 CHECK FAIL"
fi
exit $FAILED
