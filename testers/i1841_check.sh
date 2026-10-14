#!/bin/bash
# #1841: the Contest door's 409, end to end (i1841_contest_check.py) in the Linux container
# against the worktree's own Linux build and #822's stub on the container's loopback.
# #1276's wrapper with its own paths. No network: the stub is the only Turnaus there is.
# The container is named for the run and reaped by that name alone.
#
# PREDICTION, STATED FIRST, because none of this was run where it was written (2026-10-13:
# the authoring box has no Docker; the maintainer's gate runs it). On a client that reads
# the Contest door's 409 by its body's shape (TurnausClient::contestOverBody):
#
#   over       the detector, paired to the club and then to a Contest, meets the stub's
#              409 + errors.board on the third beat (STUB_GIVE_UP_STATUS=409,
#              STUB_GIVE_UP_AFTER_BEATS=2). Its log says, in this order: `the server says
#              Casual Contest 12 is over (409 with errors.board at /api/v1/casual/...)`,
#              `the binding to Casual Contest 12 has ended (HTTP 409: the Contest was given
#              up)` exactly once, `still paired to its Organisation`; neither door's
#              "stands at no Station" sentence appears; the stub's transcript shows the
#              heartbeat after the refused one landing at /api/v1/autoscorer/heartbeats
#              under the club token and nothing at a Casual door afterwards; the run ends
#              on binding=organisation with no `contest` object in credentials.json.
#   control    the same with STUB_GIVE_UP_STATUS=401 -- #891's model, the stub's default
#              spelled out -- ends the same way with the 401 reason, and the stub's
#              casual_refused events carry no `status` key: today's path, byte for byte.
#   nostation  STUB_GIVE_UP_STATUS=409-nostation (409 WITHOUT errors.board at the Contest
#              door) keeps the binding: `this Contest board stands at no Station (409); the
#              beat has nowhere to land` at least twice, the un-named autoscorer sentence
#              never, no release, binding=contest:12, the `contest` object still in the
#              file, and the club door beaten not once.
#   harness    i1841_beat_check.cpp compiled against src/communication/turnaus_client.cpp
#              and driven at the stub: `over` releases within thirty seconds and the next
#              beat lands at the club door; `kept` holds the binding through five 409s.
#   pure       i1841_body_check.cpp (unit_check.sh 1841's source) passes; and the
#              autoscorer door's two 409 sentences are in turnaus_client.cpp verbatim, once.
#
#   testers/i1841_check.sh [--mutate] [case ...]
#
# --mutate is the criterion run the other way: on a COPY of src/ every `contestOverBody(
# res.body)` at the two call sites becomes `false` -- every 409 read as "no Station", which
# is the client of 2026-10-13 -- and `harness` is run against that copy. The `over` line
# goes red naming the door (`the Contest door's 409 with errors.board ended the binding`),
# the mutant's own log says `this Contest board stands at no Station` instead, and `kept`
# stays green, so the proof is that one line and not a client that releases on anything.
# The Python judges that pattern itself and exits 0 when the mutation was caught.
#
# Needs build/_deps (the headers CMake fetches) for the two compiles; run_all.sh builds
# before it runs anything. Ends on an exit status, not an echo (#1335).
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"
TREE="$OD_TREE_ROOT"
OUT="$OD_RUNS_BASE/1841"
mkdir -p "$OUT"

if [ ! -d "$TREE/build/_deps/nlohmann_json-src" ]; then
  echo "i1841_check: no $TREE/build/_deps -- configure a Linux build of this worktree first" >&2
  exit 2
fi

T0=$(date +%s)
od_run "i1841-check" --cpus=2 --network none -v "$TREE":/app -w /app \
  -e CHECK_WORK=/tmp/i1841-check -e OD_BIN="${OD_BIN:-/app/build/opendartboard}" "$OD_IMAGE" \
  bash -c 'python3 /app/testers/i1841_contest_check.py "$@"; rc=$?; mkdir -p /app/build/i1841-check; cp -r /tmp/i1841-check/. /app/build/i1841-check/ 2>/dev/null; exit $rc' _ "$@"
RC=$?
echo "WALL $(( $(date +%s) - T0 )) s"
echo "CHECK_RC=$RC"
# The harness must exit on what it measured: run_all.sh reads the exit code and
# nothing else, and an echo returns 0 whatever it printed (#1335).
exit $RC
