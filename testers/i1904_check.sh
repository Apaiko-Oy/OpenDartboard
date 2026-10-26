#!/bin/bash
# turnaus#1904: OD_EXPOSURE / OD_GAIN, the exposure and gain pins set on every capture device
# at open, held pure. One compile of the tree plus six of a MUTATED SCRATCH COPY of
# exposure_pin.hpp, i1906_check.sh's shape and for its reason: the tree is mounted read-only
# and never written; each mutation is a replacement in a copy under the container's /tmp,
# and its needle must occur exactly once before and not at all after.
#
#   1. the tree          i1904_exposure_check -- must pass whole
#   2. conventions swap  manual is 0 on V4L2 and 1 on MSMF -- PREDICTION: exactly 2 FAILs,
#                        `v4l2:` 1 and `msmf:` 1
#   3. V4L2 auto is 1    auto_exposure=1 asked for auto on V4L2 (it means manual there) --
#                        PREDICTION: exactly 1 FAIL, `v4l2:`
#   4. order swapped     the exposure is set before the mode, so MSMF's mode set writes 0
#                        over it -- PREDICTION: exactly 4 FAILs, `v4l2:` 1, `msmf:` 1,
#                        `order:` 2
#   5. file not a no-op  a video file is planned like a device -- PREDICTION: exactly 1
#                        FAIL, `file:`
#   6. half a number     "156abc" reads 156 -- PREDICTION: exactly 3 FAILs, `parse:` 1 and
#                        `unset:` 2
#   7. default moved     unset OD_EXPOSURE reads 156 -- PREDICTION: exactly 1 FAIL, `unset:`
#   8. MSMF mode misread the start line reports MSMF's mode read-back as if it were V4L2's --
#                        PREDICTION: exactly 1 FAIL, `line:`
#
# Compiles only: no detector binary and no build/_deps.
# The script ends on `exit`, never on an `echo`: #1463, #1479.
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"

od_run "1904-check" --network none \
  -v "$OD_TREE_ROOT":/app:ro -w /app "$OD_IMAGE" bash -c '
  set -u
  PIN=src/utils/exposure_pin.hpp
  compile() { # $1 include root for src, $2 output
    g++ -std=c++17 -O1 -Wall -Wextra -I "$1" -I "$1/utils" \
        -o "$2" testers/i1904_exposure_check.cpp $(pkg-config --cflags --libs opencv4)
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
    env -u OD_EXPOSURE -u OD_GAIN "/tmp/b_$name" < /dev/null > "/tmp/$name.out" 2>&1
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
  env -u OD_EXPOSURE -u OD_GAIN /tmp/b_tree < /dev/null
  [ $? -eq 0 ] || { echo "FAIL the tree did not hold"; exit 1; }

  BAD=0
  run_mutation conventions_swapped "$PIN" \
    "return b == Backend::V4L2 ? 1.0 : 0.0;" \
    "return b == Backend::V4L2 ? 0.0 : 1.0;" \
    2 "v4l2:=1" "msmf:=1" || BAD=1
  run_mutation v4l2_auto_is_one "$PIN" \
    "return b == Backend::V4L2 ? 3.0 : 1.0;" \
    "return 1.0;" \
    1 "v4l2:=1" || BAD=1
  run_mutation order_swapped "$PIN" \
    "            out.push_back({cv::CAP_PROP_AUTO_EXPOSURE, manualModeValue(b), \"auto_exposure\"});
            out.push_back({cv::CAP_PROP_EXPOSURE, exposure.value, \"exposure\"});" \
    "            out.push_back({cv::CAP_PROP_EXPOSURE, exposure.value, \"exposure\"});
            out.push_back({cv::CAP_PROP_AUTO_EXPOSURE, manualModeValue(b), \"auto_exposure\"});" \
    4 "v4l2:=1" "msmf:=1" "order:=2" || BAD=1
  run_mutation file_not_a_noop "$PIN" \
    "        if (b == Backend::File)
        {
            return out;
        }" \
    "" \
    1 "file:=1" || BAD=1
  run_mutation half_a_number "$PIN" \
    "if (end == text || *end != '\''\\0'\'' || !std::isfinite(v))" \
    "if (end == text || !std::isfinite(v))" \
    3 "parse:=1" "unset:=2" || BAD=1
  run_mutation default_moved "$PIN" \
    "pinFrom(std::getenv(\"OD_EXPOSURE\"), true);" \
    "pinFrom(std::getenv(\"OD_EXPOSURE\") ? std::getenv(\"OD_EXPOSURE\") : \"156\", true);" \
    1 "unset:=1" || BAD=1
  run_mutation msmf_mode_misread "$PIN" \
    "        if (b == Backend::MSMF)
        {
            return \"mode not readable on MSMF\";
        }" \
    "" \
    1 "line:=1" || BAD=1
  [ $BAD -eq 0 ] || exit 1
  echo "ALL EIGHT HELD"
  exit 0
'
exit $?
