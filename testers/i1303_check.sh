#!/bin/bash
# #1303: the launcher's decisions and its behaviour, in the Linux container.
#
# Two halves, and the second is the one that could not be bought any other way.
#
# THE DECISIONS are testers/i1303_launcher_check.cpp: the three endings measured against
# real child processes, the arguments arriving byte for byte, the exit code, the words in
# both languages, and the Windows command line round-tripped through the rules
# CommandLineToArgvW documents. Pure: no camera, no console, NO NETWORK -- the container
# is started with --network none. It needs no CMake build of the worktree, because the
# launcher links nothing: no OpenCV, no httplib, no json.
#
# THE BEHAVIOUR is the REAL launcher binary, started four ways, because the third
# criterion is about a condition a fake console cannot be in. A launcher that asks a
# question of a board started as a service waits for ever, and the only way to know it
# does not is to give it an input that never answers and watch it finish anyway.
#
#   redirected   < /dev/null            what a scheduled task and a `< NUL` start have
#   open pipe    < a fifo nobody writes what a service has: no EOF is ever coming
#   no stdin     0<&-                   no input handle at all
#   a terminal   under script(1)        THE CONTROL: here it must really wait
#
# The control is not decoration. Without it "it did not pause" is satisfied by a launcher
# that can never pause, and the first three runs would prove nothing at all. #708's rule,
# in the shape this slice needs it: prove the needle is there before its absence means
# anything.
#
# WINDOWS IS NOT MEASURED HERE AND THIS FILE DOES NOT PRETEND TO. It measures what a
# launcher decides and what it does with an input nobody is at, on the platform this
# repository's harnesses run on. testers/i1303_windows.sh is the same questions asked of a
# real .exe on a real Windows console.
#
#   testers/i1303_check.sh [worktree] [--mutate]
#
# --mutate is the criterion run the other way: on a COPY of src/launcher/launcher.hpp it
# deletes the gate on console_prompt::isInteractiveConsole(), so the launcher holds the
# window open for everybody. Measured, it goes red in two places: the in-process check
# says the rule was broken, and the open-pipe run takes the whole timeout, which is what
# breaking it COSTS -- a board started as a service waiting for ever on a question nobody
# will answer. A harness that survives that deletion is not measuring this at all.
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"
TREE="${1:-$OD_TREE_ROOT}"
MODE="${2:-}"
if [ ! -d "$TREE/build/_deps/nlohmann_json-src" ]; then
  echo "i1303_check: no $TREE/build/_deps -- configure a Linux build of this worktree first" >&2
  exit 2
fi
docker run --rm --name "$(od_name "i1303-check")" --network none -v "$TREE":/app -w /app -e MODE="$MODE" \
  "$OD_IMAGE" bash /app/testers/i1303_inside.sh
RC=$?
echo "CHECK_RC=$RC"
if [ "$MODE" = "--mutate" ]; then
  if [ "$RC" -eq 0 ]; then
    echo "MUTATION NOT CAUGHT: the harness passed with the interactive-console gate deleted."
    exit 1
  fi
  echo "MUTATION CAUGHT: the harness goes red when the interactive-console gate is deleted."
  exit 0
fi
# The harness must exit on what it measured: run_all.sh reads the exit code and
# nothing else, and an echo returns 0 whatever it printed (#1335).
exit $RC
