#!/bin/bash
# turnaus#1906: OD_FOCAL_PX, the focal length perspective_processing's solvePnP is given,
# held pure. One compile of the tree plus four of a MUTATED SCRATCH COPY of focal_pin.hpp,
# i1905_check.sh's shape and for its reason: the tree is mounted read-only and never
# written; each mutation is a replacement in a copy under the container's /tmp, and its
# needle must occur exactly once before and not at all after.
#
#   1. the tree       i1906_focal_check -- must pass whole
#   2. half a pin     a bad slot ends the list instead of refusing it, so "690,abc" pins 690
#                     -- PREDICTION: exactly 3 FAILs, all `parse:`
#                     ("690,", "690,,700" and "690,abc" each pin 690)
#   3. wrong slot     camera i takes the list's LAST slot -- PREDICTION: exactly 2
#                     FAILs, `camera:` (cameras 1 and 2 read 712)
#   4. axes swapped   the principal point's x and y trade places -- PREDICTION: exactly
#                     1 FAIL, `matrix:` (the `pose:` plant images through the same matrix
#                     it solves with, so it is blind to this one, and says nothing)
#   5. default moved  unset OD_FOCAL_PX reads 690 -- PREDICTION: exactly 2 FAILs, both
#                     `default:`
#
# Compiles only: no detector binary and no build/_deps.
# The script ends on `exit`, never on an `echo`: #1463, #1479.
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"

od_run "1906-check" --network none \
  -v "$OD_TREE_ROOT":/app:ro -w /app "$OD_IMAGE" bash -c '
  set -u
  PIN=src/detector/geometry/calibration/focal_pin.hpp
  compile() { # $1 include root for src, $2 output
    g++ -std=c++17 -O1 -Wall -Wextra -I "$1" -I "$1/utils" \
        -o "$2" testers/i1906_focal_check.cpp $(pkg-config --cflags --libs opencv4)
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
    env -u OD_FOCAL_PX "/tmp/b_$name" < /dev/null > "/tmp/$name.out" 2>&1
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
  env -u OD_FOCAL_PX /tmp/b_tree < /dev/null
  [ $? -eq 0 ] || { echo "FAIL the tree did not hold"; exit 1; }

  BAD=0
  run_mutation half_a_pin "$PIN" \
    "                return std::vector<double>();
            }
            out.push_back(v);" \
    "                break;
            }
            out.push_back(v);" \
    3 "parse:=3" || BAD=1
  run_mutation wrong_slot "$PIN" \
    "            f.px = pin[cameraIndex];" \
    "            f.px = pin.back();" \
    2 "camera:=2" || BAD=1
  run_mutation axes_swapped "$PIN" \
    "focalPx, 0, cols * 0.5, 0, focalPx, rows * 0.5" \
    "focalPx, 0, rows * 0.5, 0, focalPx, cols * 0.5" \
    1 "matrix:=1" || BAD=1
  run_mutation default_moved "$PIN" \
    "focalPinFrom(std::getenv(\"OD_FOCAL_PX\"));" \
    "focalPinFrom(std::getenv(\"OD_FOCAL_PX\") ? std::getenv(\"OD_FOCAL_PX\") : \"690\");" \
    2 "default:=2" || BAD=1
  [ $BAD -eq 0 ] || exit 1
  echo "ALL FIVE HELD"
  exit 0
'
exit $?
