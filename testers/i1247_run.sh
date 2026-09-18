#!/bin/bash
# #1247's run harness: #1257's shape (one run, one fresh directory, stdout and stderr
# separated inside the container, host busy % from /proc/stat, wall seconds, the container
# named od-i1247-<label> and reaped by that name alone), with two things made arguments so
# the earlier slices' own phase scripts run unedited on the combined tree:
#
#   MOUNT  where the run directory appears in the container. #822's scripts write to
#          /run822, #892's to /run892, #895's to /run895, #891's to /run891, #1257's
#          control to /run1257 and #1188's check to /runs.
#   NET    the docker network. `none` by default, as #1257 ran. #822's unreachable phases
#          post to 192.0.2.1 (RFC 5737 TEST-NET-1) and need `bridge` so that address
#          blackholes, as it did for #822, instead of failing at once. No phase names a
#          real Turnaus; every other address is the stub on the container's loopback.
#
# #892's and #895's blind phases copy a still JPEG; #892's harness read it from /tmp, which
# does not survive a reboot, so this one reads the copy #895's own dark run left behind.
#
#   NET=none testers/i1247_run.sh <label> <container-bash-script-file> <mount>
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"
LABEL="$1"
SCRIPT="$2"
MOUNT="$3"
NET="${NET:-none}"
BASE="$OD_RUNS_BASE/1247"
RUN="$BASE/$LABEL"
mkdir -p "$BASE"
if [ -d "$RUN" ]; then
  docker run --rm --name "$(od_name "i1247-clean-$LABEL")" --network none -v "$BASE":/base "$OD_IMAGE" \
    rm -rf "/base/$LABEL" > /dev/null 2>&1
fi
rm -rf "$RUN" 2>/dev/null
mkdir -p "$RUN/cfg"
od_still "$RUN/still.jpg" || exit 2
cp "$SCRIPT" "$RUN/inside.sh"

read -r _ u0 n0 s0 i0 w0 q0 sq0 rest < /proc/stat
T0=$(date +%s.%N)

docker run --rm --name "$(od_name "i1247-$LABEL")" --cpus=2 --network "$NET" -e HOME=/root \
  -v "$OD_TREE_ROOT":/app \
  -v "$RUN":"$MOUNT" -v "$RUN/cfg":/root/.config \
  -w "$MOUNT" "$OD_IMAGE" bash "$MOUNT/inside.sh"
RC=$?

T1=$(date +%s.%N)
read -r _ u1 n1 s1 i1 w1 q1 sq1 rest < /proc/stat

BUSY=$(python3 -c "
u=$u1-$u0; n=$n1-$n0; s=$s1-$s0; i=$i1-$i0; w=$w1-$w0; q=$q1-$q0; sq=$sq1-$sq0
tot=u+n+s+i+w+q+sq
print('%.1f' % (100.0*(tot-i-w)/tot) if tot else 'n/a')")
WALL=$(python3 -c "print('%.1f' % ($T1-$T0))")

echo "RUN=$LABEL rc=$RC wall_s=$WALL host_busy_pct=$BUSY net=$NET dir=$RUN"
