#!/bin/bash
# turnaus#1991: OD_KEEP_WINDOWS on the real binary (i1991_rig.py): mocks/rig-20260918 pushed
# at the Turnaus stub twice, the switch set and unset. Needs build/opendartboard.
# OD_WINDOW_UNIT=cycles is set inside, on the board's own environment (#1826).
# The script ends on `exit`, never on an `echo`: #1463, #1479.
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"
OUT="$OD_RUNS_BASE/1991"
mkdir -p "$OUT"
T0=$(date +%s)
od_run "i1991-rig" --cpus=2 --network none -v "$OD_TREE_ROOT":/app:ro -v "$OUT":/out -w /app \
  -e CHECK_WORK=/tmp/i1991-rig -e OD_BIN="${OD_BIN:-/app/build/opendartboard}" \
  -e OD_RIG_CYCLES="${OD_RIG_CYCLES:-2000}" "$OD_IMAGE" \
  bash -c 'python3 /app/testers/i1991_rig.py; rc=$?; rm -rf /out/windows; cp -r /tmp/i1991-rig/keep/windows /out/ 2>/dev/null; cp /tmp/i1991-rig/keep/run.out /tmp/i1991-rig/keep/run.err /out/ 2>/dev/null; exit $rc'
RC=$?
T1=$(date +%s)
echo "WALL $((T1 - T0)) s"
echo "CHECK_RC=$RC"
exit $RC
