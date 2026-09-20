#!/bin/bash
# #1450: the sealed geometry fingerprint, measured in both spellings on ONE binary.
#
# `unit_check.sh` is the general harness for a pure check and it compiles and runs one
# ONCE. This issue's measurement needs the same binary run TWICE -- plain, and with
# OD_SEAL=star -- because that is what makes the falsification a falsification rather than
# a second build to be argued about. The falsifier is read through a memoised
# `static const bool`, the way OD_LOOK, OD_WIRE_COUNT and OD_RING are, so one process is
# one spelling and there is nothing to be gained by asking inside the check.
#
# So this is a harness of its own, and it is unit_check.sh's compile with a second run
# after it. Same include paths as CMakeLists.txt gives the real build, same requirement:
# build/_deps must exist, because nlohmann/json and cpp-httplib are fetched by CMake.
# run_all.sh builds before it runs anything.
#
#   testers/i1450_seal_check.sh
#
# WHAT EACH RUN HAS TO SAY, and it is not enough that both exit 0. A check that silently
# skipped its own section would do that. So this harness reads the output of each run for
# the sentence only that spelling can produce, and a missing sentence is a failure with
# the same weight as a FAIL line.
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"

FAILED=0
say() { echo "$1"; [ "${2:-ok}" = ok ] || FAILED=$((FAILED + 1)); }

if [ ! -d "$OD_TREE_ROOT/build/_deps" ]; then
  echo "i1450_seal_check: $OD_TREE_ROOT/build/_deps is not there, so the headers CMake" >&2
  echo "                  fetches (nlohmann/json, cpp-httplib) cannot be included. Build" >&2
  echo "                  this worktree first -- run_all.sh does that before anything." >&2
  exit 2
fi

RUN="$OD_RUNS_BASE/i1450"
mkdir -p "$RUN"
rm -f "$RUN/new.txt" "$RUN/old.txt"

# One container, one compile, two runs. `--network none` because a pure check reaches
# nothing; the compile needs no network either, the deps are already in the tree.
od_run "i1450-seal" --cpus=2 --network none \
  -v "$OD_TREE_ROOT":/app -v "$RUN":/run1450 -w /app "$OD_IMAGE" bash -c '
  set -u
  g++ -std=c++17 -O1 -Wall -Wextra \
      -I src -I src/utils \
      -I build/_deps/nlohmann_json-src/include \
      -I build/_deps/httplib-src \
      -o /tmp/seal_check testers/i1450_seal_check.cpp \
      $(pkg-config --cflags --libs opencv4) || { echo "COMPILE_FAILED"; exit 2; }
  /tmp/seal_check           < /dev/null > /run1450/new.txt 2>&1; echo "NEW_RC=$?" >> /run1450/new.txt
  OD_SEAL=star /tmp/seal_check < /dev/null > /run1450/old.txt 2>&1; echo "OLD_RC=$?" >> /run1450/old.txt
  cat /run1450/new.txt /run1450/old.txt
'
RC=$?
if [ "$RC" -ne 0 ]; then
  say "FAIL the check could not be compiled or run in $OD_IMAGE (rc=$RC)" bad
  echo "load_at_end=$(cut -d' ' -f1 /proc/loadavg)"
  exit $FAILED
fi

echo "---- this tree's seal ----"
cat "$RUN/new.txt"
echo "---- OD_SEAL=star, the pre-#1450 seal ----"
cat "$RUN/old.txt"
echo "--------------------------"

# ---- each run exited on what it measured ------------------------------------------------
grep -q '^NEW_RC=0$' "$RUN/new.txt" \
  && say "OK   the check passes on this tree's seal" \
  || say "FAIL the check does not pass on this tree's seal" bad
grep -q '^OLD_RC=0$' "$RUN/old.txt" \
  && say "OK   and it passes under OD_SEAL=star, measuring the defect instead" \
  || say "FAIL the check does not pass under OD_SEAL=star" bad

# ---- and each run really ran the section only that spelling can reach --------------------
#
# A check whose sections had both been skipped would exit 0 twice and say nothing. These
# two greps are what make the pair of exit codes mean something.
grep -q '^OK   a camera whose .anchored. moved is caught' "$RUN/new.txt" \
  && say 'OK   this tree'"'"'s run reached the finding: an anchored that moved breaks the seal' \
  || say "FAIL this tree's run never reached the finding" bad
