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
BASE="$OD_RUNS_BASE/1345"
RUN="$BASE/figures"
mkdir -p "$BASE"
if [ -d "$RUN" ]; then
  docker run --rm --name "$(od_name "i1345-clean")" --network none -v "$BASE":/base "$OD_IMAGE" \
    rm -rf /base/figures > /dev/null 2>&1
fi
rm -rf "$RUN" 2>/dev/null
mkdir -p "$RUN/cfg"
cp "$SCRIPT" "$RUN/inside.sh"

T0=$(date +%s)
docker run --rm --name "$(od_name "i1345-figures")" --cpus=2 --network none -e HOME=/root \
  -v "$OD_TREE_ROOT":/app \
  -v "$RUN":/run1345 -v "$RUN/cfg":/root/.config \
  -w /run1345 "$OD_IMAGE" bash /run1345/inside.sh
RC=$?
T1=$(date +%s)

echo "RUN=figures rc=$RC wall_s=$((T1 - T0)) dir=$RUN"
exit $RC
