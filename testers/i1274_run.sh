#!/bin/bash
# #1274's run harness: #1247's shape unchanged (one run, one fresh directory reaped from a
# container, stdout and stderr separated inside the container, host busy % from /proc/stat,
# wall seconds, the container named od-i1274-<label> and reaped by that name alone,
# --cpus=2, --network none), with one thing more made an argument:
#
#   APP    the tree mounted at /app. This worktree by default; the baseline build of
#          fork main is mounted the same way, so the same phase script runs against the
#          binary from before this change and the binary from after it without an edit.
#
# The run directory appears in the container at /run1274, which is what the phase scripts
# and testers/i1274_control.sh name. #1247's dark phases copy a still JPEG; this one reads
# the copy #895's own dark run left behind, as #1247's harness does.
#
#   APP=/home/mikko/opendartboard/i1274 testers/i1274_run.sh <label> <container-bash-script-file>
set -u
LABEL="$1"
SCRIPT="$2"
APP="${APP:-/home/mikko/opendartboard/i1274}"
NET="${NET:-none}"
STILL=/home/mikko/opendartboard/runs895/dark/still.jpg
BASE=/home/mikko/opendartboard/runs1274
RUN="$BASE/$LABEL"
mkdir -p "$BASE"
if [ -d "$RUN" ]; then
  docker run --rm --name "od-i1274-clean-$LABEL" --network none -v "$BASE":/base od-amd64:bullseye \
    rm -rf "/base/$LABEL" > /dev/null 2>&1
fi
rm -rf "$RUN" 2>/dev/null
mkdir -p "$RUN/cfg"
cp "$STILL" "$RUN/still.jpg"
cp "$SCRIPT" "$RUN/inside.sh"

read -r _ u0 n0 s0 i0 w0 q0 sq0 rest < /proc/stat
T0=$(date +%s.%N)

docker run --rm --name "od-i1274-$LABEL" --cpus=2 --network "$NET" -e HOME=/root \
  -v "$APP":/app \
  -v "$RUN":/run1274 -v "$RUN/cfg":/root/.config \
  -w /run1274 od-amd64:bullseye bash /run1274/inside.sh
RC=$?

T1=$(date +%s.%N)
read -r _ u1 n1 s1 i1 w1 q1 sq1 rest < /proc/stat

BUSY=$(python3 -c "
u=$u1-$u0; n=$n1-$n0; s=$s1-$s0; i=$i1-$i0; w=$w1-$w0; q=$q1-$q0; sq=$sq1-$sq0
tot=u+n+s+i+w+q+sq
print('%.1f' % (100.0*(tot-i-w)/tot) if tot else 'n/a')")
WALL=$(python3 -c "print('%.1f' % ($T1-$T0))")

echo "RUN=$LABEL app=$APP rc=$RC wall_s=$WALL host_busy_pct=$BUSY net=$NET dir=$RUN"
