#!/bin/bash
# #1276: run the takeout check (i1276_takeout_check.py) in the Linux container against the
# worktree's own Linux build and #822's stub on the container's loopback. #1259's wrapper
# with its own paths. No network: the stub is the only Turnaus there is. The container is
# named for the run and reaped by that name alone.
#
#   testers/i1276_check.sh [worktree] [case ...]
set -u
TREE="${1:-/home/mikko/opendartboard/i1276}"
shift || true
OUT=/home/mikko/opendartboard/runs1276
mkdir -p "$OUT"
docker run --rm --name od-i1276-check --cpus=2 --network none -v "$TREE":/app -w /app \
  -e CHECK_WORK=/tmp/i1276-check -e OD_BIN="${OD_BIN:-/app/build/opendartboard}" od-amd64:bullseye \
  bash -c 'python3 /app/testers/i1276_takeout_check.py "$@"; rc=$?; mkdir -p /app/build/i1276-check; cp -r /tmp/i1276-check/. /app/build/i1276-check/ 2>/dev/null; exit $rc' _ "$@"
echo "CHECK_RC=$?"
