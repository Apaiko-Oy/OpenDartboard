#!/bin/bash
# #1319's harness. Nothing here opens a camera: this box has none, and the point of
# moving the three findings into pure functions is that none of them needs one.
#
#   testers/i1319_run.sh
#
# Three things happen.
#
#   1. testers/i1319_format_check.cpp is built and run. It asks every branch of
#      readFourCC(), formatFinding(), rateFinding() and busFinding() directly.
#
#   2. The two mutations that falsify it. A check nothing can fail is not evidence, so
#      the no-format branch is made unreachable and the empty-name sentence is shown
#      coming back, and the rate comparison is made to compare a value against itself
#      and shown never firing. They are applied to a COPY of src/utils, never to the
#      worktree.
#
#   3. The end-to-end drive and its control. The fps finding is exercised through the
#      real capture path by a file source whose rate is known -- mocks/rig-20260918 is
#      30 fps -- asked for a different one, and then asked for its own rate and shown
#      silent. mocks/cam_*.mp4 is the control: it must still calibrate with no new
#      ERROR or WARN.
set -u
APP=${APP:-/home/mikko/opendartboard/i1319}
BASE=${BASE:-/home/mikko/opendartboard/runs1319}
IMAGE=${IMAGE:-od-amd64:bullseye}

mkdir -p "$BASE"
if [ -d "$BASE/run" ]; then
  docker run --rm --name od-i1319-clean --network none -v "$BASE":/base "$IMAGE" \
    rm -rf /base/run > /dev/null 2>&1
fi
rm -rf "$BASE/run" 2>/dev/null
mkdir -p "$BASE/run/cfg"

docker run --rm --name od-i1319-run --cpus=2 --network none -e HOME=/root \
  -v "$APP":/app -v "$BASE/run":/run1319 -v "$BASE/run/cfg":/root/.config \
  -w /run1319 "$IMAGE" bash /app/testers/i1319_inside.sh
RC=$?
echo "RUN=i1319 rc=$RC dir=$BASE/run"
exit $RC
