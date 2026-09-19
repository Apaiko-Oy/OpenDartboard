#!/bin/bash
# #1442's harness: a camera proposing more than twenty wires is refused, and the numbers
# that say so.
#
#   testers/run_all.sh 1442      build the tree and run this
#   testers/i1442_run.sh         run it against whatever is in this tree's src/
#
# Like #1441's beside it, this compiles #1437's wire census against this tree's
# calibration stage and measures through it, plus a pure check of the count and the two
# guards it decides. So it does not read build/opendartboard and does not start a
# detector: nothing here needs bounding by a pid, because nothing here is the board that
# never exits (#895).
#
# The four claims and why each one is there are at the top of phases1442/1442-count.sh.
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"
PHASE="${1:-$OD_TREE_ROOT/testers/phases1442/1442-count.sh}"
TAG="$(od_phase "$PHASE")"
BASE="$OD_RUNS_BASE/1442"
RUN="$BASE/$TAG"
docker rm -f "$(od_name "i1442-$TAG")" > /dev/null 2>&1
if [ -d "$RUN" ]; then
  od_run "i1442-clean" --network none -v "$BASE":/base "$OD_IMAGE" \
    rm -rf "/base/$TAG" > /dev/null 2>&1
fi
rm -rf "$RUN" 2>/dev/null
mkdir -p "$RUN"
cp "$PHASE" "$RUN/inside.sh"

T0=$(date +%s)
od_run "i1442-$TAG" --cpus=2 --network none -e HOME=/root \
  -v "$OD_TREE_ROOT":/app -v "$RUN":/run1442 -w /run1442 "$OD_IMAGE" bash /run1442/inside.sh
RC=$?
echo "RUN=$TAG rc=$RC wall_s=$(( $(date +%s) - T0 )) dir=$RUN"
exit $RC
