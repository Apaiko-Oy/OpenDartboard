#!/bin/bash
# #1518: the CLEAN reference adopts the scene at every reconciled CLEAN, and a
# dart-sized simultaneous fall of the cumulative figure reads as a takeout. One compile,
# three runs of the same binary:
#
#   1. the tree's rule            i1518_reversion_check adopting     -- must pass whole
#   2. the pin as the falsifier   OD_CLEAN_REFERENCE=calibration i1518_reversion_check calibration
#                                 -- must pass whole: the pin really restores the
#                                 pre-#1518 rule (the takeout read as a third dart, the
#                                 DART_3 wedge), so #1514's stall is reproducible on this
#                                 binary and "different build" is never a confound
#   3. the mutation proof         OD_CLEAN_REFERENCE=calibration i1518_reversion_check adopting
#                                 -- must FAIL, and the PREDICTION, stated before any
#                                 run: exactly THREE assertions go red -- window 3's
#                                 state and its per-camera votes, and window 4's state --
#                                 and nothing else. Window 1 stays green because the
#                                 phantom pull is deferred and identical under both
#                                 rules; window 2 and both tip assertions stay green
#                                 because the fresh diff against the working background
#                                 really does isolate the newest dart under either rule
#                                 (#1349/#1495's machinery is not what this issue
#                                 moved); and every `reversion:` assertion stays green
#                                 because the pin moves the reference rule, never the
#                                 pure function's arithmetic. A harness whose needle
#                                 cannot be made to fail proves nothing (#1463); this is
#                                 the needle failing on demand.
#
# The include paths are unit_check.sh's, for unit_check.sh's reason: the check compiles
# against exactly the headers the detector does, plus dart_processing.cpp itself, the
# way testers/i1349_background_check.cpp drives the same state machine.
#
# The script ends on `exit`, never on an `echo`: #1463, #1479.
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"

if [ ! -d "$OD_TREE_ROOT/build/_deps" ]; then
  echo "i1518_check: $OD_TREE_ROOT/build/_deps is not there; build this worktree first" >&2
  echo "             -- run_all.sh does that before it runs anything." >&2
  exit 2
fi

od_run "1518-check" --network none \
  -v "$OD_TREE_ROOT":/app -w /app "$OD_IMAGE" bash -c '
  set -u
  g++ -std=c++17 -O1 -Wall -Wextra \
      -I src -I src/utils \
      -I build/_deps/nlohmann_json-src/include \
      -I build/_deps/httplib-src \
      -o /tmp/i1518_reversion_check testers/i1518_reversion_check.cpp \
      src/detector/geometry/detection/dart_processing.cpp \
      $(pkg-config --cflags --libs opencv4) || { echo "COMPILE_FAILED"; exit 2; }

  echo "==== 1. the tree rule ========================================================="
  /tmp/i1518_reversion_check adopting < /dev/null
  RC_TREE=$?

  echo "==== 2. the pin restores the pre-1518 rule ===================================="
  OD_CLEAN_REFERENCE=calibration /tmp/i1518_reversion_check calibration < /dev/null
  RC_PIN=$?

  echo "==== 3. the mutation proof: the pinned binary asked the tree questions ========"
  echo "PREDICTION: fails, with exactly 3 FAIL lines, none of them a reversion: or tip assertion"
  OD_CLEAN_REFERENCE=calibration /tmp/i1518_reversion_check adopting < /dev/null > /tmp/mutation.out 2>&1
  RC_MUT=$?
  cat /tmp/mutation.out
  MUT_FAILS=$(grep -c "^FAIL" /tmp/mutation.out)
  MUT_PURE_FAILS=$(grep -c "^FAIL reversion:" /tmp/mutation.out)
  MUT_TIP_FAILS=$(grep -c "^FAIL.*tip" /tmp/mutation.out)

  echo
  echo "tree rc=$RC_TREE  pinned rc=$RC_PIN  mutation rc=$RC_MUT fails=$MUT_FAILS pure_fails=$MUT_PURE_FAILS tip_fails=$MUT_TIP_FAILS"
  [ $RC_TREE -eq 0 ] || { echo "FAIL the tree rule did not hold"; exit 1; }
  [ $RC_PIN -eq 0 ] || { echo "FAIL the pin did not restore the pre-1518 rule"; exit 1; }
  [ $RC_MUT -ne 0 ] || { echo "FAIL the mutation run PASSED -- the needle is not load-bearing"; exit 1; }
  [ "$MUT_FAILS" -eq 3 ] || { echo "FAIL the mutation run failed $MUT_FAILS assertions where the prediction was 3"; exit 1; }
  [ "$MUT_PURE_FAILS" -eq 0 ] || { echo "FAIL the pin moved the pure function, which it must not touch"; exit 1; }
  [ "$MUT_TIP_FAILS" -eq 0 ] || { echo "FAIL the pin moved a tip, which #1349/#1495 machinery owns"; exit 1; }
  echo "ALL THREE HELD"
  exit 0
'
RC=$?
echo "CHECK_RC=$RC"
exit $RC
