#!/bin/bash
# #1330's Linux harness: #1317's shape with its own paths. One run, one fresh directory
# reaped from a container because the detector writes debug_frames/ and cache/ as root,
# the container named for the run and reaped by that name alone, --cpus=2, --network none.
# No credential exists in the run's cfg/, so the client pushes nothing.
#
#   testers/i1330_run.sh [container-bash-script-file]
#
# The default script is the one this issue is about: the calibration cache round-tripped
# under the new guarantee, the same guarantee refusing #1321's std::string put back, the
# heap pointer that used to be written in its place, and the mock footage calibrating
# once and then reading its own file the second time.
set -u
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SCRIPT="${1:-$HERE/testers/phases1330/1330-ownership.sh}"
BASE="${BASE:-/home/mikko/opendartboard/runs1330}"
RUN="$BASE/ownership"
mkdir -p "$BASE"
if [ -d "$RUN" ]; then
  docker run --rm --name od-i1330-clean --network none -v "$BASE":/base od-amd64:bullseye \
    rm -rf /base/ownership > /dev/null 2>&1
fi
rm -rf "$RUN" 2>/dev/null
mkdir -p "$RUN/cfg"
cp "$SCRIPT" "$RUN/inside.sh"

# The source this issue was filed against, unpacked here rather than inside the container:
# this is a git worktree, so its .git is a file pointing at a repository the container does
# not mount and `git archive` there cannot work. Phase 3 compiles it -- it is the only way
# to measure what the old shape really put in the file, because the guarantee this issue
# adds refuses to compile a reconstruction of it, which is the whole point of the guarantee.
BASE_COMMIT="${BASE_COMMIT:-fe66d2c}"
mkdir -p "$RUN/pre-1330"
git -C "$HERE" archive "$BASE_COMMIT" | tar -x -C "$RUN/pre-1330"

read -r _ u0 n0 s0 i0 w0 q0 sq0 rest < /proc/stat
T0=$(date +%s.%N)

docker run --rm --name od-i1330-ownership --cpus=2 --network none -e HOME=/root \
  -v "$HERE":/app \
  -v "$RUN":/run1330 -v "$RUN/cfg":/root/.config \
  -w /run1330 od-amd64:bullseye bash /run1330/inside.sh
RC=$?

T1=$(date +%s.%N)
read -r _ u1 n1 s1 i1 w1 q1 sq1 rest < /proc/stat

BUSY=$(python3 -c "
u=$u1-$u0; n=$n1-$n0; s=$s1-$s0; i=$i1-$i0; w=$w1-$w0; q=$q1-$q0; sq=$sq1-$sq0
tot=u+n+s+i+w+q+sq
print('%.1f' % (100.0*(tot-i-w)/tot) if tot else 'n/a')")
WALL=$(python3 -c "print('%.1f' % ($T1-$T0))")

echo "RUN=ownership rc=$RC wall_s=$WALL host_busy_pct=$BUSY dir=$RUN"
exit $RC
