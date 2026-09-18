#!/bin/bash
# #1259: run the pairing question's end-to-end check (i1259_pairing_check.py) in the Linux
# container against the worktree's own Linux build and #822's stub on the container's
# loopback. No network: the stub is the only Turnaus there is. The container is named for
# the run and reaped by that name alone.
#
#   testers/i1259_check.sh [worktree] [case ...]
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"
TREE="${1:-$OD_TREE_ROOT}"
shift || true
OUT="$OD_RUNS_BASE/1259"
mkdir -p "$OUT"
docker run --rm --name "$(od_name "i1259-check")" --cpus=2 --network none -v "$TREE":/app -w /app \
  -e CHECK_WORK=/tmp/i1259-check "$OD_IMAGE" \
  bash -c 'python3 /app/testers/i1259_pairing_check.py "$@"; rc=$?; mkdir -p /app/build/i1259-check; cp -r /tmp/i1259-check/. /app/build/i1259-check/ 2>/dev/null; exit $rc' _ "$@"
echo "CHECK_RC=$?"
