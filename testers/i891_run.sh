#!/bin/bash
# #891's run harness, #892's with its own paths (and no still: nothing here makes a board blind). One run, one fresh directory, stdout and stderr to separate files,
# host busy % from /proc/stat over the command, wall seconds, and the container reaped by
# its own name and never by an image tag.
#
#   testers/i891_run.sh <label> <container-bash-script-file>
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"
LABEL="$1"
SCRIPT="$2"
BASE="$OD_RUNS_BASE/891"
RUN="$BASE/$LABEL"
# debug_frames/ and cache/ are written by root inside the container, so the host user
# cannot remove them. Reap the directory from a container instead, and never leave a
# previous run's cache or spool where this one would read it.
if [ -d "$RUN" ]; then
  docker run --rm --name "$(od_name "891-clean-$LABEL")" -v "$BASE":/base "$OD_IMAGE" \
    rm -rf "/base/$LABEL" > /dev/null 2>&1
fi
rm -rf "$RUN" 2>/dev/null
mkdir -p "$RUN/cfg"

cp "$SCRIPT" "$RUN/inside.sh"

read -r _ u0 n0 s0 i0 w0 q0 sq0 rest < /proc/stat
T0=$(date +%s.%N)

docker run --rm --name "$(od_name "891-$LABEL")" --cpus=3 -e HOME=/root \
  -v "$OD_TREE_ROOT":/app \
  -v "$RUN":/run891 -v "$RUN/cfg":/root/.config \
  -w /run891 "$OD_IMAGE" bash /run891/inside.sh
RC=$?

T1=$(date +%s.%N)
read -r _ u1 n1 s1 i1 w1 q1 sq1 rest < /proc/stat

BUSY=$(python3 -c "
u=$u1-$u0; n=$n1-$n0; s=$s1-$s0; i=$i1-$i0; w=$w1-$w0; q=$q1-$q0; sq=$sq1-$sq0
tot=u+n+s+i+w+q+sq
print('%.1f' % (100.0*(tot-i-w)/tot) if tot else 'n/a')")
WALL=$(python3 -c "print('%.1f' % ($T1-$T0))")

echo "RUN=$LABEL rc=$RC wall_s=$WALL host_busy_pct=$BUSY dir=$RUN"
# The harness must exit on what it measured: run_all.sh reads the exit code and
# nothing else, and an echo returns 0 whatever it printed (#1335).
exit $RC
