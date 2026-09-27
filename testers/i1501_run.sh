#!/bin/bash
# #1501: whether the board says, once and at default level, which board it is looking at --
# the one supported board's shape (Winmau Blade 6), another board scored best-effort, or a
# board it does not recognise, with OD_CAMERA_WEDGES named as the remedy. See
# testers/i1501_inside.sh for what is asserted and why.
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"

RUN="$OD_RUNS_BASE/1501"
if [ -d "$RUN" ]; then
  od_run "1501-clean" --network none -v "$OD_RUNS_BASE":/base "$OD_IMAGE" \
    rm -rf /base/1501 > /dev/null 2>&1
fi
rm -rf "$RUN" 2> /dev/null
mkdir -p "$RUN"

tr -d '\r' < "$OD_TREE_ROOT/testers/i1501_inside.sh" > "$RUN/inside.sh"

od_run "1501" --cpus=2 --network none -e HOME=/root \
  -v "$OD_TREE_ROOT":/app -v "$RUN":/run1501 -w /run1501 \
  "$OD_IMAGE" bash /run1501/inside.sh 2>&1 | tee "$RUN/out.txt"
RC=${PIPESTATUS[0]}

echo "RUN=1501 rc=$RC dir=$RUN"
exit $RC
