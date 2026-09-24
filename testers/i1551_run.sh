#!/bin/bash
# #1551's harness: admission at the coherence gate, five consecutive runs per fixture, as
# a command.
#
#   testers/run_all.sh 1551       build the tree and run this
#   testers/i1551_run.sh          run it against this checkout
#
# testers/i1551_inside.sh holds what is measured and the finding it enforces: the
# admitted-0.873 / refused-0.578 flip #1551 was filed on was two BINARIES, not two runs --
# DEBUG_SEEK_VIDEO moves the thirty-frame calibration window from the clip's opening to
# ~3 s in, and within either window every run is bit-identical. Five calibration-window
# runs of each rig fixture on one binary must produce five byte-identical admission
# transcripts, every wire-model line must say its margin, and the SAME binary under
# OD_SEEK_VIDEO=off must reproduce the flip (rig-20260922 camera 1 admitted at the
# opening) -- the mutation proof that the window is the whole cause.
#
# Eleven calibration-window runs, no whole-clip replay: OD_MAX_CYCLES=1 ends each run as
# soon as the constructor has calibrated, so the cost is the thirty averaged frames plus
# the further looks per run, minutes not tens of minutes.
#
# The script ends on `exit`, never on an `echo`: #1463, #1479.
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"

RUN="$OD_RUNS_BASE/1551"
if [ -d "$RUN" ]; then
  od_run "1551-clean" --network none -v "$OD_RUNS_BASE":/base "$OD_IMAGE" \
    rm -rf /base/1551 > /dev/null 2>&1
fi
rm -rf "$RUN" 2> /dev/null
mkdir -p "$RUN"

# A Windows checkout holds this directory's scripts with CRLF endings, and the
# container's bash reads a \r as part of the line (#1499's agent measured it).
# The copy the container runs is stripped, whatever checkout it came from.
tr -d '\r' < "$OD_TREE_ROOT/testers/i1551_inside.sh" > "$RUN/inside.sh"

od_run "1551" --cpus=2 --network none -e HOME=/root \
  -v "$OD_TREE_ROOT":/app -v "$RUN":/run1551 -w /run1551 \
  "$OD_IMAGE" bash /run1551/inside.sh 2>&1 | tee "$RUN/out.txt"
RC=${PIPESTATUS[0]}

echo "RUN=1551 rc=$RC dir=$RUN"
exit $RC
