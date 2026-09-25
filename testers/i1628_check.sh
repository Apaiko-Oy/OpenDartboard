#!/bin/bash
# #1628: a lone vote reading within its sigma of a wedge wire is SAID to be, and the
# reselection to a camera clear of every wire -- measured 1:1 and refused -- stays
# reachable behind OD_LONE_WIRE=clear. One compile, three runs of the same binary,
# in i1555_check.sh's shape and for its reason: the measurement IS the triple on one
# binary, so "different build" is never a confound.
#
#   1. the default               i1628_wire_check tree                     -- must pass:
#                                 v7.2's readings publish camera 1's S19 (#1517) and the
#                                 account says it was inside the sigma
#   2. the refused repair         OD_LONE_WIRE=clear i1628_wire_check clear -- must pass:
#                                 the pin really reselects camera 2's S3 and says so
#   3. the mutation proof         OD_LONE_WIRE=clear i1628_wire_check tree  -- must FAIL,
#                                 and the PREDICTION, stated before any run: exactly TWO
#                                 FAIL lines -- (a) v7.2 no longer publishes camera 1's S19
#                                 and (b) the account no longer says the reselection is
#                                 off. The near-wire finding and the 0.7 hold either way,
#                                 and NOT ONE `pure:` assertion may go red: the pin moves
#                                 the choice, never the margin, the vote or the cases the
#                                 rule leaves alone.
#
# It links no extra translation unit: the rule is over PointScore, in the header, for
# #1555's reason (score_processing.hpp stays clear of entry_intersection.hpp).
# The script ends on `exit`, never on an `echo`: #1463, #1479.
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"

if [ ! -d "$OD_TREE_ROOT/build/_deps" ]; then
  echo "i1628_check: $OD_TREE_ROOT/build/_deps is not there; build this worktree first" >&2
  exit 2
fi

od_run "1628-check" --network none \
  -v "$OD_TREE_ROOT":/app -w /app "$OD_IMAGE" bash -c '
  set -u
  g++ -std=c++17 -O1 -Wall -Wextra \
      -I src -I src/utils \
      -I build/_deps/nlohmann_json-src/include \
      -I build/_deps/httplib-src \
      -o /tmp/i1628_wire_check testers/i1628_wire_check.cpp \
      $(pkg-config --cflags --libs opencv4) || { echo "COMPILE_FAILED"; exit 2; }

  echo "==== 1. the tree rule ========================================================="
  /tmp/i1628_wire_check tree < /dev/null
  RC_TREE=$?
  echo "==== 2. the refused reselection, reachable behind its pin ===================="
  OD_LONE_WIRE=clear /tmp/i1628_wire_check clear < /dev/null
  RC_PIN=$?
  echo "==== 3. the mutation proof: the pinned binary asked the tree questions ========"
  echo "PREDICTION: fails, with exactly 2 FAIL lines, neither of them pure:"
  OD_LONE_WIRE=clear /tmp/i1628_wire_check tree < /dev/null > /tmp/mutation.out 2>&1
  RC_MUT=$?
  cat /tmp/mutation.out
  MUT_FAILS=$(grep -c "^FAIL " /tmp/mutation.out)
  MUT_PURE_FAILS=$(grep -c "^FAIL pure:" /tmp/mutation.out)
  echo
  echo "tree rc=$RC_TREE  pinned rc=$RC_PIN  mutation rc=$RC_MUT fails=$MUT_FAILS predicted=2 pure_fails=$MUT_PURE_FAILS"
  [ $RC_TREE -eq 0 ] || { echo "FAIL the tree rule did not hold"; exit 1; }
  [ $RC_PIN -eq 0 ] || { echo "FAIL the pin did not reselect"; exit 1; }
  [ $RC_MUT -ne 0 ] || { echo "FAIL the mutation run PASSED -- the needle is not load-bearing"; exit 1; }
  [ "$MUT_FAILS" -eq 2 ] || { echo "FAIL the mutation run failed $MUT_FAILS assertions where the prediction was 2"; exit 1; }
  [ "$MUT_PURE_FAILS" -eq 0 ] || { echo "FAIL the pin moved a pure assertion"; exit 1; }
  echo "ALL THREE HELD"
  exit 0
'
