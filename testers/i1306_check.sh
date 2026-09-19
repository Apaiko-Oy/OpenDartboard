#!/bin/bash
# #1306: the launcher applies an update it verified, and goes back to the one that worked.
# Measured in the Linux container, end to end, against a real deployment.
#
# WHAT IS REAL IN HERE. A python http server serving a signed manifest and a release zip;
# manifests signed by OpenSSL with a key minted on this run; zips written by zipfile with
# deflated entries, the shape release.yml's Compress-Archive produces; digests that are
# the SHA-256 of the file on disk; the launcher's own socket code fetching both; the
# launcher's own rename doing the swap; and real child processes started, waited for and
# classified. The one substitution is the artefact url's scheme and host, and
# testers/i1306_update_check.cpp says at the top why there is no way around it here.
#
# The container is NOT started with --network none, unlike #1303's and #1305's: this
# slice's subject is a download, so it needs a loopback interface. Nothing leaves the
# container -- the only address anything asks is 127.0.0.1:8306, and the fixture's own
# url names releases.example.invalid, which can never resolve anywhere.
#
#   testers/i1306_check.sh [worktree] [--mutate]
#
# --mutate is the criterion run the other way. On a COPY of src/launcher/apply_update.hpp
# it deletes the comparison of the downloaded artefact's SHA-256 against the signed
# manifest -- and nothing else, so a launcher that still verifies the manifest, still
# checks the length, still unpacks and still swaps. Measured, the harness goes red: the
# bad-digest case installs a release the signature never covered. A harness that survives
# that deletion is not measuring the first criterion at all.
#
# It needs build/_deps, which a Linux build of this worktree leaves behind (nlohmann and
# httplib are the detector's own dependencies and the launcher shares them since #1306).
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"
TREE="${1:-$OD_TREE_ROOT}"
MODE="${2:-}"

if [ ! -d "$TREE/build/_deps/nlohmann_json-src" ]; then
  echo "i1306_check: no $TREE/build/_deps -- configure a Linux build of this worktree first" >&2
  exit 2
fi

docker run --rm --name "$(od_name "i1306-check")" -v "$TREE":/app -w /app -e MODE="$MODE" \
  "$OD_IMAGE" bash /app/testers/i1306_inside.sh
RC=$?
echo "CHECK_RC=$RC"
if [ "$MODE" = "--mutate" ]; then
  if [ "$RC" -eq 0 ]; then
    echo "MUTATION NOT CAUGHT: the harness passed with the artefact's digest check deleted."
    exit 1
  fi
  echo "MUTATION CAUGHT: the harness goes red when the artefact's digest check is deleted."
  exit 0
fi
# The harness must exit on what it measured: run_all.sh reads the exit code and nothing
# else, and an echo returns 0 whatever it printed (#1335).
exit $RC
