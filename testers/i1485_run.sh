#!/bin/bash
# #1485's harness: what radius is a dart judged against, and which ring decides it?
#
#   testers/run_all.sh 1485       build the tree and run this
#   testers/i1485_run.sh          run it against this checkout
#
# Sections 1-4 and 6 need NO detector binary: they compile the calibration stages and
# `score_processing::scorePoint` directly and ask the shipped decision itself. Section 5
# runs `build/opendartboard` over the whole of `mocks/rig-20260918/` -- so OD_SKIP_BUILD
# does change what it measures, the same way it does for every other detector tester
# here. (Its OD_RINGS=asfitted 'before' run was retired by #1515: 77bb5b1 mended the
# fits the switch used to expose, so it changes nothing on this footage any more;
# section 4 reproduces the defect by undoing 77bb5b1 in a planted tree instead.)
#
# The script ends on `exit`, never on an `echo`: #1463.
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"

RUN="$OD_RUNS_BASE/1485"
docker rm -f "$(od_name i1485)" > /dev/null 2>&1
rm -rf "$RUN" 2>/dev/null
mkdir -p "$RUN"

START=$(date +%s)
od_run "i1485" --cpus=2 --network none -v "$OD_TREE_ROOT":/app -v "$RUN":/run1485 \
  "$OD_IMAGE" bash /app/testers/i1485_inside.sh 2>&1 | tee "$RUN/out.txt"
RC=${PIPESTATUS[0]}
END=$(date +%s)

# #1463 and README rule 5: a timeout with no load reading beside it is not evidence of
# anything, so the load at the END of the run is printed whatever happened.
echo "RUN=1485 rc=$RC seconds=$((END - START)) load_at_end=$(cut -d' ' -f1 /proc/loadavg)"
exit $RC
