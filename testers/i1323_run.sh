#!/bin/bash
# #1323's harness, in #1319's shape: one container, named for the run and reaped by that
# name alone, --cpus=2, --network none. No credential exists in the run's cfg/, so the
# client pushes nothing.
#
#   testers/i1323_run.sh
#   APP=/path/to/worktree BASE=/path/to/runs testers/i1323_run.sh
#
# It needs a built detector at $APP/build-dev/opendartboard, built WITH -DDEBUG_SEEK_VIDEO:
#
#   cmake -S . -B build-dev -DCMAKE_PREFIX_PATH=/usr/local -DCMAKE_CXX_FLAGS="-DDEBUG_SEEK_VIDEO"
#   cmake --build build-dev -- -j4
#
# The define is not a detail here and it is worth saying out loud. It seeks a file source
# past its first three seconds, so a run with it starts at frame 45 of the mocks and a run
# without it starts at frame 0 -- and the two disagree about the answer. #1320's six bull
# centres are the frame-45 ones; at frame 0 the same build puts camera 1 a pixel higher and
# rig-20260918's camera 3 fails #1320's 4%-of-the-frame board gate outright. Every number
# in this file is a frame-45 number.
#
# Five things happen, and the last three are there because a window nothing can fail is
# not evidence:
#
#   1. The two rigs, as they ship. Both calibrate, neither says ERROR or WARN, and all six
#      bull centres are the ones #1320 recorded.
#
#   2. The camera this issue is about: the same mock footage shifted 180 px right and 90
#      px down, so the middle of the frame and the middle of the board are 170 px apart.
#      Its bull must be found, and found at the control's bull plus that shift.
#
#   3. A speck painted 105 px from the middle of the FRAME and 275 px from the middle of
#      the BOARD -- off the board altogether. It must not survive colour processing.
#
#   4. FALSIFY, three arms, each built from a COPY of src/ and never from the worktree:
#      the rule put back on the frame (which is the code before #1323, and it loses the
#      bull and keeps the speck); the window made to always keep (the speck comes back);
#      and the window made to always drop (the control loses a bull).
set -u
APP=${APP:-/home/mikko/opendartboard/i1323}
BASE=${BASE:-/home/mikko/opendartboard/runs1323}
IMAGE=${IMAGE:-od-amd64:bullseye}

if [ ! -x "$APP/build-dev/opendartboard" ]; then
  echo "no detector at $APP/build-dev/opendartboard -- see the header of this file"
  exit 2
fi

mkdir -p "$BASE"
if [ -d "$BASE/run" ]; then
  docker run --rm --name od-i1323-clean --network none -v "$BASE":/base "$IMAGE" \
    rm -rf /base/run > /dev/null 2>&1
fi
rm -rf "$BASE/run" 2>/dev/null
mkdir -p "$BASE/run/cfg"

docker run --rm --name od-i1323-run --cpus=2 --network none -e HOME=/root \
  -v "$APP":/app -v "$BASE/run":/run1323 -v "$BASE/run/cfg":/root/.config \
  -w /run1323 "$IMAGE" bash /app/testers/i1323_inside.sh
RC=$?
echo "RUN=i1323 rc=$RC dir=$BASE/run"
exit $RC
