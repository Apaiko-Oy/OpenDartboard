#!/bin/bash
# turnaus#1907: OD_SHAFT_ONLY, the silhouette cut where its width steps up from the shaft's to
# the flight's, the tip-side piece fitted under every gate unchanged, held pure. One compile of
# the tree plus two of a MUTATED SCRATCH COPY of shaft_axis.hpp, i1905_check.sh's shape and
# for its reason: the tree is mounted read-only and never written; each mutation is a
# replacement in a copy under the container's /tmp, and its needle must occur exactly once
# before and not at all after.
#
#   1. the tree       i1907_shaft_only_check -- must pass whole: a shaft-plus-flight figure
#                     whose flight begins 120 px from the tip and bends the whole centreline
#                     to 5.23 px RMS is cut at 120.0 px (hinted, unhinted, drawn reversed),
#                     the kept shaft scatters 0.22-0.27 px on the shaft's own line, a plain
#                     rod is never cut, and off answers the old fit field for field
#   2. wrong end      the flight end read as the tip (the issue's mutation) -- PREDICTION:
#                     exactly 15 FAILs: `cut:` 3, `straight:` 3, `angle:` 3, `ratio:` 3 (the
#                     flight's width is taken for the shaft's, so no step is ever found and
#                     the bent whole is the answer), `mode:` 2 and `census:` 1
#   3. default moved  unset OD_SHAFT_ONLY reads "on" -- PREDICTION: exactly 2 FAILs, `parse:` 1
#                     and `off:` 1 (the default params now cut the bent dart)
#
# Then testers/i1911_census.py --self-test, which holds the reader of the `,cut=` slot.
#
# The script ends on `exit`, never on an `echo`: #1463, #1479.
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"

od_run "1907-check" --network none \
  -v "$OD_TREE_ROOT":/app:ro -w /app "$OD_IMAGE" bash -c '
  set -u
  AXIS=src/detector/geometry/detection/shaft_axis.hpp
  compile() { # $1 include root for src, $2 output
    g++ -std=c++17 -O1 -Wall -Wextra -I "$1" -I "$1/utils" \
        -o "$2" testers/i1907_shaft_only_check.cpp $(pkg-config --cflags --libs opencv4)
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
  run_mutation wrong_end "$AXIS" \
    "cut.tipHigh = cut.tipFromHint ? tipT > 0.5 * (t.front() + t.back()) : highWidth < lowWidth;" \
    "cut.tipHigh = !(cut.tipFromHint ? tipT > 0.5 * (t.front() + t.back()) : highWidth < lowWidth);" \
    15 "cut:=3" "straight:=3" "angle:=3" "ratio:=3" "mode:=2" "census:=1" || BAD=1
  run_mutation default_moved "$AXIS" \
    "shaftOnlyModeFrom(std::getenv(\"OD_SHAFT_ONLY\"))" \
    "shaftOnlyModeFrom(\"on\")" \
    2 "parse:=1" "off:=1" || BAD=1
  [ $BAD -eq 0 ] || exit 1
  echo "ALL THREE HELD"
  exit 0
'
RC=$?
python3 -I "$(dirname "${BASH_SOURCE[0]}")/i1911_census.py" --self-test || RC=1
exit $RC
