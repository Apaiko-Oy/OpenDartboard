#!/bin/bash
# #1560: the half of the lens census that cannot live in a header -- the instrument's
# own two solvers, each with a control and a mutation, predictions printed first.
#
#   1. the kappa control    the full Gauss-Newton camera fit AND the assumption-light
#                           ensemble estimator, both given data distorted at a known
#                           k1(430) = -0.20, must recover it within +/- 0.05
#   2. the kappa mutation   the same two given UNDISTORTED data must read
#                           |k1(430)| <= 0.05
#   3. the f control        rings drawn at a known f must come back within 5%, at
#                           f = 430 and again at f = 725 -- two focal lengths because
#                           one is a coincidence
#   4. the f mutation       the same rings with the PERSPECTIVE TERM REMOVED (an
#                           orthographic board) must come back NOT RESOLVED, because a
#                           scale is not a focal length and a fit that still reported
#                           one would be reading its own prior
#
# i1560_lens_check.cpp (row 1560-lenscheck, through unit_check.sh) holds the ENSEMBLE
# half against the math inlined in lens_census.hpp, plus the trap as an executable
# statement and the recorded verdict's own arithmetic. This row holds the two solvers
# that are not in that header and cannot be: the whole-model fit and the rings-only
# focal-length fit both need a Gauss-Newton and a chi-squared profile.
#
# IT RUNS IN THE CONTAINER, and that is the whole point of this file (#1560, second
# pass). The row used to be `python3 testers/i1560_k1_census.py --synthetic` straight
# from run_all.sh, which is a host command -- and the maintainer's box has no host
# python3, so the Windows Store stub answered "Python ei loytynyt" and the row was red
# by construction on the only environment that runs the suite. Every other python in
# this directory already runs inside $OD_IMAGE, which carries 3.11. Nothing else about
# the measurement changed: the same file, the same argument, stdlib only, no footage,
# no network, no build -- this row is the one thing in the suite that needs neither
# build/ nor mocks/, so it is also the first thing that still works when those are
# broken.
#
# The script ends on `exit`, never on an `echo`: #1463, #1479.
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"

# Run from Git Bash on Windows -- which is how the suite is run on the box that gates
# it -- MSYS rewrites every argument that looks like a unix path, and both halves of
# this call are exposed: `-w /app` reaches Docker as `C:/Program Files/Git/app`
# (rc=125, the container never starts), and the mount point in `-v <tree>:/app` is
# rewritten too, so even moving the container path inside a single-quoted `bash -c`
# only gets as far as `cd: /app: No such file or directory`. Both measured here rather
# than reasoned about. So this file does what i1552_check.sh and i1555_check.sh do and
# leaves the answer where it belongs, around the whole suite:
#
#   MSYS_NO_PATHCONV=1 MSYS2_ARG_CONV_EXCL='*' bash testers/run_all.sh
#
# There is no per-row escape from it worth having -- `//app` would dodge the rewrite
# and buy an ambiguous POSIX path and one harness unlike its neighbours.
od_run "1560-check" --network none \
  -v "$OD_TREE_ROOT":/app -w /app "$OD_IMAGE" \
  python3 testers/i1560_k1_census.py --synthetic < /dev/null
RC=$?
echo "RUN=1560-lensmodel rc=$RC"
exit $RC
