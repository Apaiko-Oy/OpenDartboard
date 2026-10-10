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
  1477) SRC=i1477_probe_format_check.cpp; EXTRA= ;;
  1336) SRC=i1336_probe_admission_check.cpp; EXTRA= ;;
  1517) SRC=i1517_vote_check.cpp;        EXTRA= ;;
  1510) SRC=i1510_board_check.cpp;       EXTRA=src/detector/geometry/calibration/wire_model.cpp ;;
  1510p2) SRC=i1510p2_model_check.cpp;   EXTRA=src/detector/geometry/calibration/wire_model.cpp ;;
  1553) SRC=i1553_bloom_check.cpp;       EXTRA=src/detector/geometry/calibration/wire_model.cpp ;;
  1511) SRC=i1511_axis_check.cpp;        EXTRA= ;;
  # #1648: a re-report whose cumulative figure fell is a departure; its sign guard.
  1648) SRC=i1648_departure_check.cpp;   EXTRA= ;;
  # #1554: the cast-shadow subtraction, on figures whose lighting is built.
  1554) SRC=i1554_shadow_check.cpp;      EXTRA= ;;
  # #1560: the lens-census model -- control, mutation, and the trap as arithmetic.
  1560) SRC=i1560_lens_check.cpp;        EXTRA= ;;
  # #1586: the composite rescue of a not-straight figure, on figures whose truth is built.
  1586) SRC=i1586_rescue_check.cpp;      EXTRA= ;;
  # #1649: the fresh-diff morphology translates the figure by (+4, +4) px; the axis undoes it.
  1649) SRC=i1649_unshift_check.cpp;     EXTRA= ;;
  # #1652: the same chain, translation-free at its source (OD_MASK_UNSHIFT=on).
  1652) SRC=i1652_mask_check.cpp;        EXTRA= ;;
  # wire_model.cpp for row 1510's reason: entry_intersection.hpp includes board_model.hpp,
  # whose fit calls into wire_model:: at link time.
  1512) SRC=i1512_intersect_check.cpp;   EXTRA=src/detector/geometry/calibration/wire_model.cpp ;;
  # #1681: the solve's redundancy numbers and the tip they ask for; row 1512's closure.
  # #1678: the lone-camera corroboration rule (OD_LONE_CAMERA=on), pure.
  1678) SRC=i1678_lone_check.cpp;       EXTRA= ;;
  1681) SRC=i1681_control_check.cpp;     EXTRA=src/detector/geometry/calibration/wire_model.cpp ;;
  # #1766: a two-line solve's across-wire sigma is its two lines' crossing angle, the live
  # 1.08-sigma "clears" rebuilt on planted boards, and the sentence that now says whose
  # sigma it is. Reaches score_processing.hpp too (pure) -- wire_model.cpp for row 1512's reason.
  1766) SRC=i1766_twoline_check.cpp;     EXTRA=src/detector/geometry/calibration/wire_model.cpp ;;
  # #1773: a vote reading within its sigma of a RING wire -- lone or consensus -- publishes
  # flagged with the ring across the wire; the three live darts of 2026-10-10 rebuilt. Pure,
  # over PointScore in the header; the mutation proof is i1773_check.sh's.
  1773) SRC=i1773_ringwire_check.cpp;    EXTRA= ;;
  # #1456: which look a refused camera seals -- best R of the budget, ties to the earliest.
  1456) SRC=i1456_look_choice_check.cpp;  EXTRA= ;;
  # #1787: the detection body's account fields (path, cameras, sigma, margin, the vote's
  # story) beside #1366's unchanged bytes, with the mutation inside the check. Links
  # turnaus_client.cpp for detectionBody, as i1366_position_check.py compiles it; the
  # upload-ledger and frames-buffer checks are i1787_check.sh's, which also runs this.
  1787) SRC=i1787_body_check.cpp;        EXTRA="src/communication/turnaus_client.cpp -lpthread" ;;
  # #1797: the 06:00 scheduled stop's clock rule and its one guard, driven with a fake
  # clock, and the launcher's side of it (decideMoment's fourth door, endingOf's word).
  # Pure, over scheduled_stop.hpp and the launcher headers; the mutation proofs and the
  # re-carry against a real stub are i1797_check.sh's, which also runs this.
  1797) SRC=i1797_schedule_check.cpp;    EXTRA= ;;
  # #1796: the manifest path a build asks for -- the bare one unchanged, a platform's with
  # its suffix -- pure over update_check.hpp, compiled here without OD_UPDATE_PLATFORM;
  # i1796_check.sh compiles it both ways and drives the real launcher.
  1796) SRC=i1796_path_check.cpp;        EXTRA= ;;
  # Not an iNNNN: the connected-bull regression shipped with the maintainer's fix of
  # 2026-09-22 (1e39e79), which carried no issue number and no row; registered by #1534.
  # The whole calibration directory, because half of what it asserts is
  # calibrateSingleCamera on the optional image arguments and that pulls every sibling in
  # at link time -- the same closure its hand-run driver links as build/'s objects. The
  # glob expands in the container, at /app.
  bull-colour) SRC=bull_colour_regression.cpp; EXTRA='src/detector/geometry/calibration/*.cpp' ;;
  *)
    echo "unit_check: '$NAME' is not a pure check here; they are 1336 1346 1347 1349 1350 1351 1363 1451 1456 1477 1510 1510p2 1511 1512 1517 1678 1681 1766 1773 1553 1554 1560 1586 1649 1652 bull-colour 1787 1797 1796" >&2
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
