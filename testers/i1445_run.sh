#!/bin/bash
# #1445's harness: a camera refused on one averaged frame is looked at again, and the
# measurement that sizes how often.
#
#   testers/run_all.sh 1445      build the tree and run this
#   testers/i1445_run.sh         run it against whatever is in this tree's src/ and build/
#
# Unlike #1441's and #1442's beside it, this DOES read build/opendartboard: phases C and D
# are about what a whole board does with a refused camera, and that cannot be asked of the
# calibration stage alone. So the binary must be the one run_all.sh builds -- the dev build,
# with DEBUG_SEEK_VIDEO, because which stretch of a clip a camera calibrates on is the whole
# subject here (see the seek arithmetic in i1445_look_census.cpp).
#
# Every detector run inside is backgrounded and ended by its own recorded pid: a board that
# cannot calibrate takes #895's vigil and never exits, and phase C's `once` arm is exactly
# that board.
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"
PHASE="${1:-$OD_TREE_ROOT/testers/phases1445/1445-looks.sh}"
TAG="$(od_phase "$PHASE")"
BASE="$OD_RUNS_BASE/1445"
RUN="$BASE/$TAG"
docker rm -f "$(od_name "i1445-$TAG")" > /dev/null 2>&1
if [ -d "$RUN" ]; then
  od_run "i1445-clean" --network none -v "$BASE":/base "$OD_IMAGE" \
    rm -rf "/base/$TAG" > /dev/null 2>&1
fi
rm -rf "$RUN" 2>/dev/null
mkdir -p "$RUN"
cp "$PHASE" "$RUN/inside.sh"

T0=$(date +%s)
od_run "i1445-$TAG" --cpus=2 --network none -e HOME=/root \
  -v "$OD_TREE_ROOT":/app -v "$RUN":/run1445 -w /run1445 "$OD_IMAGE" bash /run1445/inside.sh
RC=$?
echo "RUN=$TAG rc=$RC wall_s=$(( $(date +%s) - T0 )) dir=$RUN"
exit $RC
