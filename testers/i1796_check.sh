#!/bin/bash
# #1796: a Raspberry Pi board updates through the launcher the way a Windows board does.
# Measured in the Linux container against the launcher THIS TREE'S CMakeLists.txt builds,
# in the layout a Pi has, with a tar.gz artefact and a served manifest.
#
# PREDICTION, STATED FIRST. The CMake-built Linux launcher, in /var/lib/opendartboard's
# shape under STATE_DIRECTORY with the deb's detector beside it:
#
#   0  with no state and a manifest naming the deb's own version, it MAY check (one
#      request, at /updates/opendartboard/stable-linux-arm64.json), fetches nothing and
#      starts the deb's detector from /usr/local/bin;
#   1  with a manifest naming a newer version it fetches the tar.gz, unpacks it with
#      /bin/tar, puts the detector under the state directory and a COPY of the deb's in
#      update/previous, starts the new one, and the state file names both versions -- on
#      an input nobody answers, without pausing;
#   2  a newer version whose detector exits at once is started twice, rolled back, and
#      the old one runs, in one launcher run of a few seconds (one unit start, inside
#      StartLimitBurst), with the launcher's sentence in both languages;
#   3  a `scheduled` exit (60, #1797) is followed by one look and one restart with
#      identical arguments, inside the same launcher run;
#   4  pure: the bare path is unchanged and a build with OD_UPDATE_PLATFORM asks the
#      platform's (testers/i1796_path_check.cpp, compiled both ways);
#   and credentials.json, channel.json and cameras.json beside it all are byte-identical
#   afterwards.
#
# What is real: the launcher binary CMake builds from src/launcher/main.cpp with the
# fixture's anchor compiled in; manifests signed by OpenSSL with a key minted on this run;
# tar.gz archives of the deb job's shape; the launcher's own socket code fetching both over
# loopback (the manifest from the credential's base_url, the artefact from the manifest's
# path on OD_UPDATE_ARTEFACT_BASE, because this container's transport has no TLS --
# apply_update.hpp says why that pin is safe); /bin/tar unpacking; the launcher's own
# renames; real child processes. The stubs are #1306's and #1797's.
#
# The container is NOT --network none: a download needs a loopback interface. Nothing
# leaves it -- the only address anything asks is 127.0.0.1:8796, and the fixture's own url
# names releases.example.invalid, which can never resolve anywhere.
#
#   testers/i1796_check.sh [--mutate]
#
# --mutate is the criterion run the other way: on a COPY of src/ it makes
# update_check::platformSuffix() empty whatever OD_UPDATE_PLATFORM says, so a Linux build
# asks the Windows path. The pure check goes red naming the path, and the install goes red
# with it: the launcher asks stable.json, is told 404, and starts the deb's detector --
# which is exactly the board that would have installed a Windows zip had one been there.
#
# It needs build/_deps, which a Linux build of this worktree leaves behind; OD_SKIP_BUILD
# changes nothing else about it. It ends on an exit status, not an echo (#1463).
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"
MODE="${1:-}"

if [ ! -d "$OD_TREE_ROOT/build/_deps/nlohmann_json-src" ]; then
  echo "i1796_check: no $OD_TREE_ROOT/build/_deps -- configure a Linux build of this worktree first" >&2
  exit 2
fi

T0=$(date +%s)
od_run "1796-check" --cpus=2 -v "$OD_TREE_ROOT":/app -w /app -e MODE="$MODE" "$OD_IMAGE" bash /app/testers/i1796_inside.sh
RC=$?
echo "WALL $(( $(date +%s) - T0 )) s"
echo "CHECK_RC=$RC"
if [ "$MODE" = "--mutate" ]; then
  if [ "$RC" -eq 0 ]; then
    echo "MUTATION NOT CAUGHT: the harness passed with the platform suffix dropped."
    exit 1
  fi
  echo "MUTATION CAUGHT: the harness goes red when the platform suffix is dropped."
  exit 0
fi
# The harness must exit on what it measured: run_all.sh reads the exit code and nothing
# else, and an echo returns 0 whatever it printed (#1335).
exit $RC
