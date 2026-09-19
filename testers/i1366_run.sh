#!/bin/bash
# #1366: run the board-position check (i1366_position_check.py) in the Linux container
# against the worktree's own Linux build and #822's stub on the container's loopback.
# #1276's wrapper with its own paths. No network: the stub is the only Turnaus there is.
# The container is named for the run and reaped by that name alone.
#
#   testers/i1366_run.sh [case ...]        seam, spool, rig
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"
OUT="$OD_RUNS_BASE/1366"
mkdir -p "$OUT"
docker run --rm --name "$(od_name "i1366")" --cpus=2 --network none -v "$OD_TREE_ROOT":/app -w /app \
  -e CHECK_WORK=/tmp/i1366-check -e OD_BIN="${OD_BIN:-/app/build/opendartboard}" \
  -e OD_RIG_CYCLES="${OD_RIG_CYCLES:-2000}" "$OD_IMAGE" \
  bash -c 'python3 /app/testers/i1366_position_check.py "$@"; rc=$?; mkdir -p /app/build/i1366-check; cp -r /tmp/i1366-check/. /app/build/i1366-check/ 2>/dev/null; exit $rc' _ "$@"
RC=$?
echo "CHECK_RC=$RC"
# The harness must exit on what it measured: run_all.sh reads the exit code and
# nothing else, and an echo returns 0 whatever it printed (#1335).
exit $RC
