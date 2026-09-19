#!/bin/bash
# #1258: compile and run the camera question's logic check in the Linux container.
# Pure logic: no camera, no console, no network. nlohmann/json comes from the worktree's
# own CMake build (build/_deps), so run a Linux build of this worktree first.
#
#   testers/i1258_check.sh [worktree]
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"
TREE="${1:-$OD_TREE_ROOT}"
od_run "i1258-check" --network none -v "$TREE":/app -w /app "$OD_IMAGE" bash -c '
  g++ -std=c++17 -Wall -Wextra -I src/utils -I build/_deps/nlohmann_json-src/include \
      testers/i1258_choice_check.cpp -o /tmp/i1258_check || { echo COMPILE_FAILED; exit 2; }
  /tmp/i1258_check < /dev/null
'
RC=$?
echo "CHECK_RC=$RC"
# The harness must exit on what it measured: run_all.sh reads the exit code and
# nothing else, and an echo returns 0 whatever it printed (#1335).
exit $RC
