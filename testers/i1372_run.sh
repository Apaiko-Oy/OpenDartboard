#!/bin/bash
# #1372's harness: #1338's shape with its own paths. One run, one fresh directory reaped
# from a container because the detector writes debug_frames/ and cache/ as root, host busy
# % from /proc/stat, wall seconds, the container named for the run and reaped by that name
# alone, --cpus=2, --network none -- which still has a loopback, which is where the Turnaus
# stub listens.
#
#   testers/i1372_run.sh [container-bash-script-file]
#
# The run directory is the detector's working directory inside the container, which is
# what makes this issue testable at all: cache::geometry::generateFilename() is
# "cache/geometry_calibration.dat" relative to the cwd, so every phase in one run comes up
# on the file the seed phase wrote, and a fresh directory per run means no phase inherits
# yesterday's geometry.
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"
SCRIPT="${1:-$OD_TREE_ROOT/testers/phases1372/1372-cached.sh}"
BASE="$OD_RUNS_BASE/1372"
RUN="$BASE/cached"
mkdir -p "$BASE"
# Reaped by the exact name before it starts: an interrupted run leaves the container alive,
# --rm never fires, and the next run then dies on the name rather than on what it measures.
docker rm -f "$(od_name i1372-cached)" > /dev/null 2>&1
if [ -d "$RUN" ]; then
  docker run --rm --name "$(od_name i1372-clean)" --network none -v "$BASE":/base "$OD_IMAGE" \
    rm -rf /base/cached > /dev/null 2>&1
fi
rm -rf "$RUN" 2>/dev/null
mkdir -p "$RUN/cfg"
cp "$SCRIPT" "$RUN/inside.sh"

read -r _ u0 n0 s0 i0 w0 q0 sq0 rest < /proc/stat
T0=$(date +%s.%N)

docker run --rm --name "$(od_name i1372-cached)" --cpus=2 --network none -e HOME=/root \
  -v "$OD_TREE_ROOT":/app \
  -v "$RUN":/run1372 -v "$RUN/cfg":/root/.config \
  -w /run1372 "$OD_IMAGE" bash /run1372/inside.sh
RC=$?

T1=$(date +%s.%N)
read -r _ u1 n1 s1 i1 w1 q1 sq1 rest < /proc/stat

BUSY=$(python3 -c "
u=$u1-$u0; n=$n1-$n0; s=$s1-$s0; i=$i1-$i0; w=$w1-$w0; q=$q1-$q0; sq=$sq1-$sq0
tot=u+n+s+i+w+q+sq
print('%.1f' % (100.0*(tot-i-w)/tot) if tot else 'n/a')")
WALL=$(python3 -c "print('%.1f' % ($T1-$T0))")

echo "RUN=cached rc=$RC wall_s=$WALL host_busy_pct=$BUSY dir=$RUN"
# The harness must exit on what it measured: run_all.sh reads the exit code and nothing
# else, and an echo returns 0 whatever it printed (#1335).
exit $RC
