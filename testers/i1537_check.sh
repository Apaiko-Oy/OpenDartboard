#!/bin/bash
# turnaus#1537: a visit that closes with fewer than three darts publishes the unseen ones as
# inferred MISSes before its END (OD_VISIT_FILL=on), and the Turnaus body marks them. One
# compile of the tree plus seven of a MUTATED SCRATCH COPY, in i1821_check.sh's shape and for
# its reason: the tree is mounted read-only and never written; each mutation is made on a copy
# under the container's /tmp, and its needle must be found exactly once. turnaus_client.cpp is
# compiled ONCE from the tree and linked into every visit_fill.hpp mutation (they do not touch
# it); the one mutation of turnaus_client.cpp compiles its own copy.
#
# PREDICTIONS, written before the mutations were first run. Each names every FAIL line the
# mutation turns red, and nothing else may go red:
#
#   1. the tree    i1537_fill_check -- must pass whole
#   2. empty       `published <= 0` read as `published < 0`: an END with no dart before it is
#                  filled with three misses -- `empty:`                                  (1)
#   3. past        `published >= kDartsPerVisit` read as `== kDartsPerVisit`: a visit of four
#                  reports itself -1 short (it fills nothing, but says so) -- `past three:` (1)
#   4. reset       the count not reset at the END: the visit after a filled one (one dart)
#                  is counted on from the first, reads 3 published and is not filled --
#                  `reset:`                                                              (1)
#   5. order       the END published before the misses it closes -- `live: 16:12`,
#                  `live: 16:21`, `live: 16:38`, `one dart:`, `reset:`, `order:`           (6)
#   6. switch      the switch ignored, a short visit filled with OD_VISIT_FILL off --
#                  `switch:`                                                             (1)
#   7. confidence  the inferred MISS published at 1.0 -- `marked:`, `body:`               (2)
#   8. path        the inferred MISS published with no path, so its body carries no account
#                  (no `path`, no `confidence`) -- `marked:`, `body:`                      (2)
#   9. key         turnaus_client.cpp's `"inferred": true` struck -- `body:`              (1)
#
# The script ends on `exit`, never on an `echo`: #1463, #1479.
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"

if [ ! -d "$OD_TREE_ROOT/build/_deps/nlohmann_json-src" ]; then
  echo "i1537_check: no $OD_TREE_ROOT/build/_deps -- configure a Linux build of this worktree first" >&2
  exit 2
fi

T0=$(date +%s)
od_run "1537-check" --network none \
  -v "$OD_TREE_ROOT":/app:ro -w /app "$OD_IMAGE" bash -c '
  set -u
  HDR=src/scorer/visit_fill.hpp
  CLIENT=src/communication/turnaus_client.cpp
  DEPS="-I /app/build/_deps/nlohmann_json-src/include -I /app/build/_deps/httplib-src"
  LIBS="$(pkg-config --cflags --libs opencv4) -lpthread"
  compile_client() { # $1 src root, $2 object
    g++ -std=c++17 -O1 -Wall -Wextra -I "$1" -I "$1/utils" $DEPS $(pkg-config --cflags opencv4) \
      -c -o "$2" "$1/communication/turnaus_client.cpp"
  }
  compile() { # $1 src root, $2 client object, $3 output
    g++ -std=c++17 -O1 -Wall -Wextra -I "$1" -I "$1/utils" $DEPS -o "$3" testers/i1537_fill_check.cpp "$2" $LIBS
  }
  mutate() { # $1 scratch dir, $2 file, $3 needle (must occur once), $4 replacement
    rm -rf "$1"; mkdir -p "$1"; cp -r src "$1/src"
    python3 - "$1/$2" "$3" "$4" <<PY
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
  run_mutation() { # $1 name, $2 file, $3 needle, $4 replacement, $5 count, $6.. the labels predicted red
    local name="$1" file="$2" needle="$3" repl="$4" want="$5"; shift 5
    echo "==== mutation $name ==========================================================="
    echo "PREDICTION: fails, with exactly $want FAIL line(s): $*"
    mutate "/tmp/$name" "$file" "$needle" "$repl" || { echo "FAIL mutation $name did not land"; return 1; }
    local obj=/tmp/client.o
    if [ "$file" = "$CLIENT" ]; then
      obj="/tmp/client_$name.o"
      compile_client "/tmp/$name/src" "$obj" || { echo "COMPILE_FAILED ($name client)"; return 1; }
    fi
    compile "/tmp/$name/src" "$obj" "/tmp/b_$name" || { echo "COMPILE_FAILED ($name)"; return 1; }
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

  compile_client src /tmp/client.o || { echo "COMPILE_FAILED (client)"; exit 2; }
  compile src /tmp/client.o /tmp/b_tree || { echo "COMPILE_FAILED"; exit 2; }
  echo "==== 1. the tree rule ========================================================="
  /tmp/b_tree < /dev/null
  [ $? -eq 0 ] || { echo "FAIL the tree rule did not hold"; exit 1; }

  BAD=0
  run_mutation empty "$HDR" "if (published <= 0 ||" "if (published < 0 ||" 1 "empty:" || BAD=1
  run_mutation past "$HDR" "|| published >= kDartsPerVisit)" "|| published == kDartsPerVisit)" 1 \
    "past three:" || BAD=1
  run_mutation reset "$HDR" "                published_ = 0;" "" 1 "reset:" || BAD=1
  run_mutation order "$HDR" "            out.send.push_back(result);" \
    "            out.send.insert(out.send.begin(), result);" 6 \
    "live: 16:12" "live: 16:21" "live: 16:38" "one dart:" "reset:" "order:" || BAD=1
  run_mutation switch "$HDR" "                if (fill)" "                if (true)" 1 "switch:" || BAD=1
  run_mutation confidence "$HDR" "kInferredConfidence = 0.5f;" "kInferredConfidence = 1.0f;" 2 \
    "marked:" "body:" || BAD=1
  run_mutation path "$HDR" "        miss.path = inferredPath();" "" 2 "marked:" "body:" || BAD=1
  run_mutation key "$CLIENT" "            body[\"inferred\"] = true;" "" 1 "body:" || BAD=1

  [ $BAD -eq 0 ] && echo "ALL MUTATIONS FAILED AS PREDICTED"
  exit $BAD
'
RC=$?
T1=$(date +%s)
echo "WALL $((T1 - T0)) s"
echo "CHECK_RC=$RC"
# The harness must exit on what it measured: run_all.sh reads the exit code and nothing
# else, and an echo returns 0 whatever it printed (#1335).
exit $RC
