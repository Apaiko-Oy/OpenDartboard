#!/bin/bash
# #1339's Linux harness: #1320's shape with its own paths. One run, one fresh directory
# reaped from a container because the detector writes debug_frames/ and cache/ as root,
# the container named for the run and reaped by that name alone, --cpus=2, --network none.
# No credential exists in the run's cfg/, so the client pushes nothing.
#
#   testers/i1339_run.sh [container-bash-script-file]
#
# It runs the detector six times and re-encodes three clips, so it is one of the slower
# testers in this directory; OD_TESTER_TIMEOUT is what run_all.sh gives it.
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"
SCRIPT="${1:-$OD_TREE_ROOT/testers/phases1339/1339-denominator.sh}"
PHASE="$(od_phase "$SCRIPT")"
BASE="$OD_RUNS_BASE/1339"
RUN="$BASE/$PHASE"
mkdir -p "$BASE"
if [ -d "$RUN" ]; then
  od_run "i1339-clean-$PHASE" --network none -v "$BASE":/base "$OD_IMAGE" \
    rm -rf "/base/$PHASE" > /dev/null 2>&1
fi
rm -rf "$RUN" 2>/dev/null
mkdir -p "$RUN/cfg"
cp "$SCRIPT" "$RUN/inside.sh"

T0=$(date +%s)
od_run "i1339-$PHASE" --cpus=2 --network none -e HOME=/root \
  -v "$OD_TREE_ROOT":/app \
  -v "$RUN":/run1339 -v "$RUN/cfg":/root/.config \
  -w /run1339 "$OD_IMAGE" bash /run1339/inside.sh
RC=$?
T1=$(date +%s)

echo "RUN=$PHASE rc=$RC wall_s=$((T1 - T0)) dir=$RUN"
exit $RC
