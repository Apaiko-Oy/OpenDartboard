#!/bin/bash
# #1407's Linux harness: the colour stage's outer cutoff is drawn at the board's RIM, in
# board radii, against the length #1423's ring identity licenses -- not at 0.6 of the frame.
#
#   testers/run_all.sh 1407       build the tree and run this
#   testers/i1407_run.sh          run it against whatever is in build/
#
# #1394's shape and #1394's instrument: it does not build, it measures
# $OD_TREE_ROOT/build/opendartboard (a DEV build -- DEBUG_SEEK_VIDEO), and it reads what the
# cutoff alone keeps and drops from the stage's own census rather than from the ellipse two
# stages down. Every detector run is bounded by its own recorded pid, because a board that
# cannot calibrate stays up on purpose (#895) and the half-size phase hands the detector one
# camera, which always faults.
#
# The script ends on `exit`, never on an `echo`: #1463.
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"
SCRIPT="${1:-$OD_TREE_ROOT/testers/phases1407/1407-cutoff.sh}"
BASE="$OD_RUNS_BASE/1407"
RUN="$BASE/cutoff"
mkdir -p "$BASE"
if [ -d "$RUN" ]; then
  docker run --rm --name "$(od_name "i1407-clean")" --network none -v "$BASE":/base "$OD_IMAGE" \
    rm -rf /base/cutoff > /dev/null 2>&1
fi
rm -rf "$RUN" 2>/dev/null
mkdir -p "$RUN/cfg"
cp "$SCRIPT" "$RUN/inside.sh"

START=$(date +%s)
docker run --rm --name "$(od_name "i1407-cutoff")" --cpus=2 --network none -e HOME=/root \
  -v "$OD_TREE_ROOT":/app \
  -v "$RUN":/run1407 -v "$RUN/cfg":/root/.config \
  -w /run1407 "$OD_IMAGE" bash /run1407/inside.sh
RC=$?
END=$(date +%s)

echo "RUN=1407 rc=$RC seconds=$((END - START)) load_at_end=$(cut -d' ' -f1 /proc/loadavg) dir=$RUN"
exit $RC
