#!/bin/bash
# #1345's Linux harness: #1339's shape with its own paths. One run, one fresh directory
# reaped from a container because the detector writes debug_frames/ and cache/ as root,
# the container named for the run and reaped by that name alone, --cpus=2, --network none.
# No credential exists in the run's cfg/, so the client pushes nothing.
#
#   testers/i1345_run.sh [container-bash-script-file]
#
# It runs the detector twice over sixty seconds of footage apiece, so it is one of the
# slower testers in this directory; OD_TESTER_TIMEOUT is what run_all.sh gives it.
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"
SCRIPT="${1:-$OD_TREE_ROOT/testers/phases1345/1345-figures.sh}"
PHASE="$(od_phase "$SCRIPT")"
BASE="$OD_RUNS_BASE/1345"
RUN="$BASE/$PHASE"
mkdir -p "$BASE"
if [ -d "$RUN" ]; then
  od_run "i1345-clean-$PHASE" --network none -v "$BASE":/base "$OD_IMAGE" \
    rm -rf "/base/$PHASE" > /dev/null 2>&1
fi
rm -rf "$RUN" 2>/dev/null
mkdir -p "$RUN/cfg"
cp "$SCRIPT" "$RUN/inside.sh"

T0=$(date +%s)
od_run "i1345-$PHASE" --cpus=2 --network none -e HOME=/root \
  -v "$OD_TREE_ROOT":/app \
  -v "$RUN":/run1345 -v "$RUN/cfg":/root/.config \
  -w /run1345 "$OD_IMAGE" bash /run1345/inside.sh
RC=$?
T1=$(date +%s)

echo "RUN=$PHASE rc=$RC wall_s=$((T1 - T0)) dir=$RUN"
exit $RC
