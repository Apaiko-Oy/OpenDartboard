#!/bin/bash
# #1473's run harness: do two boards on one host both serve 13520?
#
# i1295_run.sh's shape, which is i1274_run.sh's (one run, one fresh directory reaped from
# a container, the container named od-<checkout>-i1473-<phase> and reaped by that name
# alone, --cpus=2, --network none, the host's busy % from /proc/stat and the wall seconds
# beside it, and the load sampled DURING the run and printed as load_at_end -- README
# rule 5). One difference: the phase starts up to two detectors at once, so it is the
# hungriest thing here for its length and --cpus=2 is doing real work.
#
# --network none is loopback and nothing else, which is all this needs: the boards bind
# 0.0.0.0:13520 inside the container, the probe connects to 127.0.0.1:13520, and the lock
# is an abstract socket in the container's own network namespace -- which is precisely
# why two boards in two containers are two hosts and do not collide.
#
#   testers/run_all.sh 1473        build the tree and run this
#   testers/i1473_run.sh           run it against whatever is already in build/
#
#   APP=<tree>                     the tree mounted at /app, so the same phase script
#                                  measures a mutated or a baseline build without an edit.
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"
PHASE="${1:-$OD_TREE_ROOT/testers/phases1473/1473-lock.sh}"
LABEL="$(od_phase "$PHASE")"
APP="${APP:-$OD_TREE_ROOT}"
NET="${NET:-none}"
BASE="$OD_RUNS_BASE/1473"
RUN="$BASE/$LABEL"

if [ ! -x "$APP/build/opendartboard" ]; then
  echo "no detector at $APP/build/opendartboard -- run testers/run_all.sh 1473, which builds it" >&2
  exit 2
fi
if [ ! -f "$PHASE" ]; then
  echo "no phase script at $PHASE" >&2
  exit 2
fi

mkdir -p "$BASE"
# The detector writes cache/ as root, so the previous run is removed from a container.
if [ -d "$RUN" ]; then
  od_run "i1473-clean-$LABEL" --network none -v "$BASE":/base "$OD_IMAGE" \
    rm -rf "/base/$LABEL" > /dev/null 2>&1
fi
rm -rf "$RUN" 2> /dev/null
mkdir -p "$RUN/cfg"
cp "$PHASE" "$RUN/inside.sh"

read -r _ u0 n0 s0 i0 w0 q0 sq0 rest < /proc/stat
T0=$(date +%s.%N)

# Sampled while the run is going, in its own session so a kill aimed at this process group
# does not take it along. The harness reads the last sample it wrote.
SAMPLES="$RUN/load.samples"
setsid bash -c 'while true; do cut -d" " -f1-3 /proc/loadavg >> "$1"; sleep 15; done' \
  od-i1473-load "$SAMPLES" < /dev/null > /dev/null 2>&1 &
SAMPLER=$!

od_run "i1473-$LABEL" --cpus=2 --network "$NET" -e HOME=/root \
  -v "$APP":/app \
  -v "$RUN":/run1473 -v "$RUN/cfg":/root/.config \
  -w /run1473 "$OD_IMAGE" bash /run1473/inside.sh
RC=$?

kill "$SAMPLER" > /dev/null 2>&1
T1=$(date +%s.%N)
read -r _ u1 n1 s1 i1 w1 q1 sq1 rest < /proc/stat

BUSY=$(python3 -c "
u=$u1-$u0; n=$n1-$n0; s=$s1-$s0; i=$i1-$i0; w=$w1-$w0; q=$q1-$q0; sq=$sq1-$sq0
tot=u+n+s+i+w+q+sq
print('%.1f' % (100.0*(tot-i-w)/tot) if tot else 'n/a')")
WALL=$(python3 -c "print('%.1f' % ($T1-$T0))")
LOAD_END="$(tail -1 "$SAMPLES" 2> /dev/null | cut -d' ' -f1)"
LOAD_MAX="$(cut -d' ' -f1 "$SAMPLES" 2> /dev/null | sort -g | tail -1)"

echo "RUN=$LABEL app=$APP rc=$RC wall_s=$WALL host_busy_pct=$BUSY net=$NET dir=$RUN"
echo "LOAD load_at_end=${LOAD_END:-n/a} load_max_during=${LOAD_MAX:-n/a} nproc=$(nproc)"
# The harness exits on what it measured. run_all.sh reads the exit code and nothing else,
# and an echo returns 0 whatever it printed (#1335, #1463).
exit $RC
