#!/bin/bash
# #1849: the Pi setup machine and the re-join pair path, in the Linux container, no network.
#
#   machine   i1849_machine_check.cpp: src/setup/setup_machine.hpp driven through every path
#             in the issue's checklist with NetworkManager, the radio and the pair call faked
#             -- the counter file and N = 5, the look that counts to N raising Turnaus-XXXX,
#             the pre-access-point scan, GET and POST /setup, the success path and the four
#             failure paths (wrong Wi-Fi password, refused code, expired code, another club),
#             and the three STATUS= lines.
#   pair      i1849_pair_check.cpp: the real TurnausClient against an in-process stand-in
#             for POST /api/v1/autoscorer/devices -- `--keep-device` presents the current
#             token, keeps a rotated one, and leaves the file byte-for-byte on a 422 or on an
#             answer for another club; without the flag --pair is what it was.
#
#   testers/i1849_check.sh [machine] [pair]      (both when none is named)
#
# Needs build/_deps (nlohmann/json, cpp-httplib), as i1841_check.sh does; run_all.sh builds
# before it runs anything. Ends on an exit status, not an echo (#1335).
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"
TREE="$OD_TREE_ROOT"

if [ ! -d "$TREE/build/_deps/nlohmann_json-src" ]; then
  echo "i1849_check: no $TREE/build/_deps -- configure a Linux build of this worktree first" >&2
  exit 2
fi

CASES="${*:-machine pair}"
T0=$(date +%s)
od_run "i1849-check" --cpus=2 --network none -v "$TREE":/app -w /app "$OD_IMAGE" bash -c '
  INC="-I /app/src -I /app/src/utils -I /app/build/_deps/nlohmann_json-src/include -I /app/build/_deps/httplib-src"
  rc=0
  for c in '"$CASES"'; do
    case "$c" in
      machine)
        g++ -std=c++17 -Wall $INC /app/testers/i1849_machine_check.cpp -o /tmp/i1849_machine || { echo "FAIL machine: does not compile"; rc=1; continue; }
        /tmp/i1849_machine || rc=1 ;;
      pair)
        g++ -std=c++17 $INC $(pkg-config --cflags opencv4 2>/dev/null) \
          /app/testers/i1849_pair_check.cpp /app/src/communication/turnaus_client.cpp -o /tmp/i1849_pair \
          $(pkg-config --libs opencv4 2>/dev/null) -lpthread || { echo "FAIL pair: does not compile"; rc=1; continue; }
        /tmp/i1849_pair || rc=1 ;;
      *) echo "FAIL unknown case $c"; rc=1 ;;
    esac
  done
  exit $rc'
RC=$?
echo "WALL $(( $(date +%s) - T0 )) s"
echo "CHECK_RC=$RC"
exit $RC
