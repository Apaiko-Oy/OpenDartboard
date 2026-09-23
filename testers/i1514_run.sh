#!/bin/bash
# #1514's harness: the rig-20260922 stall census, as a command.
#
#   testers/run_all.sh 1514       build the tree and run this
#   testers/i1514_run.sh          run it against this checkout
#
# One detector run of mocks/rig-20260922 to the end of its footage with the
# window census on, then the census of what every completed window voted.
# testers/i1514_inside.sh holds what is measured and the finding it was filed
# on: a dart parked in the board at calibration was pulled before the first
# throw, the CLEAN test's cumulative figure carried its silhouette for ever,
# no takeout could be reconciled and the board wedged at DART_3 with no END.
#
# Since #1518 repaired the reference, this is a CHECK as well as a census: it
# fails on a run it could not READ (#1322, i1484's rule, unchanged), and on
# either of the stall's two figures reading zero again -- no window with any
# camera CLEAN, or no END over the whole clip. The inside script says why and
# names the mutation proof.
#
# The script ends on `exit`, never on an `echo`: #1463, #1479.
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"

RUN="$OD_RUNS_BASE/1514"
if [ -d "$RUN" ]; then
  od_run "1514-clean" --network none -v "$OD_RUNS_BASE":/base "$OD_IMAGE" \
    rm -rf /base/1514 > /dev/null 2>&1
fi
rm -rf "$RUN" 2> /dev/null
mkdir -p "$RUN"

# A Windows checkout holds this directory's scripts with CRLF endings, and the
# container's bash reads a \r as part of the line (#1499's agent measured it).
# The copy the container runs is stripped, whatever checkout it came from.
tr -d '\r' < "$OD_TREE_ROOT/testers/i1514_inside.sh" > "$RUN/inside.sh"

od_run "1514" --cpus=2 --network none -e HOME=/root \
  -v "$OD_TREE_ROOT":/app -v "$RUN":/run1514 -w /run1514 \
  "$OD_IMAGE" bash /run1514/inside.sh 2>&1 | tee "$RUN/out.txt"
RC=${PIPESTATUS[0]}

echo "RUN=1514 rc=$RC dir=$RUN"
exit $RC
