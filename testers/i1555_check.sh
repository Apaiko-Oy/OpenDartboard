#!/bin/bash
# #1555: the published path is a decision, the losing path stays reachable behind a pin,
# and a fallback never reads as a triangulated position. One compile, three runs of the
# same binary -- i1518_check.sh's and i1552_check.sh's shape, for their reason: the
# measurement IS the triple on one binary, so "different build" is never a confound and
# unit_check.sh (which compiles and runs a check once) cannot hold it.
#
#   1. the tree's rule            i1555_publish_check tree     -- must pass whole
#   2. the pin as the falsifier   OD_SCORE_PATH=vote i1555_publish_check pinned
#                                 -- must pass whole: the pin really restores the
#                                 pre-#1555 binary (the vote publishes, the solver is
#                                 not consulted, and the account says which pin did it),
#                                 which is how both paths stay measurable on one binary
#   3. the mutation proof         OD_SCORE_PATH=vote i1555_publish_check tree
#                                 -- must FAIL, and the PREDICTION, stated before any
#                                 run: exactly TWO assertions go red where the census
#                                 left the vote publishing, and exactly THREE where it
#                                 wired the geometry -- (a) the pin reads SET where the
#                                 mode expected it unset, (b) the vote-only account
#                                 names the pin where the mode expected it not to, and,
#                                 only where geometry won, (c) the published path is no
#                                 longer the census winner. NOT ONE `pure:` assertion
#                                 may go red: a pin may move the wiring and the wording
#                                 and never a pure function (#1552's rule, one file
#                                 over). The check prints the census constant itself, so
#                                 the prediction is arithmetic rather than a guess, and
#                                 a harness whose needle cannot be made to fail proves
#                                 nothing (#1463).
#
# The include paths are unit_check.sh's, for unit_check.sh's reason: the check compiles
# against exactly the headers the detector does. It links no extra translation unit,
# which is itself part of what #1555 decided -- `decidePublishedPath` takes primitives so
# that score_processing.hpp does not have to reach entry_intersection.hpp and drag
# board_model/wire_model into four pure checks that link nothing (1451-scorable did not
# LINK for two issues over exactly that, and unit_check.sh's own comment records it).
#
# The script ends on `exit`, never on an `echo`: #1463, #1479.
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"

if [ ! -d "$OD_TREE_ROOT/build/_deps" ]; then
  echo "i1555_check: $OD_TREE_ROOT/build/_deps is not there; build this worktree first" >&2
  echo "             -- run_all.sh does that before it runs anything." >&2
  exit 2
fi

od_run "1555-check" --network none \
  -v "$OD_TREE_ROOT":/app -w /app "$OD_IMAGE" bash -c '
  set -u
  g++ -std=c++17 -O1 -Wall -Wextra \
      -I src -I src/utils \
      -I build/_deps/nlohmann_json-src/include \
      -I build/_deps/httplib-src \
      -o /tmp/i1555_publish_check testers/i1555_publish_check.cpp \
      $(pkg-config --cflags --libs opencv4) || { echo "COMPILE_FAILED"; exit 2; }

  echo "==== 1. the tree rule ========================================================="
  /tmp/i1555_publish_check tree < /dev/null > /tmp/tree.out 2>&1
  RC_TREE=$?
  cat /tmp/tree.out
  WON=$(sed -n "s/.*CENSUS-CONSTANT geometryWon=\([01]\).*/\1/p" /tmp/tree.out | head -1)
  if [ "$WON" = "1" ]; then PREDICTED=3; else PREDICTED=2; fi

  echo "==== 2. the pin restores the pre-1555 binary =================================="
  OD_SCORE_PATH=vote /tmp/i1555_publish_check pinned < /dev/null
  RC_PIN=$?

  echo "==== 3. the mutation proof: the pinned binary asked the tree questions ========"
  echo "PREDICTION: fails, with exactly $PREDICTED FAIL lines (the census constant in"
  echo "            this build reads geometryWon=$WON), and not one of them a pure:"
  echo "            assertion -- the pin may move the wiring and the wording, never a"
  echo "            pure function."
  OD_SCORE_PATH=vote /tmp/i1555_publish_check tree < /dev/null > /tmp/mutation.out 2>&1
  RC_MUT=$?
  cat /tmp/mutation.out
  MUT_FAILS=$(grep -c "^FAIL" /tmp/mutation.out)
  MUT_PURE_FAILS=$(grep -c "^FAIL pure:" /tmp/mutation.out)

  echo
  echo "tree rc=$RC_TREE  pinned rc=$RC_PIN  mutation rc=$RC_MUT fails=$MUT_FAILS predicted=$PREDICTED pure_fails=$MUT_PURE_FAILS"
  [ -n "$WON" ] || { echo "FAIL the check did not print its census constant"; exit 1; }
  [ $RC_TREE -eq 0 ] || { echo "FAIL the tree rule did not hold"; exit 1; }
  [ $RC_PIN -eq 0 ] || { echo "FAIL the pin did not restore the pre-1555 binary"; exit 1; }
  [ $RC_MUT -ne 0 ] || { echo "FAIL the mutation run PASSED -- the needle is not load-bearing"; exit 1; }
  [ "$MUT_FAILS" -eq "$PREDICTED" ] || { echo "FAIL the mutation run failed $MUT_FAILS assertions where the prediction was $PREDICTED"; exit 1; }
  [ "$MUT_PURE_FAILS" -eq 0 ] || { echo "FAIL the pin moved a pure function, which it must not touch"; exit 1; }
  echo "ALL THREE HELD"
  exit 0
'
