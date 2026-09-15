#!/bin/bash
# #1259's Linux control harness: #1249's shape unchanged (one run, one fresh directory reaped
# from a container, stdout and stderr separated inside the container, host busy % from
# /proc/stat, wall seconds, the container named for the run and reaped by that name alone,
# --cpus=2, --network none), mounting this worktree. It shows that the pairing question and the transport and client changes the Linux build
# does compile leave the Linux score stream where #817 left it.
#
#   testers/i1259_run.sh <container-bash-script-file>
set -u
SCRIPT="$1"
BASE=/home/mikko/opendartboard/runs1259
RUN="$BASE/control"
mkdir -p "$BASE"
if [ -d "$RUN" ]; then
  docker run --rm --name od-i1259-clean --network none -v "$BASE":/base od-amd64:bullseye \
    rm -rf /base/control > /dev/null 2>&1
fi
rm -rf "$RUN" 2>/dev/null
mkdir -p "$RUN/cfg"
cp "$SCRIPT" "$RUN/inside.sh"

read -r _ u0 n0 s0 i0 w0 q0 sq0 rest < /proc/stat
T0=$(date +%s.%N)

docker run --rm --name od-i1259-control --cpus=2 --network none -e HOME=/root \
  -v /home/mikko/opendartboard/i1259:/app \
  -v "$RUN":/run1259 -v "$RUN/cfg":/root/.config \
  -w /run1259 od-amd64:bullseye bash /run1259/inside.sh
RC=$?

T1=$(date +%s.%N)
read -r _ u1 n1 s1 i1 w1 q1 sq1 rest < /proc/stat

BUSY=$(python3 -c "
u=$u1-$u0; n=$n1-$n0; s=$s1-$s0; i=$i1-$i0; w=$w1-$w0; q=$q1-$q0; sq=$sq1-$sq0
tot=u+n+s+i+w+q+sq
print('%.1f' % (100.0*(tot-i-w)/tot) if tot else 'n/a')")
WALL=$(python3 -c "print('%.1f' % ($T1-$T0))")

echo "RUN=control rc=$RC wall_s=$WALL host_busy_pct=$BUSY dir=$RUN"