grep -q '^OK   FALSIFIED the pre-#1450 seal cannot tell a readable board' "$RUN/old.txt" \
  && say "OK   and the falsified run reached the defect: the old seal cannot tell them apart" \
  || say "FAIL the falsified run never reached the defect" bad

# ---- the two spellings are not the same line ---------------------------------------------
#
# The positive control for the falsifier itself. If OD_SEAL did nothing, both runs would
# print identical SEAL lines and every assertion above could still pass by accident.
NEWSEAL=$(grep -m1 '^SEAL anchored:' "$RUN/new.txt")
OLDSEAL=$(grep -m1 '^SEAL anchored:' "$RUN/old.txt")
[ -n "$NEWSEAL" ] && [ -n "$OLDSEAL" ] && [ "$NEWSEAL" != "$OLDSEAL" ] \
  && say "OK   OD_SEAL really changes the line: [$OLDSEAL] against [$NEWSEAL]" \
  || say "FAIL OD_SEAL did not change the sealed line, so the falsifier is not falsifying" bad

# ---- nothing writes a fingerprint down ----------------------------------------------------
#
# The whole of this issue's stated cost -- "a board that sealed under the old spelling and
# is asked under the new one sees a breach and stops" -- rests on the seal being
# PERSISTED. It is not. `sealed_geometry` lives in GeometryDetector and nowhere else, and
# the census below is what keeps that true: a later slice that serialises it, puts it in
# the cache or publishes it over the API turns this red and has to say so.
# Prose is filtered out and that is the point of the filter rather than a convenience:
# `geometry_agreement.hpp` holds a docblock that explains the seal at length and must be
# allowed to NAME it. What may not spread is a line of CODE that reaches the seal, so a
# line whose first characters are a comment marker is not one.
code_touching_the_seal() {
  grep -rn 'sealed_geometry' "$OD_TREE_ROOT/src" | grep -Ev ':[[:space:]]*(\*|//|/\*)'
}

HOLDERS=$(code_touching_the_seal | cut -d: -f1 | sed "s|$OD_TREE_ROOT/||" | sort -u | tr '\n' ' ')
[ "$HOLDERS" = "src/detector/geometry/geometry_detector.cpp src/detector/geometry/geometry_detector.hpp " ] \
  && say "OK   the seal is reached from one place only: $HOLDERS" \
  || say "FAIL the seal has escaped GeometryDetector; it is now reached from: $HOLDERS" bad

code_touching_the_seal | grep -Eq 'ofstream|fwrite|write\(|json|cache|save' \
  && say "FAIL something now writes the sealed fingerprint down, so a restart CAN read an old spelling" bad \
  || say "OK   and no path writes it to a file, a cache or a payload, so no restart reads an old one"

# ---- nor does this change move the cache's own refusal -------------------------------------
#
# #1330: the cache discards a record whose size is not sizeof(DartboardCalibration). #1450
# adds no field to that struct -- it reads two #1363 already put there -- so a held
# calibration is still loaded rather than thrown away, and a board restarting across this
# change does not recalibrate either. Measured as a diff rather than asserted: if this
# branch touched any struct the cache writes, this names the file.
STRUCTS=$(cd "$OD_TREE_ROOT" && git diff --name-only "$(git merge-base HEAD origin/main)" HEAD -- \
  src/detector/geometry/calibration/orientation_processing.hpp \
  src/detector/geometry/calibration/wire_processing.hpp \
  src/detector/geometry/calibration/ellipse_processing.hpp \
  src/detector/geometry/calibration/board_look.hpp \
  src/detector/geometry/calibration/geometry_calibration.hpp 2>/dev/null | tr '\n' ' ')
[ -z "$STRUCTS" ] \
  && say "OK   no header the cache's record_bytes is taken from was touched, so a held calibration still loads" \
  || say "FAIL this branch moves sizeof(DartboardCalibration) through: $STRUCTS -- boards will recalibrate" bad

echo "load_at_end=$(cut -d' ' -f1 /proc/loadavg)"
[ "$FAILED" -eq 0 ] && echo "ALL OK" || echo "FAILURES: $FAILED"
# The harness exits on what it measured, never on an echo (#1463).
exit $FAILED
