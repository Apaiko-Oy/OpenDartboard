#!/bin/bash
# #1441's harness: the wire stage reads inside its own region, and the numbers that say so.
#
#   testers/run_all.sh 1441      build the tree and run this
#   testers/i1441_run.sh         run it against whatever is in this tree's src/
#
# It compiles #1437's wire census against this tree's calibration stage and measures the
# region through it, so it does not read build/opendartboard and does not start a
# detector: nothing here needs bounding by a pid, because nothing here is the board that
# never exits (#895).
#
# The four claims and why each one is there are at the top of phases1441/1441-region.sh.
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"
PHASE="${1:-$OD_TREE_ROOT/testers/phases1441/1441-region.sh}"
TAG="$(od_phase "$PHASE")"
BASE="$OD_RUNS_BASE/1441"
RUN="$BASE/$TAG"
docker rm -f "$(od_name "i1441-$TAG")" > /dev/null 2>&1
if [ -d "$RUN" ]; then
  od_run "i1441-clean" --network none -v "$BASE":/base "$OD_IMAGE" \
    rm -rf "/base/$TAG" > /dev/null 2>&1
fi
rm -rf "$RUN" 2>/dev/null
mkdir -p "$RUN"
cp "$PHASE" "$RUN/inside.sh"

T0=$(date +%s)
od_run "i1441-$TAG" --cpus=2 --network none -e HOME=/root \
  -v "$OD_TREE_ROOT":/app -v "$RUN":/run1441 -w /run1441 "$OD_IMAGE" bash /run1441/inside.sh
RC=$?
echo "RUN=$TAG rc=$RC wall_s=$(( $(date +%s) - T0 )) dir=$RUN"
exit $RC
