#!/bin/bash
# turnaus#1905: OD_UNDISTORT, the kept centreline straightened through #1560's kappa before the
# straightness gate and the line fit, held pure. One compile of the tree plus three of a
# MUTATED SCRATCH COPY of shaft_axis.hpp, i1911_check.sh's shape and for its reason: the tree
# is mounted read-only and never written; each mutation is a replacement in a copy under the
# container's /tmp, and its needle must occur exactly once before and not at all after.
#
#   1. the tree       i1905_undistort_check -- must pass whole: a rod drawn through kappa
#                     -1.1e-6 at the frame edge scatters 1.26 px raw and 0.33 px straightened,
#                     its line is the distorted chord, and off answers the old fit field for field
#   2. sign flipped   the centreline is undistorted with -kappa (the issue's mutation) --
#                     PREDICTION: exactly 5 FAILs, `straight:` 2 (the curve bends the other way
#                     and scatters 1.68 px, worse than raw), `transport:` 2 and `census:` 1 (the
#                     refused line is no longer the `ok` slot the census assertion reads)
#   3. untransported  the undistorted line is handed to the board fit as it is, never taken
#                     back to the distorted pixels -- PREDICTION: exactly 2 FAILs, both
#                     `transport:` (the line sits 26 px off the chord at 1.5 deg; the
#                     straightness, the gate and the census all hold -- they never read it)
#   4. default moved  unset OD_UNDISTORT reads "on" -- PREDICTION: exactly 1 FAIL, `parse:`
#                     (AxisParams' default; every other assertion names its kappa)
#
# Then testers/i1911_census.py --self-test, which holds the reader of the `,raw=` slot.
#
# The script ends on `exit`, never on an `echo`: #1463, #1479.
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"

od_run "1905-check" --network none \
  -v "$OD_TREE_ROOT":/app:ro -w /app "$OD_IMAGE" bash -c '
  set -u
  AXIS=src/detector/geometry/detection/shaft_axis.hpp
  compile() { # $1 include root for src, $2 output
    g++ -std=c++17 -O1 -Wall -Wextra -I "$1" -I "$1/utils" \
        -o "$2" testers/i1905_undistort_check.cpp $(pkg-config --cflags --libs opencv4)
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
  run_mutation sign_flipped "$AXIS" \
    "lens_census::undistortPx(kcx, kcy, params.undistort_kappa, p.x, p.y);" \
    "lens_census::undistortPx(kcx, kcy, -params.undistort_kappa, p.x, p.y);" \
    5 "straight:=2" "transport:=2" "census:=1" || BAD=1
  run_mutation untransported "$AXIS" \
    "                lens_census::distortPx(kcx, kcy, params.undistort_kappa, e0.x, e0.y);
                lens_census::distortPx(kcx, kcy, params.undistort_kappa, e1.x, e1.y);
                lens_census::distortPx(kcx, kcy, params.undistort_kappa, mid.x, mid.y);
" "" 2 "transport:=2" || BAD=1
  run_mutation default_moved "$AXIS" \
    "undistortKappaFrom(std::getenv(\"OD_UNDISTORT\"));" \
    "undistortKappaFrom(std::getenv(\"OD_UNDISTORT\") ? std::getenv(\"OD_UNDISTORT\") : \"on\");" \
    1 "parse:=1" || BAD=1
  [ $BAD -eq 0 ] || exit 1
  echo "ALL FOUR HELD"
  exit 0
'
RC=$?
python3 -I "$(dirname "${BASH_SOURCE[0]}")/i1911_census.py" --self-test || RC=1
exit $RC
