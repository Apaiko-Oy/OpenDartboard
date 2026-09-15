#!/bin/bash
# #1259: run the pairing question's end-to-end check (i1259_pairing_check.py) in the Linux
# container against the worktree's own Linux build and #822's stub on the container's
# loopback. No network: the stub is the only Turnaus there is. The container is named for
# the run and reaped by that name alone.
#
#   testers/i1259_check.sh [worktree] [case ...]
set -u
TREE="${1:-/home/mikko/opendartboard/i1259}"
shift || true
OUT=/home/mikko/opendartboard/runs1259
mkdir -p "$OUT"
docker run --rm --name od-i1259-check --cpus=2 --network none -v "$TREE":/app -w /app \
  -e CHECK_WORK=/tmp/i1259-check od-amd64:bullseye \
  bash -c 'python3 /app/testers/i1259_pairing_check.py "$@"; rc=$?; mkdir -p /app/build/i1259-check; cp -r /tmp/i1259-check/. /app/build/i1259-check/ 2>/dev/null; exit $rc' _ "$@"
echo "CHECK_RC=$?"
