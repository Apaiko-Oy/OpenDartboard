#!/bin/bash
# turnaus#1910: OD_SETTLE_FRAMES=<n>, the window's average and its last n-1 settled frames
# each fitted, the admitted fit with the lowest straightness residual the camera's line,
# held pure. One compile of the tree plus three of a MUTATED SCRATCH COPY of
# settle_frames.hpp, i1907_check.sh's shape and for its reason: the tree is mounted
# read-only and never written; each mutation is a replacement in a copy under the
# container's /tmp, and its needle must occur exactly once before and not at all after.
#
#   1. the tree        i1910_settle_check -- must pass whole: three synthetic pictures of one
#                      dart, bowed 14 and 11 px by its swing (4.18 and 3.29 px RMS, refused
#                      not straight) and at rest (0.29 px, admitted), fitted by the real
#                      observeShaftAxis: n = 1 refuses, n = 3 is the third picture's line
#   2. first wins      the first picture (the average) always wins -- the issue's mutation --
#                      PREDICTION: exactly 5 FAILs, `n3:` 2, `admitted:` 1, `line:` 1,
#                      `census:` 1
#   3. default moved   unset OD_SETTLE_FRAMES reads 3 -- PREDICTION: exactly 1 FAIL, `parse:` 1
#   4. refused wins    a refused fit may win on a lower residual -- PREDICTION: exactly 2
#                      FAILs, `n3:` 1 (three admitted, not one) and `admitted:` 1 (the blob)
#
# The script ends on `exit`, never on an `echo`: #1463, #1479.
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"

od_run "1910-check" --network none \
  -v "$OD_TREE_ROOT":/app:ro -w /app "$OD_IMAGE" bash -c '
  set -u
  HDR=src/detector/geometry/detection/settle_frames.hpp
  compile() { # $1 include root for src, $2 output
    g++ -std=c++17 -O1 -Wall -Wextra -I "$1" -I "$1/utils" \
        -o "$2" testers/i1910_settle_check.cpp $(pkg-config --cflags --libs opencv4)
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
    env -u OD_SETTLE_FRAMES "/tmp/b_$name" < /dev/null > "/tmp/$name.out" 2>&1
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
  env -u OD_SETTLE_FRAMES /tmp/b_tree < /dev/null
  [ $? -eq 0 ] || { echo "FAIL the tree did not hold"; exit 1; }

  BAD=0
  run_mutation first_wins "$HDR" \
    "        c.winner = best < 0 ? 0 : best;" \
    "        c.winner = 0 * best;" \
    5 "n3:=2" "admitted:=1" "line:=1" "census:=1" || BAD=1
  run_mutation default_moved "$HDR" \
    "settleFramesFrom(std::getenv(\"OD_SETTLE_FRAMES\"))" \
    "settleFramesFrom(\"3\")" \
    1 "parse:=1" || BAD=1
  run_mutation refused_wins "$HDR" \
    "            if (!fits[k].valid)" \
    "            if (false && !fits[k].valid)" \
    2 "n3:=1" "admitted:=1" || BAD=1
  [ $BAD -eq 0 ] || exit 1
  echo "ALL FOUR HELD"
  exit 0
'
exit $?
