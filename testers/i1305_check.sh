#!/bin/bash
# #1305: mint the manifest fixtures with OpenSSL and run the update check's logic check in
# the Linux container. Pure logic: no camera, no console, NO NETWORK -- the container is
# started with --network none, so the only Turnaus in this check is the one the test hands
# in. nlohmann/json comes from the worktree's own CMake build (build/_deps), so run a Linux
# build of this worktree first.
#
#   testers/i1305_check.sh [worktree] [--mutate]
#
# --mutate is #1305's criterion run the other way: it deletes the signature verification
# from a COPY of src/update/manifest.hpp, runs the same check against it, and expects it to
# go RED. A check that survives that deletion is not measuring the refusal it claims to.
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"
TREE="${1:-$OD_TREE_ROOT}"
MODE="${2:-}"
# The two modes are two phases and they may not share a container name (#1341): a --mutate
# run left over from a kill would otherwise block the ordinary one, and the conflict would
# arrive as Docker's rc=125 rather than as anything about this check.
PHASE="check"
[ "$MODE" = "--mutate" ] && PHASE="check-mutate"
od_run "i1305-$PHASE" --network none -v "$TREE":/app -w /app -e MODE="$MODE" "$OD_IMAGE" bash -c '
  python3 testers/i1305_fixtures.py || { echo FIXTURES_FAILED; exit 2; }
  SRC=/app/src
  if [ "$MODE" = "--mutate" ]; then
    # The deletion, on a copy. Nothing in the worktree is touched.
    rm -rf /tmp/mutated && cp -r /app/src /tmp/mutated
    python3 - <<PY
path = "/tmp/mutated/update/manifest.hpp"
text = open(path).read()
gone = """        if (!verified)
        {
            return refuse(Refusal::NotVerified);
        }

"""
assert gone in text, "the verification is not where the mutation expects it"
open(path, "w").write(text.replace(gone, ""))
print("MUTATED: the signature verification is deleted")
PY
    [ $? -eq 0 ] || { echo MUTATION_FAILED; exit 2; }
    SRC=/tmp/mutated
  fi
  g++ -O2 -std=c++17 -Wall -Wextra -I "$SRC" -I build/_deps/nlohmann_json-src/include \
      -I build/_deps/httplib-src \
      testers/i1305_update_check.cpp -o /tmp/i1305_check || { echo COMPILE_FAILED; exit 2; }
  /tmp/i1305_check testers/fixtures1305 /tmp/i1305-work < /dev/null
'
RC=$?
echo "CHECK_RC=$RC"
if [ "$MODE" = "--mutate" ]; then
  if [ "$RC" -eq 0 ]; then
    echo "MUTATION NOT CAUGHT: the check passed with the verification deleted."
    exit 1
  fi
  echo "MUTATION CAUGHT: the check goes red when the verification is deleted."
  exit 0
fi
exit $RC
