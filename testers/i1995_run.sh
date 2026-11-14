#!/bin/bash
# turnaus#1995: a kept window replays through the current binary (i1995_replay.py):
# mocks/rig-20260918 pushed at the Turnaus stub with OD_KEEP_WINDOWS set, the directory read
# again with `opendartboard --replay-windows`, and every kept dart held to the body the stub
# counted -- sector, path, cameras, radius, angle. Then a calibration stating another record
# size refused by name, a removed picture refused by name, and two predicted mutations of the
# detector, each rebuilt on a copy of the tree inside the container. Needs build/opendartboard
# AND the build directory it came from: the mutants are incremental builds of a copy of it.
#
# The checkout is mounted read-only at /tree; /app inside the container is a COPY of its
# sources and build (the build's paths are /app's), which the mutations rewrite and which
# dies with the container. OD_WINDOW_UNIT=cycles is set inside, on the board's own
# environment (#1826). I1995_MUTATIONS=0 skips the two rebuilds; I1995_REUSE=1 replays the
# directory a previous run left instead of running the rig (neither is set by the gate).
# The script ends on `exit`, never on an `echo`: #1463, #1479.
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"
OUT="$OD_RUNS_BASE/1995"
mkdir -p "$OUT"
T0=$(date +%s)
od_run "i1995-replay" --cpus=2 --network none -e HOME=/root -v "$OD_TREE_ROOT":/tree:ro -v "$OUT":/out -w /tmp \
  -e CHECK_WORK=/tmp/i1995 -e OD_APP=/tree -e OD_BIN="${OD_BIN:-/tree/build/opendartboard}" \
  -e OD_RIG_CYCLES="${OD_RIG_CYCLES:-2000}" -e I1995_MUTATIONS="${I1995_MUTATIONS:-1}" \
  -e I1995_REUSE="${I1995_REUSE:-0}" "$OD_IMAGE" \
  bash -c 'mkdir -p /app && cp -a /tree/src /tree/CMakeLists.txt /tree/build /app/ 2>/dev/null
           for extra in assets templates models distributions scripts; do [ -e /tree/$extra ] && cp -a /tree/$extra /app/; done
           python3 /tree/testers/i1995_replay.py'
RC=$?
T1=$(date +%s)
echo "WALL $((T1 - T0)) s"
echo "CHECK_RC=$RC"
exit $RC
