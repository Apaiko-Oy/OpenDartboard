#!/bin/bash
# unrun-tester: #1682's multi-dart window census, run by hand; nothing in run_all reaches it.
#
#   testers/i1682_run.sh <name> [VAR=value ...]
#
# The 1555 bakeoff's six fixture-windows and the r18 pin, on the capture clock, each with
# OD_TRACE, into $OD_RUNS_BASE/i1682-<name>; the census files are the bakeoff's own names,
# so testers/i1655_rows.py compares two of these dirs (or one against a bakeoff run).
# Then, per run:
#
#   python3 testers/i1682_timeline.py <dir>/r29-dev.trace.csv <dir>/r29-dev.txt [--summary]
#
# Runs sequentially, one container, --cpus=2. VAR=value pairs reach the detector.
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"
name="${1:?a run name}"; shift
RUN="$OD_RUNS_BASE/i1682-$name"
mkdir -p "$RUN"
E=(-e "OD_MOTION_CLOCK=${OD_MOTION_CLOCK:-capture}"); for kv in "$@"; do E+=(-e "$kv"); done
docker run --rm --name "$(od_name i1682-$name)" --cpus=2 --network none -e HOME=/root "${E[@]}" \
  -v "$OD_TREE_ROOT":/app -v "$RUN":/run1555 -w /run1555 \
  "$OD_IMAGE" bash /app/testers/i1682_inside.sh
exit $?
