#!/bin/bash
# #1317's Linux harness: #1321's shape with its own paths. One run, one fresh directory
# reaped from a container because the detector writes debug_frames/ and cache/ as root,
# stdout and stderr separated inside the container, host busy % from /proc/stat, wall
# seconds, the container named for the run and reaped by that name alone, --cpus=2,
# --network none. No credential exists in the run's cfg/, so the client pushes nothing.
#
#   testers/i1317_run.sh [container-bash-script-file]
#
# The default script is the one this issue is about: a wire stage that found fewer than
# twenty wires, the three guards that exist to catch it, and the mock footage beside them
# as the control.
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"
SCRIPT="${1:-$OD_TREE_ROOT/testers/phases1317/1317-partial.sh}"
BASE="$OD_RUNS_BASE/1317"
RUN="$BASE/partial"
mkdir -p "$BASE"
if [ -d "$RUN" ]; then
  docker run --rm --name "$(od_name "i1317-clean")" --network none -v "$BASE":/base "$OD_IMAGE" \
    rm -rf /base/partial > /dev/null 2>&1
fi
rm -rf "$RUN" 2>/dev/null
mkdir -p "$RUN/cfg"
cp "$SCRIPT" "$RUN/inside.sh"

# The base commit's source, unpacked here rather than inside the container: this is a git
# worktree, so its .git is a file pointing at a repository the container does not mount and
# `git archive` there cannot work. Only 1317-asan.sh reads it; every other phase ignores it.
BASE_COMMIT="${BASE_COMMIT:-50e3b07}"
mkdir -p "$RUN/base-src"
git -C "$OD_TREE_ROOT" archive "$BASE_COMMIT" | tar -x -C "$RUN/base-src"

read -r _ u0 n0 s0 i0 w0 q0 sq0 rest < /proc/stat
T0=$(date +%s.%N)

docker run --rm --name "$(od_name "i1317-partial")" --cpus=2 --network none -e HOME=/root \
  -v "$OD_TREE_ROOT":/app \
  -v "$RUN":/run1317 -v "$RUN/cfg":/root/.config \
  -w /run1317 "$OD_IMAGE" bash /run1317/inside.sh
RC=$?

T1=$(date +%s.%N)
read -r _ u1 n1 s1 i1 w1 q1 sq1 rest < /proc/stat

BUSY=$(python3 -c "
u=$u1-$u0; n=$n1-$n0; s=$s1-$s0; i=$i1-$i0; w=$w1-$w0; q=$q1-$q0; sq=$sq1-$sq0
tot=u+n+s+i+w+q+sq
print('%.1f' % (100.0*(tot-i-w)/tot) if tot else 'n/a')")
WALL=$(python3 -c "print('%.1f' % ($T1-$T0))")

echo "RUN=partial rc=$RC wall_s=$WALL host_busy_pct=$BUSY dir=$RUN"
exit $RC
