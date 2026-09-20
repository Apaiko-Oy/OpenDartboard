#!/bin/bash
# #1371: compile and run one of this repository's PURE checks in the Linux container.
#
# A pure check is a translation unit that includes a detector header, asserts what the
# functions in it do, prints one OK or FAIL line per assertion and exits on the count. It
# needs no camera, no footage, no network and no detector process, so it costs a compile
# and a few milliseconds -- which is why six of them accumulated in this directory with
# nothing running them (#1371). This is the harness that runs them, and it is general on
# purpose: a slice that writes another pure check adds a row below and a line to
# run_all.sh, rather than a seventh copy of this file.
#
#   testers/unit_check.sh <name>          one of the names in the table below
#
# The include paths are the ones CMakeLists.txt gives the real build, so a check compiles
# against exactly the headers the detector does. nlohmann/json and cpp-httplib are fetched
# by CMake into build/_deps, so this needs a Linux build of this worktree to have happened
# first -- i1258_check.sh has the same requirement for the same reason, and run_all.sh
# builds before it runs anything.
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"

NAME="${1:-}"

# name        source                              extra translation units it needs
# ----------  ----------------------------------  ---------------------------------------
case "$NAME" in
  1346) SRC=i1346_vote_check.cpp;        EXTRA= ;;
  1347) SRC=i1347_sector_check.cpp;      EXTRA= ;;
  1349) SRC=i1349_background_check.cpp;  EXTRA=src/detector/geometry/detection/dart_processing.cpp ;;
  1350) SRC=i1350_vote_line_check.cpp;   EXTRA= ;;
  1351) SRC=i1351_ledger_check.cpp;      EXTRA= ;;
  1363) SRC=i1363_anchor_check.cpp;      EXTRA= ;;
  # wire_model.cpp is here for the reason #1447 wrote down one issue earlier: since #1467
  # merged, wire_processing.cpp calls into `wire_model::` in eight places and only #1467's
  # own build lines were moved -- so 1451-scorable has not LINKED since (`undefined
  # reference to wire_model::minimumCoherence()`, COMPILE_FAILED, rc=2). A tester that
  # cannot be built is neither a pass nor a failure and reads as neither. Measured on
  # origin/main and on origin/issue-1485 alike while carrying #1489.
  1451) SRC=i1451_scoring_check.cpp;     EXTRA="src/detector/geometry/calibration/wire_processing.cpp src/detector/geometry/calibration/wire_model.cpp" ;;
  *)
    echo "unit_check: '$NAME' is not a pure check here; they are 1346 1347 1349 1350 1351 1363 1451" >&2
    exit 2
    ;;
esac

if [ ! -d "$OD_TREE_ROOT/build/_deps" ]; then
  echo "unit_check: $OD_TREE_ROOT/build/_deps is not there, so the headers CMake fetches" >&2
  echo "            (nlohmann/json, cpp-httplib) cannot be included. Build this worktree" >&2
  echo "            first -- run_all.sh does that before it runs anything." >&2
  exit 2
fi

od_run "unit-$NAME" --network none \
  -v "$OD_TREE_ROOT":/app -w /app "$OD_IMAGE" bash -c '
  set -u
  g++ -std=c++17 -O1 -Wall -Wextra \
      -I src -I src/utils \
      -I build/_deps/nlohmann_json-src/include \
      -I build/_deps/httplib-src \
      -o /tmp/unit_check "testers/'"$SRC"'" '"$EXTRA"' \
      $(pkg-config --cflags --libs opencv4) || { echo "COMPILE_FAILED"; exit 2; }
  /tmp/unit_check < /dev/null
'
RC=$?
echo "CHECK_RC=$RC"
# The harness must exit on what it measured: run_all.sh reads the exit code and
# nothing else, and an echo returns 0 whatever it printed (#1335).
exit $RC
