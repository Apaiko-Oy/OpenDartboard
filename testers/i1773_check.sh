#!/bin/bash
# #1773: a vote reading within its sigma of a RING wire -- lone or consensus -- publishes
# flagged with the ring across the wire, at 0.7, and the score string does not move. One
# compile of the tree plus two of a MUTATED SCRATCH COPY of the header, in i1628_check.sh's
# shape and for its reason: the measurement is the set of runs on one tree, so "different
# build" is never a confound. The mutations are made with sed on a copy of src/ under the
# container's /tmp; the tree is mounted and never written.
#
#   1. the tree                 i1773_ringwire_check               -- must pass whole, and
#                                print RING-SENSITIVE n= (the assertions whose figure IS
#                                the ring arithmetic) and CONSENSUS n= (the ones only a
#                                consensus reading can turn red)
#   2. mutation A: the arithmetic  the scratch header reads every radius 50 mm further out
#                                (`out.radiusMm = p.board.radius * kScoringRadiusMm + 50.0f`)
#                                -- must FAIL, PREDICTION STATED FIRST: exactly n + c FAIL
#                                lines, every `ring:` assertion and every `both:` one --
#                                the three live shapes among them by name -- and NOT ONE
#                                `pure:` assertion: a MISS still has no margin, a reading
#                                without a radius has none, #1628's wedge margin is not
#                                this arithmetic, and the naming of the score across a
#                                wire does not read a millimetre.
#   3. mutation B: lone only    the scratch header's guard reads `choice.agreeing != 1`
#                                where the tree's reads `< 1` -- must FAIL with exactly c
#                                FAIL lines, all of them `both:`, which is live case (3):
#                                the two-camera OUTER 0.5 mm from the bull's wire. Nothing
#                                `ring:` and nothing `pure:` moves: every other case is a
#                                lone reading or an unasked one.
#
# It links no extra translation unit: the rule is over PointScore, in the header, for
# #1555's reason (score_processing.hpp stays clear of entry_intersection.hpp).
# The script ends on `exit`, never on an `echo`: #1463, #1479.
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"

if [ ! -d "$OD_TREE_ROOT/build/_deps" ]; then
  echo "i1773_check: $OD_TREE_ROOT/build/_deps is not there; build this worktree first" >&2
  exit 2
fi

