#!/bin/bash
# #1556: the crossing is measured across the boundary, the flag names both candidates, and
# a demotion that cannot name the second one is not published as a flag. One compile, four
# runs of the same binary -- i1518_check.sh's, i1552_check.sh's and i1555_check.sh's shape,
# for their reason: the measurement IS the set of runs on one binary, so "different build"
# is never a confound, and unit_check.sh compiles and runs a check once.
#
#   1. the tree's rule            i1556_flag_check tree      -- must pass whole
#   2. the losing rule, reachable OD_WIRE_FLAG=sigma-major i1556_flag_check crude
#                                 -- must pass whole: the pin really restores #1555's
#                                 major-axis test on this binary, which is what keeps the
#                                 two flag rules comparable rather than argued about
#   3. mutation A: the pin        OD_WIRE_FLAG=sigma-major i1556_flag_check tree
#                                 -- must FAIL, PREDICTION STATED FIRST: exactly TWO
#                                 assertions, (a) the pin reads SET where the mode
#                                 expected it unset and (b) the published verdict on the
#                                 DISCRIMINATOR -- the one planted dart the two rules
#                                 disagree about -- follows the major-axis rule where the
#                                 mode expected the across-boundary one. NOT ONE `pure:`
#                                 and NOT ONE `flag:` assertion may go red: a pin moves a
#                                 verdict and never a measurement (#1552's rule).
#   4. mutation B: the uncertainty OD_ENTRY_SIGMA=zero i1556_flag_check tree
#                                 -- must FAIL, and THE PREDICTION IS THIS ISSUE'S OWN:
#                                 zeroing the uncertainty empties the flag census, so
#                                 EVERY flag-sensitive assertion goes red and nothing else
#                                 does but the pin's own state. That is n+1 where n is the
#                                 `FLAG-SENSITIVE n=` line the check prints, so the
#                                 prediction is arithmetic rather than a guess -- and a
#                                 needle that cannot be made to fail proves nothing
#                                 (#1463).
#
# wire_model.cpp is linked for row 1512's reason: entry_intersection.hpp includes
# board_model.hpp, whose fit calls into wire_model:: at link time. score_processing.hpp is
# included beside it and adds NOTHING to the link, which is #1555's refusal 5 measured
# rather than asserted: the publication rule takes primitives so that header stays
# includable by five checks that link nothing.
#
# The script ends on `exit`, never on an `echo`: #1463, #1479.
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"

if [ ! -d "$OD_TREE_ROOT/build/_deps" ]; then
  echo "i1556_check: $OD_TREE_ROOT/build/_deps is not there; build this worktree first" >&2
  echo "             -- run_all.sh does that before it runs anything." >&2
  exit 2
fi

od_run "1556-check" --network none \
  -v "$OD_TREE_ROOT":/app -w /app "$OD_IMAGE" bash -c '
  set -u
  g++ -std=c++17 -O1 -Wall -Wextra \
      -I src -I src/utils \
      -I build/_deps/nlohmann_json-src/include \
      -I build/_deps/httplib-src \
      -o /tmp/i1556_flag_check testers/i1556_flag_check.cpp \
      src/detector/geometry/calibration/wire_model.cpp \
      $(pkg-config --cflags --libs opencv4) || { echo "COMPILE_FAILED"; exit 2; }

  echo "==== 1. the tree rule ========================================================="
  /tmp/i1556_flag_check tree < /dev/null > /tmp/tree.out 2>&1
  RC_TREE=$?
  cat /tmp/tree.out
  N=$(sed -n "s/.*FLAG-SENSITIVE n=\([0-9]*\).*/\1/p" /tmp/tree.out | head -1)

  echo "==== 2. the sigma-major pin restores the pre-1556 rule ========================"
  OD_WIRE_FLAG=sigma-major /tmp/i1556_flag_check crude < /dev/null
  RC_PIN=$?

  echo "==== 3. mutation A: the pinned binary asked the tree questions ================"
  echo "PREDICTION: fails, with exactly 2 FAIL lines -- the pin state and the published"
  echo "            verdict on the discriminator -- and not one of them a pure: or a"
  echo "            flag: assertion. A pin moves a verdict, never a measurement."
  OD_WIRE_FLAG=sigma-major /tmp/i1556_flag_check tree < /dev/null > /tmp/mutA.out 2>&1
  RC_A=$?
  cat /tmp/mutA.out
  # "^FAIL " with the space, and the space is load-bearing: the check ends on the line
  # FAILURES: n, which "^FAIL" also matches. #1555 predicted 2 and counted 3 over exactly
  # that, and the third was the summary counting itself.
  A_FAILS=$(grep -c "^FAIL " /tmp/mutA.out)
  A_PURE=$(grep -c "^FAIL pure:" /tmp/mutA.out)
  A_FLAG=$(grep -c "^FAIL flag:" /tmp/mutA.out)

  echo "==== 4. mutation B: zeroing the uncertainty ==================================="
  echo "PREDICTION: fails, with exactly $((N + 1)) FAIL lines -- every one of the $N"
  echo "            flag-sensitive assertions plus the pin state -- and not one of them a"
  echo "            pure: assertion. Zeroing the uncertainty empties the flag census,"
  echo "            which is this issue own mutation proof."
  OD_ENTRY_SIGMA=zero /tmp/i1556_flag_check tree < /dev/null > /tmp/mutB.out 2>&1
  RC_B=$?
  cat /tmp/mutB.out
  B_FAILS=$(grep -c "^FAIL " /tmp/mutB.out)
  B_PURE=$(grep -c "^FAIL pure:" /tmp/mutB.out)
  B_FLAG=$(grep -c "^FAIL flag:" /tmp/mutB.out)

  echo
  echo "tree rc=$RC_TREE  crude rc=$RC_PIN  mutA rc=$RC_A fails=$A_FAILS pure=$A_PURE flag=$A_FLAG"
  echo "flag-sensitive n=$N  mutB rc=$RC_B fails=$B_FAILS predicted=$((N + 1)) pure=$B_PURE flag=$B_FLAG"
  [ -n "$N" ] || { echo "FAIL the check did not print its flag-sensitive count"; exit 1; }
  [ $RC_TREE -eq 0 ] || { echo "FAIL the tree rule did not hold"; exit 1; }
  [ $RC_PIN -eq 0 ] || { echo "FAIL the pin did not restore the pre-1556 rule"; exit 1; }
  [ $RC_A -ne 0 ] || { echo "FAIL mutation A PASSED -- the needle is not load-bearing"; exit 1; }
  [ "$A_FAILS" -eq 2 ] || { echo "FAIL mutation A failed $A_FAILS assertions where the prediction was 2"; exit 1; }
  [ "$A_PURE" -eq 0 ] || { echo "FAIL the pin moved a pure function, which it must not touch"; exit 1; }
  [ "$A_FLAG" -eq 0 ] || { echo "FAIL the pin moved a measurement, which it must not touch"; exit 1; }
  [ $RC_B -ne 0 ] || { echo "FAIL mutation B PASSED -- a zeroed uncertainty still flagged something"; exit 1; }
  [ "$B_FAILS" -eq $((N + 1)) ] || { echo "FAIL mutation B failed $B_FAILS assertions where the prediction was $((N + 1))"; exit 1; }
  [ "$B_FLAG" -eq "$N" ] || { echo "FAIL mutation B left $((N - B_FLAG)) flag-sensitive assertion(s) green"; exit 1; }
  [ "$B_PURE" -eq 0 ] || { echo "FAIL zeroing the uncertainty moved a pure function"; exit 1; }
  echo "ALL FOUR HELD"
  exit 0
'
