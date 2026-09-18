#!/bin/bash
# #1319's harness. Nothing here opens a camera: this box has none, and the point of
# moving the three findings into pure functions is that none of them needs one.
#
#   testers/i1319_run.sh
#
# Three things happen.
#
#   1. testers/i1319_format_check.cpp is built and run. It asks every branch of
#      readFourCC(), formatFinding(), rateFinding(), busFinding(), and -- since the
#      negotiation half landed -- captureRateRequest(), rateFloorFinding() and
#      fourccRequestFinding() directly. The rate request is asked against a model of the
#      rig's own two modes rather than a camera: MSMF picks a mode by nearest frame rate
#      and never scores the subtype, so two rates and a request are the whole of the
#      arithmetic this issue turned on -- and the fix is a function of those two numbers
#      with no read-back and no second ask, which is the shape it had to take after a
#      re-ask hung the board on the third camera.
#
#   2. The three mutations that falsify it. A check nothing can fail is not evidence, so
#      the no-format branch is made unreachable and the empty-name sentence is shown
#      coming back, the rate comparison is made to compare a value against itself and
#      shown never firing, and the rate floor is dropped so that --fps reaches the camera
#      raw and the model camera is shown landing on the ten frames a second this issue is
#      about. They are applied to a COPY of src/utils, never to the worktree.
#
#   3. The end-to-end drive and its control. The fps finding is exercised through the
#      real capture path by a file source whose rate is known -- mocks/rig-20260918 is
#      30 fps -- asked for a different one, and then asked for its own rate and shown
#      silent. mocks/cam_*.mp4 is the control: it must still calibrate with no new
#      ERROR or WARN.
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"
APP=${APP:-$OD_TREE_ROOT}
BASE=${BASE:-$OD_RUNS_BASE/1319}
IMAGE=${IMAGE:-$OD_IMAGE}

mkdir -p "$BASE"
if [ -d "$BASE/run" ]; then
  docker run --rm --name "$(od_name "i1319-clean")" --network none -v "$BASE":/base "$IMAGE" \
    rm -rf /base/run > /dev/null 2>&1
fi
rm -rf "$BASE/run" 2>/dev/null
mkdir -p "$BASE/run/cfg"

docker run --rm --name "$(od_name "i1319-run")" --cpus=2 --network none -e HOME=/root \
  -v "$APP":/app -v "$BASE/run":/run1319 -v "$BASE/run/cfg":/root/.config \
  -w /run1319 "$IMAGE" bash /app/testers/i1319_inside.sh
RC=$?
echo "RUN=i1319 rc=$RC dir=$BASE/run"
exit $RC
