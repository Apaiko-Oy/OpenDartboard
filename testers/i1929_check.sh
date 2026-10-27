#!/bin/bash
# turnaus#1929: a board asked to recalibrate between legs. Two checks, one container, the
# predictions stated in each source's header:
#
#   1. i1929_decide_check.cpp         PURE. recalibration.hpp's rule and marker, the exit status,
#                                     and the launcher following a 61 with another start and no
#                                     update look -- carry() with a runner that returns numbers.
#                                     Then FOUR MUTATED SCRATCH COPIES of src/, each predicted
#                                     to turn named lines red (and, for the three in decide(),
#                                     exactly those): the round-in-hand branch gone, the Moved
#                                     branch gone, the answered-reference branch gone, and
#                                     endingOf() no longer knowing 61.
#   2. i1929_recalibration_check.py   THE BOARD. build/opendartboard on the shipped mocks against
#                                     testers/turnaus_stub.py, which serves turnaus#1928's two
#                                     addresses: polled; refused mid-leg; accepted between legs
#                                     and confirmed once; exit 61; started again by this harness
#                                     as the launcher and systemd do, calibrating afresh under
#                                     --reuse-calibration, beating CALIBRATING then READY with
#                                     the camera report; no repeat while the stub lists the
#                                     answered request again; a held Moved verdict refused.
#
# WHY THE BOARD HALF HAS NO MUTATION OF ITS OWN. Every decision it can be wrong about is
# recalibration::decide's, which the four mutations above measure; a mutated detector would
# be a second full build of the tree for each one, and what it would show -- that the board
# calls the rule -- the board half shows on this tree by the refusal and the acceptance
# arriving at the stub with the stub's own round beside them.
#
#   testers/i1929_check.sh [worktree] [case ...]     cases: legs moved (default both)
#
# The script ends on `exit`, never on an `echo`: #1463, #1479.
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"
TREE="${1:-$OD_TREE_ROOT}"
shift || true

if [ ! -x "$TREE/build/opendartboard" ] || [ ! -d "$TREE/build/_deps/nlohmann_json-src" ]; then
  echo "i1929_check: $TREE/build/opendartboard or build/_deps is not there; build this worktree first" >&2
  echo "             -- run_all.sh does that before it runs anything." >&2
  exit 2
fi

T0=$(date +%s)
od_run "1929-check" --cpus=2 --network none -v "$TREE":/app -w /app \
  -e CHECK_WORK=/tmp/i1929-check "$OD_IMAGE" bash -c '
  set -u
  DEPS="-I /app/build/_deps/nlohmann_json-src/include -I /app/build/_deps/httplib-src"
  RC=0

  pure() {  # <label> <src root>: compile and run the pure check; its rc
    local label=$1 root=$2 work=/tmp/pure-$1
    rm -rf "$work" && mkdir -p "$work"
    g++ -O1 -std=c++17 -Wall -Wextra -I "$root" $DEPS /app/testers/i1929_decide_check.cpp \
        -o "$work/decide" -lpthread || { echo "COMPILE_FAILED i1929_decide_check ($label)"; return 2; }
    (cd "$work" && ./decide < /dev/null > "$work/out.txt" 2>&1)
    local rc=$?
    cat "$work/out.txt"
    return $rc
  }

  mutate() {  # <label> <file under src> <sed address> <lines after> <must-be-gone>
    local label=$1 file=$2 pattern=$3 after=$4 gone=$5
    rm -rf "/tmp/mutated-$label" && cp -r /app/src "/tmp/mutated-$label"
    local f="/tmp/mutated-$label/$file"
    grep -cF "$pattern" "$f" | grep -qx 1 || { echo "MUTATION_FAILED $label: the line is not where it was"; return 1; }
    sed -i "\\|$pattern|,+${after}d" "$f"
    if grep -qF "$gone" "$f"; then echo "MUTATION_FAILED $label: still there"; return 1; fi
    return 0
  }

  predicted() {  # <label> <expected FAIL count or -> <named line>...
    local label=$1 count=$2; shift 2
    local out=/tmp/pure-$label/out.txt ok=1 line got
    for line in "$@"; do
      if ! grep -qF "FAIL $line" "$out"; then echo "  not red as predicted: $line"; ok=0; fi
    done
    got=$(grep -c "^FAIL " "$out")
    if [ "$count" != "-" ] && [ "$got" != "$count" ]; then
      echo "  $got lines red where $count were predicted"; ok=0
    fi
    [ $ok = 1 ]
  }

  echo "==== i1929_decide_check, this tree ==============================================="
  pure tree /app/src; rc=$?; echo "i1929_decide_check rc=$rc"; [ $rc -eq 0 ] || RC=1

  run_mutation() {  # <label> <file> <pattern> <after> <gone> <count> <named...>
    local label=$1 file=$2 pattern=$3 after=$4 gone=$5 count=$6; shift 6
    echo "==== mutation: $label ==========================================================="
    echo "PREDICTION: red -- $*"
    if ! mutate "$label" "$file" "$pattern" "$after" "$gone"; then RC=1; return; fi
    pure "$label" "/tmp/mutated-$label" > /dev/null; local rc=$?
    grep "^FAIL " "/tmp/pure-$label/out.txt" | sed "s/^/  /"
    if [ $rc -ne 0 ] && [ $rc -ne 2 ] && predicted "$label" "$count" "$@"; then
      echo "PASS the $label mutation is caught as predicted (rc=$rc)"
    else
      echo "FAIL the $label mutation was not caught as predicted (rc=$rc)"; RC=1
    fi
  }

  run_mutation round utils/recalibration.hpp "if (facts.round_in_hand)" 5 "if (facts.round_in_hand)" 1 \
    "a round in hand is refused, in words that say a leg is in play"
  run_mutation moved utils/recalibration.hpp "if (!facts.moved.empty())" 5 "if (!facts.moved.empty())" 2 \
    "a held Moved verdict is refused, in words that say an operator clears it" \
    "and Moved is the reason said when a round is in hand as well"
  run_mutation answered utils/recalibration.hpp "if (facts.already_answered)" 4 "if (facts.already_answered)" 1 \
    "a reference answered already is ignored: never confirmed or acted on twice"
  run_mutation ending launcher/ending.hpp "if (outcome.code == kRecalibrateStop)" 3 "return Ending::Recalibrating;" - \
    "exit 61 is Ending::Recalibrating" \
    "the launcher followed the recalibration stop with another start" \
    "the window says why it stopped, in Finnish and English"

  echo "==== i1929_recalibration_check: the board ========================================"
  python3 /app/testers/i1929_recalibration_check.py "$@"; rc=$?
  echo "i1929_recalibration_check rc=$rc"; [ $rc -eq 0 ] || RC=1
  mkdir -p /app/build/i1929-check && cp -r /tmp/i1929-check/. /app/build/i1929-check/ 2>/dev/null
  exit $RC
' _ "$@"
RC=$?
T1=$(date +%s)
echo "WALL $((T1 - T0)) s"
echo "CHECK_RC=$RC"
exit $RC
