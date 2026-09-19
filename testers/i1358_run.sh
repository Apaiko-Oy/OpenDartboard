#!/bin/bash
# #1358's Linux harness: #1345's shape with its own paths. One run, one fresh directory
# reaped from a container because the detector writes debug_frames/ and cache/ as root,
# the container named for the run and reaped by that name alone, --cpus=2, --network none.
# No credential exists in the run's cfg/, so the client pushes nothing.
#
#   testers/i1358_run.sh [container-bash-script-file]
#
# It runs the detector three times over sixty seconds of footage apiece -- the rig, the
# shipped mocks, and the rig again with #1358's falsification switch -- so it is one of
# the slower testers in this directory; OD_TESTER_TIMEOUT is what run_all.sh gives it.
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"
SCRIPT="${1:-$OD_TREE_ROOT/testers/phases1358/1358-window.sh}"
BASE="$OD_RUNS_BASE/1358"
RUN="$BASE/window"
mkdir -p "$BASE"
if [ -d "$RUN" ]; then
  docker run --rm --name "$(od_name "i1358-clean")" --network none -v "$BASE":/base "$OD_IMAGE" \
    rm -rf /base/window > /dev/null 2>&1
fi
rm -rf "$RUN" 2>/dev/null
mkdir -p "$RUN/cfg"
cp "$SCRIPT" "$RUN/inside.sh"

T0=$(date +%s)
docker run --rm --name "$(od_name "i1358-window")" --cpus=2 --network none -e HOME=/root \
  -v "$OD_TREE_ROOT":/app \
  -v "$RUN":/run1358 -v "$RUN/cfg":/root/.config \
  -w /run1358 "$OD_IMAGE" bash /run1358/inside.sh
RC=$?
T1=$(date +%s)

echo "RUN=window rc=$RC wall_s=$((T1 - T0)) dir=$RUN"
exit $RC
