#!/bin/bash
# #1474's harness: #1451's shape with its own paths and WITHOUT its branch-point build.
# One run, one fresh directory reaped from a container because the detector writes
# debug_frames/ and cache/ as root, host busy % from /proc/stat, wall seconds, the
# container named for the run and reaped by that name alone, --cpus=2, --network none --
# which still has a loopback, which is where the Turnaus stub listens.
#
#   testers/i1474_run.sh [container-bash-script-file]
#
# NO BASE_COMMIT HERE, and that is a decision rather than a shortcut. #1451 had to compile
# its branch point because the thing it measured was a SILENCE, and only a build without
# the change in it can be shown to be silent. This issue's falsifier is a switch on the
# same binary -- OD_BEAT_CAMERAS=0 restores the pre-#1474 body, byte for byte -- which is
# the stronger control of the two: "a different build" is never a confound, and it is also
# the fleet mid-upgrade, which the server contract has to go on accepting. It also removes
# the failure README rule 6 names: a pinned base commit that goes on compiling a tree the
# branch no longer contains, and passes green.
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"
HERE="$OD_TREE_ROOT"
SCRIPT="${1:-$HERE/testers/phases1474/1474-census.sh}"
PHASE="$(od_phase "$SCRIPT")"
BASE="$OD_RUNS_BASE/1474"
RUN="$BASE/$PHASE"
mkdir -p "$BASE"
if [ -d "$RUN" ]; then
  od_run "1474-clean-$PHASE" --network none -v "$BASE":/base "$OD_IMAGE" \
    rm -rf "/base/$PHASE" > /dev/null 2>&1
fi
rm -rf "$RUN" 2>/dev/null
mkdir -p "$RUN/cfg"
cp "$SCRIPT" "$RUN/inside.sh"

read -r _ u0 n0 s0 i0 w0 q0 sq0 rest < /proc/stat
T0=$(date +%s.%N)

od_run "1474-$PHASE" --cpus=2 --network none -e HOME=/root \
  -v "$HERE":/app \
  -v "$RUN":/run1474 -v "$RUN/cfg":/root/.config \
  -w /run1474 "$OD_IMAGE" bash /run1474/inside.sh
RC=$?

T1=$(date +%s.%N)
read -r _ u1 n1 s1 i1 w1 q1 sq1 rest < /proc/stat

BUSY=$(python3 -c "
u=$u1-$u0; n=$n1-$n0; s=$s1-$s0; i=$i1-$i0; w=$w1-$w0; q=$q1-$q0; sq=$sq1-$sq0
tot=u+n+s+i+w+q+sq
print('%.1f' % (100.0*(tot-i-w)/tot) if tot else 'n/a')")
WALL=$(python3 -c "print('%.1f' % ($T1-$T0))")

# README rule 5: a quiet box at the start is not a quiet box throughout, and a timeout with
# no load reading beside it is not evidence of anything.
LOAD_AT_END=$(cut -d' ' -f1 /proc/loadavg)

echo "RUN=$PHASE rc=$RC wall_s=$WALL host_busy_pct=$BUSY load_at_end=$LOAD_AT_END dir=$RUN"
# The harness must exit on what it measured: run_all.sh reads the exit code and nothing
# else, and an echo returns 0 whatever it printed (#1335, #1463).
exit $RC
