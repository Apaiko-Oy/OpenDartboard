#!/bin/bash
# #1552: a reversion CLEAN vote is remembered for two windows, so a takeout whose
# cameras fall in different windows still reconciles and the visit boundary gets its
# END. One compile, three runs of the same binary:
#
#   1. the tree's rule            i1552_memory_check remembering     -- must pass whole
#   2. the pin as the falsifier   OD_REVERSION_MEMORY=off i1552_memory_check forgetting
#                                 -- must pass whole: the pin really restores the
#                                 pre-#1552 vote (the split reversions never meet, the
#                                 boundary merges, the next visit's dart lands as a
#                                 third dart of the same visit), so rig-20260922's
#                                 missing first END is reproducible on this binary and
#                                 "different build" is never a confound
#   3. the mutation proof         OD_REVERSION_MEMORY=off i1552_memory_check remembering
#                                 -- must FAIL, and the PREDICTION, stated before any
#                                 run: exactly THREE assertions go red -- window 3's
#                                 reconciled state, window 4's state, and window 4's
#                                 tip (the pinned window 4 buries dart B under the
#                                 retriever's stale working background on camera 2, so
#                                 no tip is found there) -- and nothing else. Windows 1
#                                 and 2 stay green because the memory is neither set
#                                 nor read before the split; window 3's candidate
#                                 assertion stays green because the pin moves what the
#                                 vote READS, never a camera's own candidate; and every
#                                 `pure:` assertion stays green because the pin never
#                                 touches the pure functions. A harness whose needle
#                                 cannot be made to fail proves nothing (#1463); this
#                                 is the needle failing on demand.
#
# The include paths are unit_check.sh's, for unit_check.sh's reason: the check compiles
# against exactly the headers the detector does, plus dart_processing.cpp itself, the
# way testers/i1518_reversion_check.cpp drives the same state machine.
#
# The script ends on `exit`, never on an `echo`: #1463, #1479.
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"

if [ ! -d "$OD_TREE_ROOT/build/_deps" ]; then
  echo "i1552_check: $OD_TREE_ROOT/build/_deps is not there; build this worktree first" >&2
  echo "             -- run_all.sh does that before it runs anything." >&2
  exit 2
fi

od_run "1552-check" --network none \
  -v "$OD_TREE_ROOT":/app -w /app "$OD_IMAGE" bash -c '
  set -u
  g++ -std=c++17 -O1 -Wall -Wextra \
      -I src -I src/utils \
      -I build/_deps/nlohmann_json-src/include \
      -I build/_deps/httplib-src \
      -o /tmp/i1552_memory_check testers/i1552_memory_check.cpp \
      src/detector/geometry/detection/dart_processing.cpp \
      $(pkg-config --cflags --libs opencv4) || { echo "COMPILE_FAILED"; exit 2; }

  echo "==== 1. the tree rule ========================================================="
  /tmp/i1552_memory_check remembering < /dev/null
  RC_TREE=$?

  echo "==== 2. the pin restores the pre-1552 vote ===================================="
  OD_REVERSION_MEMORY=off /tmp/i1552_memory_check forgetting < /dev/null
  RC_PIN=$?

  echo "==== 3. the mutation proof: the pinned binary asked the tree questions ========"
  echo "PREDICTION: fails, with exactly 3 FAIL lines -- window 3 state, window 4 state,"
  echo "            window 4 tip -- none of them a pure: or a candidate assertion"
  OD_REVERSION_MEMORY=off /tmp/i1552_memory_check remembering < /dev/null > /tmp/mutation.out 2>&1
  RC_MUT=$?
  cat /tmp/mutation.out
  MUT_FAILS=$(grep -c "^FAIL" /tmp/mutation.out)
  MUT_PURE_FAILS=$(grep -c "^FAIL pure:" /tmp/mutation.out)
  MUT_CAND_FAILS=$(grep -c "^FAIL.*CANDIDATE" /tmp/mutation.out)

  echo
  echo "tree rc=$RC_TREE  pinned rc=$RC_PIN  mutation rc=$RC_MUT fails=$MUT_FAILS pure_fails=$MUT_PURE_FAILS candidate_fails=$MUT_CAND_FAILS"
  [ $RC_TREE -eq 0 ] || { echo "FAIL the tree rule did not hold"; exit 1; }
  [ $RC_PIN -eq 0 ] || { echo "FAIL the pin did not restore the pre-1552 vote"; exit 1; }
  [ $RC_MUT -ne 0 ] || { echo "FAIL the mutation run PASSED -- the needle is not load-bearing"; exit 1; }
  [ "$MUT_FAILS" -eq 3 ] || { echo "FAIL the mutation run failed $MUT_FAILS assertions where the prediction was 3"; exit 1; }
  [ "$MUT_PURE_FAILS" -eq 0 ] || { echo "FAIL the pin moved a pure function, which it must not touch"; exit 1; }
  [ "$MUT_CAND_FAILS" -eq 0 ] || { echo "FAIL the pin moved a camera CANDIDATE, and it may only move what the vote reads"; exit 1; }
  echo "ALL THREE HELD"
  exit 0
'
