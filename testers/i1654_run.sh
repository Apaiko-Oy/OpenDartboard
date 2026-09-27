#!/bin/bash
# #1654's census: the wire comb's theta20 and 12/9 wire on the averaged frame and on each
# of the looks a calibration takes, per camera, fixture and window.
#
#   testers/i1654_run.sh     run it against this checkout's src/ (no detector binary)
#
# testers/i1654_comb_census.cpp says what each column is. Calibration-only: no replay.
# The script ends on `exit`, never on an `echo`: #1463, #1479.
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"
RUN="$OD_RUNS_BASE/1654${OD_COMB_CENTRE:+-$OD_COMB_CENTRE}"
if [ -d "$RUN" ]; then
  od_run "1654-clean" --network none -v "$OD_RUNS_BASE":/base "$OD_IMAGE" \
    rm -rf "/base/$(basename "$RUN")" > /dev/null 2>&1
fi
rm -rf "$RUN" 2> /dev/null
mkdir -p "$RUN"
tr -d '\r' < "$OD_TREE_ROOT/testers/i1654_inside.sh" > "$RUN/inside.sh"
T0=$(date +%s)
od_run "1654" --cpus=2 --network none -e HOME=/root -e OD_COMB_CENTRE="${OD_COMB_CENTRE:-}" -e OD_I1654_FIXTURES="${OD_I1654_FIXTURES:-rig-20260922 rig-20260918}" \
  -v "$OD_TREE_ROOT":/app -v "$RUN":/run1654 -w /run1654 \
  "$OD_IMAGE" bash /run1654/inside.sh 2>&1 | tee "$RUN/out.txt"
RC=${PIPESTATUS[0]}
echo "RUN=1654 rc=$RC wall_s=$(( $(date +%s) - T0 )) dir=$RUN"
exit $RC
