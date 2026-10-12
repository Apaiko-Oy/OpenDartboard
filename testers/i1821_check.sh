#!/bin/bash
# turnaus#1821: a dart in the board (casual board 20's S13, 2026-10-11 00:59:28) published as an
# unflagged MISS by the rim-only carry. One compile of the tree plus eight of a MUTATED SCRATCH
# COPY of rim_offer.hpp, in i1820_check.sh's shape and for its reason: the tree is mounted
# read-only and never written; each mutation is made on a copy under the container's /tmp, and
# its needle must be found exactly once.
#
# PREDICTIONS, written before the mutations were first run. Each names every FAIL line the
# mutation turns red, and nothing else may go red:
#
#   1. the tree    i1821_offer_check -- must pass whole
#   2. line        the usable-line clause dropped: #1802's two phantoms with one camera
#                  clearing the floor (camera 2 `not straight`) are offered their tip's wedge --
#                  `phantom: 01:02:06`, `phantom: 01:02:10`                               (2)
#   3. count       `clearing != 1` read as `clearing < 1`: two cameras clearing on a board of
#                  four are offered the last one's wedge -- `count:`                      (1)
#   4. vote        the vote's own reading no longer stops the rule -- `lone: 00:59:20`    (1)
#   5. carry       a dart the rim-only votes did not carry is offered -- `carry:`         (1)
#   6. ring        the double offered instead of the single -- `live: 00:59:28 S13`,
#                  `switch:` (both read the alternative's text)                           (2)
#   7. switch      the switch ignored, every eligible window flagged -- `switch:`         (1)
#   8. wedge       a tip with no read wedge offered `S-1` -- `wedge:`                     (1)
#   9. tip         the tip clause dropped: the no-tip S13 is refused by the wedge clause
#                  under the wrong word -- `live: 00:59:28 with no tip`                   (1)
#
# The script ends on `exit`, never on an `echo`: #1463, #1479.
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"

od_run "1821-check" --network none \
  -v "$OD_TREE_ROOT":/app:ro -w /app "$OD_IMAGE" bash -c '
  set -u
  HDR=src/detector/geometry/detection/rim_offer.hpp
  compile() { # $1 include root for src, $2 output
    g++ -std=c++17 -O1 -Wall -Wextra -I "$1" -o "$2" testers/i1821_offer_check.cpp
  }
  mutate() { # $1 scratch dir, $2 needle (must occur once), $3 replacement
    rm -rf "$1"; mkdir -p "$1"; cp -r src "$1/src"
    python3 - "$1/$HDR" "$2" "$3" <<PY
import sys
p, a, b = sys.argv[1:4]
s = open(p).read()
n = s.count(a)
print("mutation in %s: needle found %d time(s)" % (p, n))
if n != 1:
    sys.exit(1)
open(p, "w").write(s.replace(a, b))
PY
  }
  run_mutation() { # $1 name, $2 needle, $3 replacement, $4 count, $5.. the labels predicted red
    local name="$1" needle="$2" repl="$3" want="$4"; shift 4
    echo "==== mutation $name ==========================================================="
    echo "PREDICTION: fails, with exactly $want FAIL line(s): $*"
    mutate "/tmp/$name" "$needle" "$repl" || { echo "FAIL mutation $name did not land"; return 1; }
    compile "/tmp/$name/src" "/tmp/b_$name" || { echo "COMPILE_FAILED ($name)"; return 1; }
    "/tmp/b_$name" < /dev/null > "/tmp/$name.out" 2>&1
    local rc=$? fails named=0 label
    cat "/tmp/$name.out"
    # "^FAIL " with the space: FAILURES: n would match "^FAIL" (#1556).
    fails=$(grep -c "^FAIL " "/tmp/$name.out")
    for label in "$@"; do
      named=$((named + $(grep -c -F "FAIL $label" "/tmp/$name.out")))
    done
    echo "mutation $name rc=$rc fails=$fails named=$named"
    [ $rc -ne 0 ] && [ "$fails" -eq "$want" ] && [ "$named" -eq "$want" ] ||
      { echo "FAIL mutation $name did not fail as predicted"; return 1; }
  }

  compile src /tmp/b_tree || { echo "COMPILE_FAILED"; exit 2; }
  echo "==== 1. the tree rule ========================================================="
  /tmp/b_tree < /dev/null
  [ $? -eq 0 ] || { echo "FAIL the tree rule did not hold"; exit 1; }

  BAD=0
  run_mutation line   "        if (!w->usable_line)" "        if (false)" 2 \
    "phantom: 01:02:06" "phantom: 01:02:10" || BAD=1
  run_mutation count  "if (clearing != 1)" "if (clearing < 1)" 1 "count:" || BAD=1
  run_mutation vote   "        if (vote_read)" "        if (false)" 1 "lone: 00:59:20" || BAD=1
  run_mutation carry  "        if (!rim_carried)" "        if (false)" 1 "carry:" || BAD=1
  run_mutation ring   "o.alternative = \"S\" +" "o.alternative = \"D\" +" 2 \
    "live: 00:59:28 S13" "switch:" || BAD=1
  run_mutation switch "        if (!on)" "        if (false)" 1 "switch:" || BAD=1
  run_mutation wedge  "if (!w->wedge_read || w->segment < 1 || w->segment > 20)" "if (false)" 1 \
    "wedge:" || BAD=1
  run_mutation tip    "        if (!w->tip_found)" "        if (false)" 1 \
    "live: 00:59:28 with no tip" || BAD=1
  [ $BAD -eq 0 ] || exit 1
  echo "ALL NINE HELD"
  exit 0
'
exit $?
