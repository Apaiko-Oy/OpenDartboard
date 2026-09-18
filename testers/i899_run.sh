#!/bin/bash
# #899's Linux harness: #1321's shape with its own paths. One run, one fresh directory
# reaped from a container because the detector writes debug_frames/ and cache/ as root,
# stdout and stderr separated inside the container, host busy % from /proc/stat, wall
# seconds, the container named for the run and reaped by that name alone, --cpus=2,
# --network none. No credential exists in the run's cfg/, so the client pushes nothing.
#
#   testers/i899_run.sh [container-bash-script-file]
#
# The default script is the one this issue is about: a board that loses every camera and
# gets them back, run twice -- once with nothing touched and once with the cameras 25 px
# from where they were -- and the mock footage beside it as the control.
set -u
SCRIPT="${1:-/home/mikko/opendartboard/i899/testers/phases899/899-recover.sh}"
BASE=/home/mikko/opendartboard/runs899
RUN="$BASE/recover"
mkdir -p "$BASE"
if [ -d "$RUN" ]; then
  docker run --rm --name od-i899-clean --network none -v "$BASE":/base od-amd64:bullseye \
    rm -rf /base/recover > /dev/null 2>&1
fi
rm -rf "$RUN" 2>/dev/null
mkdir -p "$RUN/cfg"
cp "$SCRIPT" "$RUN/inside.sh"

read -r _ u0 n0 s0 i0 w0 q0 sq0 rest < /proc/stat
T0=$(date +%s.%N)

docker run --rm --name od-i899-recover --cpus=2 --network none -e HOME=/root \
  -v /home/mikko/opendartboard/i899:/app \
  -v "$RUN":/run899 -v "$RUN/cfg":/root/.config \
  -w /run899 od-amd64:bullseye bash /run899/inside.sh
RC=$?

T1=$(date +%s.%N)
read -r _ u1 n1 s1 i1 w1 q1 sq1 rest < /proc/stat

BUSY=$(python3 -c "
u=$u1-$u0; n=$n1-$n0; s=$s1-$s0; i=$i1-$i0; w=$w1-$w0; q=$q1-$q0; sq=$sq1-$sq0
tot=u+n+s+i+w+q+sq
print('%.1f' % (100.0*(tot-i-w)/tot) if tot else 'n/a')")
WALL=$(python3 -c "print('%.1f' % ($T1-$T0))")

echo "RUN=recover rc=$RC wall_s=$WALL host_busy_pct=$BUSY dir=$RUN"
exit $RC
