#!/bin/bash
# #1449's harness: #1372's shape with its own paths. One run, one fresh directory reaped
# from a container because the detector writes debug_frames/ and cache/ as root, host busy
# % from /proc/stat, wall seconds, the container named for the run and reaped by that name
# alone, --cpus=2, --network none -- which still has a loopback, which is where the Turnaus
# stub listens.
#
#   testers/i1449_run.sh [container-bash-script-file]
#
# The parent commit is unpacked HERE rather than inside the container, for #1330's reason:
# this is a git worktree, so its .git is a file pointing at a repository the container does
# not mount and `git -C /app archive` there cannot work. The phase script compiles it, and
# that is what makes the "before" a measurement rather than a claim -- the board this issue
# was filed about, on the binary that has the defect, on the same footage as the repair.
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"
HERE="$OD_TREE_ROOT"
SCRIPT="${1:-$HERE/testers/phases1449/1449-anchoring.sh}"
PHASE="$(od_phase "$SCRIPT")"
BASE="$OD_RUNS_BASE/1449"
RUN="$BASE/$PHASE"
mkdir -p "$BASE"
if [ -d "$RUN" ]; then
  od_run "1449-clean-$PHASE" --network none -v "$BASE":/base "$OD_IMAGE" \
    rm -rf "/base/$PHASE" > /dev/null 2>&1
fi
rm -rf "$RUN" 2>/dev/null
mkdir -p "$RUN/cfg"
cp "$SCRIPT" "$RUN/inside.sh"

# The branch point: the tree as it was before this issue, which is what phase BEFORE
# builds and runs. Overridable so the harness still means something after a rebase.
BASE_COMMIT="${BASE_COMMIT:-1a8a57a}"
mkdir -p "$RUN/pre-1449"
git -C "$HERE" archive "$BASE_COMMIT" | tar -x -C "$RUN/pre-1449"

read -r _ u0 n0 s0 i0 w0 q0 sq0 rest < /proc/stat
T0=$(date +%s.%N)

od_run "1449-$PHASE" --cpus=2 --network none -e HOME=/root \
  -v "$HERE":/app \
  -v "$RUN":/run1449 -v "$RUN/cfg":/root/.config \
  -w /run1449 "$OD_IMAGE" bash /run1449/inside.sh
RC=$?

T1=$(date +%s.%N)
read -r _ u1 n1 s1 i1 w1 q1 sq1 rest < /proc/stat

BUSY=$(python3 -c "
u=$u1-$u0; n=$n1-$n0; s=$s1-$s0; i=$i1-$i0; w=$w1-$w0; q=$q1-$q0; sq=$sq1-$sq0
tot=u+n+s+i+w+q+sq
print('%.1f' % (100.0*(tot-i-w)/tot) if tot else 'n/a')")
WALL=$(python3 -c "print('%.1f' % ($T1-$T0))")

echo "RUN=$PHASE rc=$RC wall_s=$WALL host_busy_pct=$BUSY dir=$RUN"
# The harness must exit on what it measured: run_all.sh reads the exit code and nothing
# else, and an echo returns 0 whatever it printed (#1335).
exit $RC
