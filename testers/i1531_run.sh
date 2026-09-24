#!/bin/bash
# #1531: the hand-written arm, asked about the whole of testers/corpus1531 -- and five
# plants that say which of its refusals the corpus can actually see.
#
# #1337's decision, taken 2026-09-23: keep the hand-written P-256 verifier and hold BOTH
# arms of src/update/manifest.hpp to ONE corpus, with no new dependency. This is the Linux
# half. The other half is a step in .github/workflows/release.yml which compiles
# testers/i1531_corpus_check.cpp with cl.exe and runs it against the SAME committed
# corpus -- on pull requests as well as tags, because the corpus carries its own throwaway
# anchor and needs no secret of any kind.
#
#   testers/i1531_run.sh [worktree]
#   testers/i1531_run.sh [worktree] --plants-only
#
# It needs no detector binary and starts nothing on the network; what it needs is
# nlohmann/json, which manifest.hpp includes and which this repository gets through CMake's
# FetchContent. So run a Linux configure or build of this worktree first -- run_all.sh
# builds before it runs anything, so inside the gate this is already true.
#
# ---- THE PLANTS, AND THE ONE THE ISSUE ASKED FOR ----------------------------------------
#
# #1531's own acceptance criterion is a mutation proof: skip the hand-written arm's range
# check on s, and watch the corpus go red naming s-zero and s-order. IT DOES NOT, and that
# is a measurement rather than a miss. Removing the range check on s leaves the arithmetic
# behind it, and the arithmetic refuses both cases anyway: s = 0 and s = n both invert to
# zero mod n, so u1 and u2 are both zero, so the point sum is the point at infinity, which
# verify() refuses two lines further down. The same is true of the on-curve check on the
# anchor -- an off-curve point does not verify a real signature, it just fails.
#
# The general fact is worth carrying past this file: A CORPUS OF MANIFESTS THAT MUST BE
# REFUSED CANNOT PROVE A DEFENCE-IN-DEPTH CHECK LOAD-BEARING, because the check underneath
# it already refuses. It can only prove the OUTERMOST refusal on each path. So each plant
# below declares the exact set of cases it turns red, measured, including the two that
# turn none -- and the tester asserts the set EQUALS the declaration, so a change that
# makes one of those checks load-bearing at last is a red build here rather than a silence.
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"
TREE="${1:-$OD_TREE_ROOT}"
MODE="${2:-}"

JSON="$TREE/build/_deps/nlohmann_json-src/include"
if [ ! -d "$JSON" ]; then
  echo "i1531: $JSON is not there, and manifest.hpp includes nlohmann/json.hpp." >&2
  echo "       Configure or build this worktree for Linux first; run_all.sh does." >&2
  exit 2
fi

od_run "i1531-corpus" --network none -v "$TREE":/app -w /app -e MODE="$MODE" "$OD_IMAGE" bash /app/testers/i1531_inside.sh
RC=$?
echo "RUN_RC=$RC"
# The harness must exit on what it measured: run_all.sh reads the exit code and nothing
# else, and an echo returns 0 whatever it printed (#1335).
exit $RC
