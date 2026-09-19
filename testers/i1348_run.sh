#!/bin/bash
# #1348's harness. Two halves, because the two halves of the issue live at different
# altitudes and neither can measure the other.
#
#   testers/i1348_run.sh            both halves
#   testers/i1348_run.sh arithmetic only the direct calls
#   testers/i1348_run.sh board      only the whole-binary runs
#
# ARITHMETIC compiles testers/i1348_quorum_check.cpp against the detector's own
# dart_processing.cpp and drives processDartState directly, once per case, because that
# stage's state is static and its `initialized` flag is set once for the life of a
# program. It is where the quorum's four-voter case is reached at all: whyNoEventIsPossible
# refuses any board that is not three slots, so a whole binary can never run four cameras,
# and at three voters and under the majority rule and the absolute count are the same
# number. #1355 reached its fourth camera the same way.
#
# BOARD runs the real binary on the shipped mocks -- the control, which must calibrate 3
# of 3 and say nothing new -- and then on the same footage with two cameras dropped, which
# is #1338's phase A. That board forms dart events happily since #1353 and has one camera
# able to vote, so it can never move its own state. Before #1348 it calibrated and beat
# READY; it must now be refused, and OD_STATE_QUORUM=absolute must put it back, on this
# same binary, or the refusal is a claim about a build rather than about a rule.
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"
WHICH="${1:-both}"
BASE="$OD_RUNS_BASE/1348"
RUN="$BASE/quorum"
mkdir -p "$BASE"
# Reaped by the exact name before it starts: an interrupted run leaves the container alive,
# --rm never fires, and the next run then dies on the name rather than on what it measures.
docker rm -f "$(od_name i1348-quorum)" > /dev/null 2>&1
docker rm -f "$(od_name i1348-board)" > /dev/null 2>&1
if [ -d "$RUN" ]; then
  docker run --rm --name "$(od_name i1348-clean)" --network none -v "$BASE":/base "$OD_IMAGE" \
    rm -rf /base/quorum > /dev/null 2>&1
fi
rm -rf "$RUN" 2>/dev/null
mkdir -p "$RUN/cfg"

RC=0

if [ "$WHICH" = both ] || [ "$WHICH" = arithmetic ]; then
  docker run --rm --name "$(od_name i1348-quorum)" --cpus=2 --network none -e HOME=/root \
    -v "$OD_TREE_ROOT":/app -w /app "$OD_IMAGE" bash -c '
      set -u
      g++ -std=c++17 -O1 -I /app/src -I /app/src/utils -o /tmp/quorum_check \
        /app/testers/i1348_quorum_check.cpp /app/src/detector/geometry/detection/dart_processing.cpp \
        $(pkg-config --cflags --libs opencv4) || exit 2
      rc=0
      for case in table sentences four-majority four-absolute shoes-majority shoes-absolute; do
        echo "=================================================================="
        /tmp/quorum_check "$case" || rc=1
      done
      exit $rc'
  A=$?
  echo "ARITHMETIC_RC=$A"
  [ "$A" = 0 ] || RC=1
fi

if [ "$WHICH" = both ] || [ "$WHICH" = board ]; then
  cp "$OD_TREE_ROOT/testers/phases1348/1348-reduced.sh" "$RUN/inside.sh"
  docker run --rm --name "$(od_name i1348-board)" --cpus=2 --network none -e HOME=/root \
    -v "$OD_TREE_ROOT":/app \
    -v "$RUN":/run1348 -v "$RUN/cfg":/root/.config \
    -w /run1348 "$OD_IMAGE" bash /run1348/inside.sh
  B=$?
  echo "BOARD_RC=$B dir=$RUN"
  [ "$B" = 0 ] || RC=1
fi

exit $RC
