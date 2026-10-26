#!/bin/bash
# turnaus#1904: open captures with OD_EXPOSURE / OD_GAIN set, through the class the detector
# opens them with (camera::OpenCvCaptureSource, via i1904_capture_probe.cpp), and read what
# comes back.
#
#   testers/i1904_run.sh                      the file half, here
#   OD_PROBE_DEVICES="0 1 2" OD_EXPOSURE=156 OD_GAIN=4 testers/i1904_run.sh
#                                             plus the device half, on a box with cameras
#
# File half (every box), PREDICTED:
#   A. two video files, pinned: the no-op line exactly ONCE, no per-camera exposure line,
#      no set() line, both opened
#   B. the same files, unset: no line naming OD_EXPOSURE at all -- the bakeoff's logs as
#      they were
#   C. a pin that is not a number: one WARN naming it, and nothing set
#
# Device half (only when OD_PROBE_DEVICES names devices, /dev/video<N> passed through):
#   D. each camera opened gets one per-start `Camera N exposure:` line, and with a pin set,
#      one `asked .., got ..` line per property. What the camera reports is printed, not
#      judged: the value a UVC camera rounds a pin to is the camera's business.
#
# The script ends on `exit`, never on an `echo`: #1463, #1479.
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"

DEVICES="${OD_PROBE_DEVICES:-}"
DEV_ARGS=()
for d in $DEVICES; do DEV_ARGS+=(--device "/dev/video$d"); done

od_run "1904-run" --network none "${DEV_ARGS[@]}" \
  -e OD_PROBE_DEVICES="$DEVICES" -e PIN_EXPOSURE="${OD_EXPOSURE:-156}" -e PIN_GAIN="${OD_GAIN:-4}" \
  -v "$OD_TREE_ROOT":/app:ro -w /app "$OD_IMAGE" bash -c '
  set -u
  g++ -std=c++17 -O1 -Wall -Wextra -I src -I src/utils -o /tmp/probe testers/i1904_capture_probe.cpp \
      $(pkg-config --cflags --libs opencv4) -lpthread > /tmp/build.log 2>&1 ||
    { tail -20 /tmp/build.log; echo "FAIL the probe did not build"; exit 2; }
  F1=mocks/rig-20260922/cam_1.mp4
  F2=mocks/rig-20260922/cam_2.mp4
  BAD=0
  count() { grep -c -- "$1" "$2"; }

  echo "---- A. two files, OD_EXPOSURE=156 OD_GAIN=4 ----"
  env OD_EXPOSURE=156 OD_GAIN=4 /tmp/probe $F1 $F2 > /tmp/a.log 2>&1
  noop=$(count "the pin is a no-op" /tmp/a.log); start=$(count "exposure: " /tmp/a.log)
  asked=$(count ": asked " /tmp/a.log); opened=$(count "I1904 opened=1" /tmp/a.log)
  echo "no-op lines=$noop (predicted 1), start lines=$start (0), set lines=$asked (0), opened=$opened (1)"
  [ "$noop" -eq 1 ] && [ "$start" -eq 0 ] && [ "$asked" -eq 0 ] && [ "$opened" -eq 1 ] || { echo "FAIL A"; BAD=1; }

  echo "---- B. the same files, unset ----"
  env -u OD_EXPOSURE -u OD_GAIN /tmp/probe $F1 $F2 > /tmp/b.log 2>&1
  named=$(grep -c -e "OD_EXPOSURE" -e "OD_GAIN" -e "exposure" /tmp/b.log); opened=$(count "I1904 opened=1" /tmp/b.log)
  echo "lines naming the pin or exposure=$named (predicted 0), opened=$opened (1)"
  [ "$named" -eq 0 ] && [ "$opened" -eq 1 ] || { echo "FAIL B"; BAD=1; }

  echo "---- C. OD_EXPOSURE=bright ----"
  env -u OD_GAIN OD_EXPOSURE=bright /tmp/probe $F1 > /tmp/c.log 2>&1
  warn=$(count "\"bright\" is not a number" /tmp/c.log); noop=$(count "the pin is a no-op" /tmp/c.log)
  echo "refusal lines=$warn (predicted 1), no-op lines=$noop (0)"
  [ "$warn" -eq 1 ] && [ "$noop" -eq 0 ] || { echo "FAIL C"; BAD=1; }

  if [ -n "$OD_PROBE_DEVICES" ]; then
    echo "---- D. devices $OD_PROBE_DEVICES, OD_EXPOSURE=$PIN_EXPOSURE OD_GAIN=$PIN_GAIN ----"
    n=0; for d in $OD_PROBE_DEVICES; do n=$((n + 1)); done
    env OD_EXPOSURE="$PIN_EXPOSURE" OD_GAIN="$PIN_GAIN" /tmp/probe $OD_PROBE_DEVICES > /tmp/d.log 2>&1
    grep -e "exposure" -e "gain" -e "I1904" /tmp/d.log
    start=$(count "exposure: " /tmp/d.log); opened=$(count "I1904 opened=1" /tmp/d.log)
    echo "start lines=$start (predicted $n), opened=$opened (1)"
    [ "$start" -eq "$n" ] && [ "$opened" -eq 1 ] || { echo "FAIL D"; BAD=1; }
  else
    echo "---- D. skipped: OD_PROBE_DEVICES names no camera on this box; the device half is the rig'"'"'s ----"
  fi
  [ $BAD -eq 0 ] || exit 1
  echo "I1904 RUN HELD"
  exit 0
'
exit $?
