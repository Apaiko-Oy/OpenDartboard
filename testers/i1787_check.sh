#!/bin/bash
# #1787: the board's full per-dart account, the log upload's bookkeeping and the ring of
# kept frames -- three pure checks, one container, each with its mutation inside and its
# prediction stated in its own header:
#
#   1. i1787_body_check.cpp     the account's fields in TurnausClient::detectionBody beside
#                               #1366's unchanged bytes; 29 one-field mutations named by field
#   2. i1787_upload_check.cpp   LogUploadLedger: the same bytes never twice, a failed post
#                               re-posts from the same offset; the count model shown to lose
#   3. i1787_frames_check.cpp   frame_keep: ten kept, dart 5 gone, dart 15 served with its
#                               census; the pin off keeps nothing; the bytes per dart MEASURED
#
# The include paths are unit_check.sh's, for unit_check.sh's reason; the body and frames
# checks link turnaus_client.cpp (detectionBody, frameAnswerBody) as i1366_position_check.py
# does, and -lpthread for its std::thread on bullseye's glibc. No detector binary, so
# OD_SKIP_BUILD changes nothing; the frames check reads mocks/rig-20260918/cam_1.mp4's
# first frame for a real settled-size picture and draws one if the clip cannot be opened.
#
# MEASURED (#1341's rule): 204 s wall on 2026-10-10 (the WALL line below prints it on every
# run); recorded in run_all.sh beside the row. The frames figure that run printed:
# raw_bytes_per_dart=2764800 png_bytes_per_dart=1158466 ring_of_10_MB=26.37.
#
# The script ends on `exit`, never on an `echo`: #1463, #1479.
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"

if [ ! -d "$OD_TREE_ROOT/build/_deps" ]; then
  echo "i1787_check: $OD_TREE_ROOT/build/_deps is not there; build this worktree first" >&2
  echo "             -- run_all.sh does that before it runs anything." >&2
  exit 2
fi

T0=$(date +%s)
od_run "1787-check" --network none \
  -v "$OD_TREE_ROOT":/app -w /app "$OD_IMAGE" bash -c '
  set -u
  FLAGS="-std=c++17 -O1 -Wall -Wextra -I src -I src/utils -I build/_deps/nlohmann_json-src/include -I build/_deps/httplib-src"
  LIBS="$(pkg-config --cflags --libs opencv4) -lpthread"
  RC=0
  for name in body upload frames; do
    echo "==== i1787_${name}_check ======================================================"
    g++ $FLAGS -o /tmp/i1787_${name}_check testers/i1787_${name}_check.cpp \
        src/communication/turnaus_client.cpp $LIBS || { echo "COMPILE_FAILED i1787_${name}_check"; exit 2; }
    /tmp/i1787_${name}_check < /dev/null
    rc=$?
    echo "i1787_${name}_check rc=$rc"
    [ $rc -eq 0 ] || RC=1
  done
  exit $RC
'
RC=$?
T1=$(date +%s)
echo "WALL $((T1 - T0)) s"
echo "CHECK_RC=$RC"
# The harness must exit on what it measured: run_all.sh reads the exit code and
# nothing else, and an echo returns 0 whatever it printed (#1335).
exit $RC
