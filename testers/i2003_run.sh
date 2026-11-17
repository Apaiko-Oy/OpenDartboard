#!/bin/bash
# turnaus#2003: the club's Harrows board calibrates on all three cameras, off the board's
# own start-up pictures of the evening camera 1 was set aside (testers/fixtures2003/).
# i2003_inside.sh holds what is asserted. It compiles the calibration from the tree's
# source and measures no detector binary, so OD_SKIP_BUILD changes nothing about it.
#
#   testers/i2003_run.sh              this tree
#   testers/i2003_run.sh <tree-root>  the same pictures and the same assertions against
#                                     another tree's src/ -- the mutation proof's control
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"

RUN="$OD_RUNS_BASE/2003"
docker rm -f "$(od_name i2003)" > /dev/null 2>&1
rm -rf "$RUN" 2>/dev/null
mkdir -p "$RUN"

MOUNT=()
INSIDE_SRC=/app
if [ -n "${1:-}" ]; then
  MOUNT=(-v "$(cd "$1" && pwd)":/other:ro)
  INSIDE_SRC=/other
fi

START=$(date +%s)
od_run "i2003" --cpus=2 --network none -v "$OD_TREE_ROOT":/app:ro "${MOUNT[@]}" -v "$RUN":/run2003 \
  "$OD_IMAGE" bash /app/testers/i2003_inside.sh "$INSIDE_SRC" 2>&1 | tee "$RUN/out.txt"
RC=${PIPESTATUS[0]}
END=$(date +%s)

echo "RUN=2003 rc=$RC seconds=$((END - START)) load_at_end=$(cut -d' ' -f1 /proc/loadavg)"
exit $RC
