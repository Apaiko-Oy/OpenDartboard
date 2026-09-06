#!/bin/bash
# #892's run harness, #822's with its own paths. One run, one fresh directory, stdout and stderr to separate files,
# host busy % from /proc/stat over the command, wall seconds, and the container reaped by
# its own name and never by an image tag.
#
#   testers/i822_run.sh <label> <container-bash-script-file>
set -u
LABEL="$1"
SCRIPT="$2"
BASE=/home/mikko/opendartboard/runs892
RUN="$BASE/$LABEL"
# debug_frames/ and cache/ are written by root inside the container, so the host user
# cannot remove them. Reap the directory from a container instead, and never leave a
# previous run's cache or spool where this one would read it.
if [ -d "$RUN" ]; then
  docker run --rm --name "i892-clean-$LABEL" -v "$BASE":/base od-amd64:bullseye \
    rm -rf "/base/$LABEL" > /dev/null 2>&1
fi
rm -rf "$RUN" 2>/dev/null
mkdir -p "$RUN/cfg"
cp /tmp/still892.jpg "$RUN/still.jpg"
cp "$SCRIPT" "$RUN/inside.sh"

read -r _ u0 n0 s0 i0 w0 q0 sq0 rest < /proc/stat
T0=$(date +%s.%N)

docker run --rm --name "i892-$LABEL" --cpus=3 -e HOME=/root \
  -v /home/mikko/opendartboard/i892:/app \
  -v "$RUN":/run892 -v "$RUN/cfg":/root/.config \
  -w /run892 od-amd64:bullseye bash /run892/inside.sh
RC=$?

T1=$(date +%s.%N)
read -r _ u1 n1 s1 i1 w1 q1 sq1 rest < /proc/stat

BUSY=$(python3 -c "
u=$u1-$u0; n=$n1-$n0; s=$s1-$s0; i=$i1-$i0; w=$w1-$w0; q=$q1-$q0; sq=$sq1-$sq0
tot=u+n+s+i+w+q+sq
print('%.1f' % (100.0*(tot-i-w)/tot) if tot else 'n/a')")
WALL=$(python3 -c "print('%.1f' % ($T1-$T0))")

echo "RUN=$LABEL rc=$RC wall_s=$WALL host_busy_pct=$BUSY dir=$RUN"