od_run "1773-check" --network none \
  -v "$OD_TREE_ROOT":/app -w /app "$OD_IMAGE" bash -c '
  set -u
  HDR=src/detector/geometry/detection/score_processing.hpp
  compile() { # $1 include root for src, $2 output
    g++ -std=c++17 -O1 -Wall -Wextra \
        -I "$1" -I "$1/utils" \
        -I build/_deps/nlohmann_json-src/include \
        -I build/_deps/httplib-src \
        -o "$2" testers/i1773_ringwire_check.cpp \
        $(pkg-config --cflags --libs opencv4)
  }
  mutate() { # $1 scratch dir, $2 sed expression, $3 the needle it must find exactly once
    rm -rf "$1"; mkdir -p "$1"; cp -r src "$1/src"
    local before after
    before=$(grep -c -F -- "$3" "$1/$HDR")
    sed -i "$2" "$1/$HDR"
    after=$(grep -c -F -- "$3" "$1/$HDR")
    echo "mutation in $1: needle found $before time(s) before, $after after"
    [ "$before" -eq 1 ] && [ "$after" -eq 0 ]
  }

  compile src /tmp/i1773_tree || { echo "COMPILE_FAILED"; exit 2; }

  echo "==== 1. the tree rule ========================================================="
  /tmp/i1773_tree < /dev/null > /tmp/tree.out 2>&1
  RC_TREE=$?
  cat /tmp/tree.out
  N=$(sed -n "s/.*RING-SENSITIVE n=\([0-9]*\).*/\1/p" /tmp/tree.out | head -1)
  C=$(sed -n "s/.*CONSENSUS n=\([0-9]*\).*/\1/p" /tmp/tree.out | head -1)
  [ -n "$N" ] && [ -n "$C" ] || { echo "FAIL the check did not print its sensitive counts"; exit 1; }

  echo "==== 2. mutation A: every radius read 50 mm further out ========================"
  echo "PREDICTION: fails, with exactly $((N + C)) FAIL lines -- the $N ring: assertions and"
  echo "            the $C both: ones, the three live shapes by name among them -- and not"
  echo "            one pure: assertion."
  mutate /tmp/mutA "s/out.radiusMm = p.board.radius \* kScoringRadiusMm;/out.radiusMm = p.board.radius * kScoringRadiusMm + 50.0f;/" \
         "out.radiusMm = p.board.radius * kScoringRadiusMm;" || { echo "FAIL mutation A did not land"; exit 1; }
  compile /tmp/mutA/src /tmp/i1773_mutA || { echo "COMPILE_FAILED (mutation A)"; exit 2; }
  /tmp/i1773_mutA < /dev/null > /tmp/mutA.out 2>&1
  RC_A=$?
  cat /tmp/mutA.out
  # "^FAIL " with the space: the summary line FAILURES: n would match "^FAIL" (#1556).
  A_FAILS=$(grep -c "^FAIL " /tmp/mutA.out)
  A_PURE=$(grep -c "^FAIL pure:" /tmp/mutA.out)
  A_RING=$(grep -c "^FAIL ring:" /tmp/mutA.out)
  A_BOTH=$(grep -c "^FAIL both:" /tmp/mutA.out)
  A_LIVE=$(grep -c "^FAIL .*live ([123])" /tmp/mutA.out)

  echo "==== 3. mutation B: the flag for lone readings only ============================"
  echo "PREDICTION: fails, with exactly $C FAIL lines, all of them both: -- live case (3),"
  echo "            the two-camera OUTER 0.5 mm from the bull wire -- and nothing ring: or pure:."
  mutate /tmp/mutB "s/choice.agreeing < 1 || choice.by_default/choice.agreeing != 1 || choice.by_default/" \
         "choice.agreeing < 1 || choice.by_default" || { echo "FAIL mutation B did not land"; exit 1; }
  compile /tmp/mutB/src /tmp/i1773_mutB || { echo "COMPILE_FAILED (mutation B)"; exit 2; }
  /tmp/i1773_mutB < /dev/null > /tmp/mutB.out 2>&1
  RC_B=$?
  cat /tmp/mutB.out
  B_FAILS=$(grep -c "^FAIL " /tmp/mutB.out)
  B_PURE=$(grep -c "^FAIL pure:" /tmp/mutB.out)
  B_RING=$(grep -c "^FAIL ring:" /tmp/mutB.out)
  B_BOTH=$(grep -c "^FAIL both:" /tmp/mutB.out)

  echo
  echo "tree rc=$RC_TREE ring-sensitive n=$N consensus c=$C"
  echo "mutA rc=$RC_A fails=$A_FAILS predicted=$((N + C)) ring=$A_RING both=$A_BOTH pure=$A_PURE live=$A_LIVE"
  echo "mutB rc=$RC_B fails=$B_FAILS predicted=$C both=$B_BOTH ring=$B_RING pure=$B_PURE"
  [ $RC_TREE -eq 0 ] || { echo "FAIL the tree rule did not hold"; exit 1; }
  [ $RC_A -ne 0 ] || { echo "FAIL mutation A PASSED -- the ring arithmetic is not load-bearing"; exit 1; }
  [ "$A_FAILS" -eq $((N + C)) ] || { echo "FAIL mutation A failed $A_FAILS assertions where the prediction was $((N + C))"; exit 1; }
  [ "$A_RING" -eq "$N" ] || { echo "FAIL mutation A left $((N - A_RING)) ring-sensitive assertion(s) green"; exit 1; }
  [ "$A_PURE" -eq 0 ] || { echo "FAIL mutation A moved a pure assertion"; exit 1; }
  [ "$A_LIVE" -ge 3 ] || { echo "FAIL mutation A did not turn all three live shapes red (saw $A_LIVE)"; exit 1; }
  [ $RC_B -ne 0 ] || { echo "FAIL mutation B PASSED -- the consensus case is not load-bearing"; exit 1; }
  [ "$B_FAILS" -eq "$C" ] && [ "$B_BOTH" -eq "$C" ] || { echo "FAIL mutation B failed $B_FAILS ($B_BOTH both:) where the prediction was $C, all both:"; exit 1; }
  [ "$B_RING" -eq 0 ] && [ "$B_PURE" -eq 0 ] || { echo "FAIL mutation B moved a lone or pure assertion"; exit 1; }
  echo "ALL THREE HELD"
  exit 0
'
