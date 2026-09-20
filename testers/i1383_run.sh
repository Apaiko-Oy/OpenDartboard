#!/bin/bash
# #1383's harness: a blind board never exits, so OD_MAX_CYCLES does not end it and the
# unit's crash-loop limit never sees it.
#
#   testers/i1383_run.sh
#
# It runs testers/i1383_inside.sh in the container, which measures both halves of the
# maintainer's decision on the real binary -- the cycle bound, and what a supervisor is
# told -- each against the same binary with #1383 switched off. What only a live systemd
# can answer is testers/i1383_units.sh, which is outside run_all.sh and says why.
#
# The blind fixture is #892's: three copies of one still JPEG where three cameras should
# be. od_still makes it once per box from the shipped mocks and caches it beside the runs,
# so this harness needs no fixture of its own and no network.
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"

RUN="$OD_RUNS_BASE/1383"
rm -rf "$RUN" 2>/dev/null
mkdir -p "$RUN/cfg"

if ! od_still "$RUN/still.jpg"; then
  echo "FAIL i1383: could not make the still frame the blind fixture is three copies of" >&2
  exit 2
fi

# --network none: nothing here pairs, nothing here pushes, and a board that cannot see
# never opens a socket to announce. The notify socket is a file in the run directory.
od_run i1383 --cpus=2 --network none -e HOME=/root \
  -v "$OD_TREE_ROOT":/app -v "$RUN":/run1383 -v "$RUN/cfg":/root/.config \
  -w /run1383 "$OD_IMAGE" bash /app/testers/i1383_inside.sh
RC=$?
echo "INSIDE_RC=$RC dir=$RUN"
exit $RC
