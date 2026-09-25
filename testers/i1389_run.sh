#!/bin/bash
# #1389's harness. Three halves, because they live at three altitudes and none of them
# can measure the others.
#
#   testers/i1389_run.sh            all three
#   testers/i1389_run.sh census     only the fourth-call-site guard
#   testers/i1389_run.sh arithmetic only the direct calls
#   testers/i1389_run.sh board      only the whole-binary runs
#
# CENSUS runs testers/i1389_quorum_census.py over src/ and asserts it empty -- and then
# runs it again over a COPY of the tree with a fourth call site planted in it, and
# asserts it names that site by file and line. That second run is the whole point: a
# census that has never been shown to fail is a census nobody can trust, and this one
# exists to catch a line that does not exist yet. It is #997's mutation proof in the
# shape #1371's census.sh established. It reads files and builds nothing, but it runs in
# a container all the same (#1607): both runs used to be a HOST `python3`, and the box
# that runs the suite has none -- the Windows Store stub answers "Python ei loytynyt",
# rc=49, and both halves then read 49. The tree goes in at /app and the planted copy at
# /run1389/planted, read-only; the same file with the same argument, only its paths are
# the container's. #1560's rule: the IMAGE has an interpreter, the HOST may not.
#
# ARITHMETIC compiles the pure header against testers/i1389_quorum_check.cpp and moves
# the floor under a fixed board, which a whole binary cannot do without a second build.
#
# BOARD runs the real binary on the shipped mocks -- the control, three of three, which
# must still calibrate and say nothing new -- and then the same footage reduced, and then
# the reduced board again with the shipped-before quorums put back by environment on the
# SAME binary. Without that last one, everything above it is a claim about a build.
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"
WHICH="${1:-both}"
BASE="$OD_RUNS_BASE/1389"
RUN="$BASE/floor"
mkdir -p "$BASE"
# Reaped by the exact name before it starts: an interrupted run leaves the container
# alive, --rm never fires, and the next run then dies on the name rather than on what it
# measures.
docker rm -f "$(od_name i1389-arith)" > /dev/null 2>&1
docker rm -f "$(od_name i1389-board)" > /dev/null 2>&1
if [ -d "$RUN" ]; then
  docker run --rm --name "$(od_name i1389-clean)" --network none -v "$BASE":/base "$OD_IMAGE" \
    rm -rf /base/floor > /dev/null 2>&1
fi
rm -rf "$RUN" 2>/dev/null
mkdir -p "$RUN/cfg"

RC=0

if [ "$WHICH" = both ] || [ "$WHICH" = all ] || [ "$WHICH" = census ]; then
  echo "=================================================================="
  echo "=== the census, over this tree ==="
  od_run "i1389-census" --network none -v "$OD_TREE_ROOT":/app:ro -w /app "$OD_IMAGE" \
    python3 testers/i1389_quorum_census.py /app < /dev/null
  C=$?
  echo
  echo "=== the same census, over a copy of this tree with a fourth call site planted ==="
  # The plant goes in a stage that has never counted cameras, which is exactly how the
  # three this issue removed arrived: score_processing knows about cameras and has never
  # had an opinion about how many of them a board needs.
  PLANT="$RUN/planted"
  rm -rf "$PLANT"; mkdir -p "$PLANT"
  cp -r "$OD_TREE_ROOT/src" "$PLANT/src"
  TARGET="$PLANT/src/detector/geometry/detection/score_processing.cpp"
  cat >> "$TARGET" <<'PLANTED'

// A fourth quorum, written the way the three ADR-0081 removed were written: a plain
// number, in a stage that has never counted cameras, with a comment that sounds
// reasonable. This file is a COPY made by testers/i1389_run.sh and is never built.
namespace
{
    bool aFourthQuorum(int cameras_that_agree)
    {
        return cameras_that_agree >= 2;
    }
}
PLANTED
  od_run "i1389-census-planted" --network none -v "$OD_TREE_ROOT":/app:ro \
    -v "$PLANT":/run1389/planted:ro -w /app "$OD_IMAGE" \
    python3 testers/i1389_quorum_census.py /run1389/planted < /dev/null
  P=$?
  echo
  if [ "$C" = 0 ]; then
    echo "OK   the census over this tree is empty"
  else
    echo "FAIL the census over this tree is not empty; its own lines are above"; RC=1
  fi
  if [ "$P" != 0 ]; then
    echo "OK   and it refuses the planted fourth site, so it is a guard and not a description"
  else
    echo "FAIL a fourth call site was planted and the census passed, so it guards nothing"; RC=1
  fi
  echo "CENSUS_RC=$C PLANTED_RC=$P"
fi

if [ "$WHICH" = both ] || [ "$WHICH" = all ] || [ "$WHICH" = arithmetic ]; then
  echo "=================================================================="
  docker run --rm --name "$(od_name i1389-arith)" --cpus=2 --network none -e HOME=/root \
    -v "$OD_TREE_ROOT":/app -w /app "$OD_IMAGE" bash -c '
      set -u
      g++ -std=c++17 -O1 -I /app/src -o /tmp/quorum_check /app/testers/i1389_quorum_check.cpp || exit 2
      rc=0
      for case in floor named; do
        echo "------------------------------------------------------------------"
        /tmp/quorum_check "$case" || rc=1
      done
      exit $rc'
  A=$?
  echo "ARITHMETIC_RC=$A"
  [ "$A" = 0 ] || RC=1
fi

if [ "$WHICH" = both ] || [ "$WHICH" = all ] || [ "$WHICH" = board ]; then
  echo "=================================================================="
  cp "$OD_TREE_ROOT/testers/phases1389/1389-floor.sh" "$RUN/inside.sh"
  docker run --rm --name "$(od_name i1389-board)" --cpus=2 --network none -e HOME=/root \
    -v "$OD_TREE_ROOT":/app \
    -v "$RUN":/run1389 -v "$RUN/cfg":/root/.config \
    -w /run1389 "$OD_IMAGE" bash /run1389/inside.sh
  B=$?
  echo "BOARD_RC=$B dir=$RUN"
  [ "$B" = 0 ] || RC=1
fi

exit $RC
