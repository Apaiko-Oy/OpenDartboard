#!/bin/bash
# #1588: run_all.sh's 1532-guard row, with its interpreter inside the image.
#
# i1532_guard.py is #1532's check and this file does not change what it asks: the update
# journey's workflow publishes nothing and names no secret, release.yml does not run the
# journey, and the pull_request `paths:` filter covers every file src/launcher/main.cpp
# reaches through a quoted #include. The same file, the same argument (the tree), stdlib
# only, no footage, no network and no build.
#
# IT RUNS IN THE CONTAINER, and that is the whole point of this file. The row used to be
# `python3 testers/i1532_guard.py <tree>` straight from run_all.sh, which is a host
# command -- and the maintainer's box has no host python3, so the Windows Store stub
# answered "Python ei loytynyt", rc=49, and the row was red by construction on the only
# environment that runs the suite. #1560 made the identical repair for 1560-lensmodel
# (i1560_check.sh) and wrote the rule down beside that row: a tester may assume the IMAGE
# has an interpreter and may never assume the HOST has one. $OD_IMAGE carries 3.11.
#
# The GitHub runner is not affected: update-journey.yml calls i1532_guard.py itself, on
# the runner's own python, and that is where section 2 of the guard (GITHUB_WORKFLOW_REF,
# GITHUB_REF_TYPE) is asked. No GITHUB_* variable is passed into this container, so here
# -- as on any host off a runner -- only sections 1 and 3 are asked, which is what the row
# asked before.
#
# Run from Git Bash on Windows, MSYS rewrites both halves of this docker call (`-w /app`
# and the mount point in `-v <tree>:/app`); the answer is the env guards around the whole
# suite, as for i1552_check.sh, i1555_check.sh and i1560_check.sh, not a cleverer quoting:
#
#   MSYS_NO_PATHCONV=1 MSYS2_ARG_CONV_EXCL='*' bash testers/run_all.sh 1532-guard
#
# The script ends on `exit`, never on an `echo`: #1463, #1479.
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"

od_run "1532-guard" --network none \
  -v "$OD_TREE_ROOT":/app -w /app "$OD_IMAGE" \
  python3 testers/i1532_guard.py /app < /dev/null
RC=$?
echo "RUN=1532-guard rc=$RC"
exit $RC
