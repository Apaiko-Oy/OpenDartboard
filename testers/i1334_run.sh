#!/bin/bash
# #1334's control: a board with no network still starts, still opens its cameras and still
# spools. #1331's harness shape with #1335's paths -- one run, one fresh directory reaped
# from a container because the detector writes cache/ as root, the container named for the
# checkout and reaped by that name alone, --cpus=2, --network none.
#
#   testers/run_all.sh 1334        build the tree and run this
#   testers/i1334_run.sh           run it against whatever is in build/
#
# Why a unit-file change ships a detector tester. #1334 puts Wants=network-online.target
# and After=network-online.target on opendartboard.service, and the thing that must not
# break is the board that has no network at all. Half of that is a question about systemd
# -- does a weak Wants= on a target whose wait-online provider fails still let the unit
# run -- and the units tester beside this one measures it on a live manager. Its name is
# i1334_units, and the extension is left off deliberately: #1371's census reads any
# mention of a program's filename in a reachable file as a call to it, prose included,
# and would then report that tester's unrun-tester marker as stale.
#
# The other half is the question about the detector, and it is this one: with no
# interface but loopback and nothing listening on it, the board must still come up, see
# darts and write down what it saw.
#
# It is not a claim that this branch changed any of that. It is the control that says the
# branch did not, which is what the issue asked for in those words.
#
# It measures $OD_TREE_ROOT/build/opendartboard and does not build it, which is run_all's
# job. The build matters here as everywhere in this directory: every number belongs to a
# build carrying DEBUG_SEEK_VIDEO, which seeks a file source three seconds in, so this
# calibrates on frame 90 of the mocks where a release binary calibrates on frame 0.
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"
BASE="$OD_RUNS_BASE/1334"
RUN="$BASE/networkless"

if [ ! -x "$OD_TREE_ROOT/build/opendartboard" ]; then
  echo "no detector at $OD_TREE_ROOT/build/opendartboard -- run testers/run_all.sh 1334, which builds it"
  exit 2
fi

mkdir -p "$BASE"
if [ -d "$RUN" ]; then
  docker run --rm --name "$(od_name "i1334-clean")" --network none -v "$BASE":/base "$OD_IMAGE" \
    rm -rf /base/networkless > /dev/null 2>&1
fi
rm -rf "$RUN" 2> /dev/null
mkdir -p "$RUN/cfg"

read -r _ u0 n0 s0 i0 w0 q0 sq0 rest < /proc/stat
T0=$(date +%s.%N)

# --network none is the whole point: loopback and nothing else. The cfg mount is fresh, so
# the board starts unpaired and with no spool, which phase 1 of the inside script relies on.
docker run --rm --name "$(od_name "i1334-networkless")" --cpus=2 --network none -e HOME=/root \
  -v "$OD_TREE_ROOT":/app \
  -v "$RUN":/run1334 -v "$RUN/cfg":/root/.config \
  -w /run1334 "$OD_IMAGE" bash /app/testers/i1334_inside.sh
RC=$?

T1=$(date +%s.%N)
read -r _ u1 n1 s1 i1 w1 q1 sq1 rest < /proc/stat

BUSY=$(python3 -c "
u=$u1-$u0; n=$n1-$n0; s=$s1-$s0; i=$i1-$i0; w=$w1-$w0; q=$q1-$q0; sq=$sq1-$sq0
tot=u+n+s+i+w+q+sq
print('%.1f' % (100.0*(tot-i-w)/tot) if tot else 'n/a')")
WALL=$(python3 -c "print('%.1f' % ($T1-$T0))")

echo "RUN=networkless rc=$RC wall_s=$WALL host_busy_pct=$BUSY net=none dir=$RUN"
# The harness must exit on what it measured: run_all.sh reads the exit code and nothing
# else, and an echo returns 0 whatever it printed (#1335).
exit $RC
