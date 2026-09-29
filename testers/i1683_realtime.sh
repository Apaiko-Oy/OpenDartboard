#!/bin/bash
# unrun-tester: a real-time replay depends on the machine's load by design (that is what it measures), so it is never a gate; run by hand, one at a time.
#
# #1683: replay mocks/rig-20260929 IN REAL TIME, as the live board runs, and print its
# darts visit by visit against GROUND-TRUTH.md.
#
#   testers/i1683_realtime.sh <tree> <outdir> [VAR=value ...]
#
# <tree> holds a dev build at <tree>/build-rt/opendartboard (DEBUG_SEEK_VIDEO and
# DEBUG_VIA_VIDEO_INPUT, as the bakeoff builds it; OD_BUILD_DIR names another directory).
# Each VAR=value is passed into the container, so #1680's switches are
# `OD_SPIKE_THRESHOLD=0.006 OD_LONE_CAMERA=on` on a tree that has them.
#
# What differs from the bakeoff (testers/i1555_run.sh), and only this:
#   OD_REALTIME_REPLAY=on   every file is played at its presentation times from the first
#                           scoring read; a read takes each file's newest frame, and a slow
#                           cycle skips frames (utils/capture_realtime.hpp)
#   OD_MOTION_CLOCK=wall    the motion timers on wall time, the live board's default
#   OD_SEEK_VIDEO=off       the clip's opening window, a live start on a clean board
# The container gets --cpus=2, as the bakeoff's does (OD_DOCKER_CPUS names another share,
# to stand in for a slower board). The load before and after, and the
# per-cycle processing time the replay measured, are printed with the result, because a
# real-time run's result belongs to them.
set -u
TREE=${1:?tree}
OUT=${2:?outdir}
shift 2
BUILD=${OD_BUILD_DIR:-build-rt}
FIX=/app/mocks/rig-20260929
mkdir -p "$OUT"
ENVS=(-e HOME=/root -e OD_REALTIME_REPLAY=on -e OD_MOTION_CLOCK=wall -e OD_SEEK_VIDEO=off
      -e OD_MAX_CYCLES=0 -e OD_GEO_SCORE=on -e OD_SHAFT_CENSUS=1)
for kv in "$@"; do ENVS+=(-e "$kv"); done
echo "I1683 RUN tree=$(git -C "$TREE" rev-parse --short HEAD) extra=[$*] cpus=${OD_DOCKER_CPUS:-2} start=$(date -Is) load_before=$(cut -d' ' -f1-3 /proc/loadavg)" | tee "$OUT/run.txt"
docker run --rm --cpus=${OD_DOCKER_CPUS:-2} --network none "${ENVS[@]}" \
  -v "$TREE":/app -v "$OUT":/run -w /run od-amd64:bullseye \
  timeout 900 /app/$BUILD/opendartboard --cams $FIX/cam_1.mp4,$FIX/cam_2.mp4,$FIX/cam_3.mp4 \
  --width 1280 --height 720 > "$OUT/out.raw" 2>&1
rc=$?
echo "I1683 DONE rc=$rc end=$(date -Is) load_after=$(cut -d' ' -f1-3 /proc/loadavg)" | tee -a "$OUT/run.txt"
sed 's/\x1b\[[0-9;]*m//g' "$OUT/out.raw" > "$OUT/out.txt"
python3 "$(dirname "$0")/i1683_visits.py" "$OUT/out.txt" "$TREE/mocks/rig-20260929/GROUND-TRUTH.md" | tee "$OUT/visits.txt"
