#!/bin/bash
# #1305: --check-update end to end, run by the real program against a stub on the
# container's own loopback. #1276's wrapper shape with its own paths: one container, named
# for the run and reaped by that name alone, --network none, so the only Turnaus in this
# run is the python server this script starts.
#
# It builds a SECOND binary with the fixture's public key compiled in
# (-DOD_UPDATE_ANCHOR_CURRENT), because the shipped build has no anchor yet -- #1299 mints
# the first signing key -- and a check whose every answer is "this build has no update key"
# would show nothing about the path underneath it.
#
# The four things it shows, in order:
#   1. nothing listening      -> the board keeps running and says why
#   2. a deployment with no manifest (404) -> the board keeps running and says so
#   3. a manifest naming this build's own version -> there is no update
#   4. a manifest naming another version         -> an update is available
#   5. one byte of that manifest changed         -> SIGNATURE VERIFICATION FAILED
# and around every one of them, that the config directory is byte-for-byte unchanged.
#
#   testers/i1305_run.sh [worktree]
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"
TREE="${1:-$OD_TREE_ROOT}"
OUT="$OD_RUNS_BASE/1305"
mkdir -p "$OUT"
od_run "i1305-run" --cpus=2 --network none -e HOME=/root \
  -v "$TREE":/app -v "$OUT":/out -w /app "$OD_IMAGE" bash /app/testers/i1305_inside.sh
RC=$?
echo "RUN_RC=$RC"
# The harness must exit on what it measured: run_all.sh reads the exit code and
# nothing else, and an echo returns 0 whatever it printed (#1335).
exit $RC
