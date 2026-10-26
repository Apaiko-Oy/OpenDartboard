#!/bin/bash
# turnaus#1908: OD_VOTE_WEIGHT=fit, the string vote weighing each camera by its own evidence,
# held pure. One compile of the tree plus three of a MUTATED SCRATCH COPY of
# score_processing.hpp, i1907_check.sh's shape and for its reason: the tree is mounted
# read-only and never written; each mutation is a replacement in a copy under the container's
# /tmp, and its needle must occur exactly once before and not at all after.
#
#   1. the tree        i1908_vote_weight_check -- must pass whole: two rim-only cameras reading
#                      S12 (0.25 each) do not outvote one admitted line reading D12 (1.00); the
#                      three weight factors; same / tie / floor publish the majority's choice
#                      field for field; a winner no scoring-area camera read, a MISS and an
#                      asserted 20 are never weighed into a publish
#   2. weights ignored every reading weighs 1.00 (the issue's mutation) -- PREDICTION: exactly
#                      10 FAILs: `issue:` 1 (S12 publishes), `account:` 2, `weight:` 3 (every
#                      factor but the 1.00 one), `keep:` 2 (the tie reads `same`, the floor
#                      reads `tie`) and `guard:` 2 (the rim guard is never reached, the
#                      asserted case's weights read 1.00)
#   3. default moved   unset OD_VOTE_WEIGHT reads `fit` -- PREDICTION: exactly 1 FAIL, `parse:`
#   4. guard removed   a winner only rim-only cameras read may publish -- PREDICTION: exactly
#                      1 FAIL, `guard:` 1 (the rim case)
#
# Then testers/i1908_vote_census.py --self-test, which holds the live census's reading of a
# PATH: DEGRADED sentence and its weights (turnaus#1941; i1907_check.sh's shape for
# i1911_census.py). The census itself reads session logs kept off GitHub and is run by hand.
#
# The script ends on `exit`, never on an `echo`: #1463, #1479.
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"

if [ ! -d "$OD_TREE_ROOT/build/_deps" ]; then
  echo "i1908_check: $OD_TREE_ROOT/build/_deps is not there; build this worktree first" >&2
  exit 2
fi

od_run "1908-check" --network none \
  -v "$OD_TREE_ROOT":/app:ro -w /app "$OD_IMAGE" bash -c '
  set -u
  HDR=src/detector/geometry/detection/score_processing.hpp
  compile() { # $1 include root for src, $2 output
    g++ -std=c++17 -O1 -Wall -Wextra -I "$1" -I "$1/utils" \
        -I build/_deps/nlohmann_json-src/include -I build/_deps/httplib-src \
        -o "$2" testers/i1908_vote_weight_check.cpp $(pkg-config --cflags --libs opencv4)
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
  env -u OD_VOTE_WEIGHT /tmp/b_tree < /dev/null
  [ $? -eq 0 ] || { echo "FAIL the tree did not hold"; exit 1; }

  BAD=0
  run_mutation weights_ignored "$HDR" \
    "        return axis * rings * sectors;" \
    "        return 1.0 + 0.0 * axis * rings * sectors;" \
    10 "issue:=1" "account:=2" "weight:=3" "keep:=2" "guard:=2" || BAD=1
  run_mutation default_moved "$HDR" \
    "voteWeightModeFrom(std::getenv(\"OD_VOTE_WEIGHT\"))" \
    "voteWeightModeFrom(\"fit\")" \
    1 "parse:=1" || BAD=1
  run_mutation guard_removed "$HDR" \
    "        if (!scoring_area[best])" \
    "        if (false && !scoring_area[best])" \
    1 "guard:=1" || BAD=1
  [ $BAD -eq 0 ] || exit 1
  echo "ALL FOUR HELD"
  exit 0
'
RC=$?
python3 -I "$(dirname "${BASH_SOURCE[0]}")/i1908_vote_census.py" --self-test || RC=1
exit $RC
