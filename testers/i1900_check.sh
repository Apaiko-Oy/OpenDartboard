#!/bin/bash
# turnaus#1900: the launcher's artefact fetch is a bounded series (three attempts, 5 s then
# 15 s apart, Retry-After honoured), and a board not yet on the clock waits for it, bounded.
#
# PREDICTION, STATED FIRST. Against testers/i1900_server.py through the launcher's own
# fetchArtefactOverHttp() and OD_UPDATE_ARTEFACT_BASE, counting requests in the server's
# access log:
#
#   403, 200            installs at the second attempt: 2 requests, one wait of 5 s
#   403, 403, 403       starts the old version: 3 requests, waits 5 and 15, and the
#                       next-look sentence (06:00 restart, or --update-now)
#   404                 1 request, no wait: not retried
#   429 Retry-After: 7  waits 7 s, not 5
#   wrong bytes         the digest refuses them, 1 request: not retried
#   503, 500, 200       installs at the third attempt; 408 and a closed port are retried;
#                       a Retry-After of 99 ends the series rather than being shortened
#   403 x 3, real wait  3 requests in about 20 s of wall clock: inside one minute
#   the clock           no timesyncd directory: no wait; the flag set: no wait; set
#                       during the wait: stops then; never set: 30 s and on
#
# THE MUTATION PROOFS, each on a COPY of src/ (never the worktree), each of which must turn
# the check red:
#   intended   fetchWithRetries() announces and waits for every retry but sends only the
#              first request -- the retry that is merely intended, which leaves ONE request
#              in the server's log
#   after      Retry-After is ignored, so 429 waits the default 5 s
#   definite   404 is retried as if it were a 403
#
# One container, --network none (loopback is all it needs), no detector binary. It needs
# build/_deps for nlohmann/json and cpp-httplib, which a Linux build of this worktree leaves
# behind. It ends on `exit`, never on an `echo` (#1463).
#
#   testers/i1900_check.sh
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"

if [ ! -d "$OD_TREE_ROOT/build/_deps/nlohmann_json-src" ]; then
  echo "i1900_check: no $OD_TREE_ROOT/build/_deps -- configure a Linux build of this worktree first" >&2
  exit 2
fi

T0=$(date +%s)
od_run "1900-check" --network none -v "$OD_TREE_ROOT":/app:ro -w /app "$OD_IMAGE" bash -c '
  set -u
  WORK=/tmp/i1900
  PORT=8900
  DEPS="-I /app/build/_deps/nlohmann_json-src/include -I /app/build/_deps/httplib-src"
  rm -rf "$WORK" && mkdir -p "$WORK"
  head -c 300000 /dev/urandom > "$WORK/good.bin"

  python3 /app/testers/i1900_server.py "$PORT" "$WORK/good.bin" "$WORK/access.log" > "$WORK/server.out" 2>&1 &
  SERVER=$!
  trap "kill $SERVER 2> /dev/null" EXIT
  sleep 1

  # <label> <src>: compile the check against a tree and run it with a fresh access log.
  run() {
    g++ -O1 -std=c++17 -Wall -Wextra -Werror -I "$2" $DEPS /app/testers/i1900_retry_check.cpp \
        -o "$WORK/check-$1" -lpthread || { echo "COMPILE_FAILED ($1)"; return 2; }
    : > "$WORK/access.log"
    "$WORK/check-$1" "$PORT" "$WORK" < /dev/null
  }

  echo "==== the tree as it is ===="
  run tree /app/src
  RC=$?
  echo "TREE_RC=$RC"

  # <name> <python replacing one exact text in a copy>
  MUTANTS_CAUGHT=0
  mutate() {
    local name="$1" file="$2" gone="$3" instead="$4"
    rm -rf "$WORK/src-$name" && cp -r /app/src "$WORK/src-$name"
    GONE="$gone" INSTEAD="$instead" FILE="$WORK/src-$name/$file" python3 -c "
import os
p, gone, instead = os.environ[\"FILE\"], os.environ[\"GONE\"], os.environ[\"INSTEAD\"]
t = open(p, encoding=\"utf-8\").read()
assert t.count(gone) == 1, \"the mutation does not find its text exactly once\"
open(p, \"w\", encoding=\"utf-8\").write(t.replace(gone, instead))
" || { echo "MUTATION_FAILED ($name)"; return 2; }
    echo
    echo "==== mutation: $name ===="
    # A mutant skips the 20 s wall-clock scenario: the tree run already measured it.
    OD_I1900_NO_WALL=1 run "$name" "$WORK/src-$name" > "$WORK/mutant-$name.out" 2>&1
    local mrc=$?
    grep "^FAIL" "$WORK/mutant-$name.out" | sed "s/^/     | /"
    if [ "$mrc" -eq 1 ]; then
      echo "MUTATION CAUGHT: $name (rc=$mrc, $(grep -c "^FAIL" "$WORK/mutant-$name.out") lines red)"
      MUTANTS_CAUGHT=$((MUTANTS_CAUGHT + 1))
    else
      echo "MUTATION NOT CAUGHT: $name (rc=$mrc)"
    fi
  }
  mutate intended launcher/apply_update.hpp \
    "            fetched.response = fetch(url);" \
    "            if (attempt == 1) { fetched.response = fetch(url); }"
  mutate after launcher/apply_update.hpp \
    "        if ((failed.status == 429 || failed.status == 503) && failed.retry_after_s > 0)" \
    "        if (false)"
  mutate definite launcher/apply_update.hpp \
    "        return status >= 500 || status == 403 || status == 408 || status == 429;" \
    "        return status >= 500 || status == 403 || status == 404 || status == 408 || status == 429;"

  echo
  echo "MUTANTS_CAUGHT=$MUTANTS_CAUGHT of 3"
  [ "$RC" -eq 0 ] && [ "$MUTANTS_CAUGHT" -eq 3 ]
'
RC=$?
echo "WALL $(( $(date +%s) - T0 )) s"
echo "CHECK_RC=$RC"
exit $RC
