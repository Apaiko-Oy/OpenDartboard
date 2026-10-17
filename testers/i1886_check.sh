#!/bin/bash
# turnaus#1886: a Windows board asks the system not to sleep while the detector runs.
#
# One container, no network, no detector binary, no build/_deps:
#
#   1. testers/i1886_check.cpp built as Linux builds it: the decision and the pin, pure,
#      and a Request that does nothing and says nothing.
#   2. the same file built with -D_WIN32 against testers/fixtures1886/windows.h, which
#      records each SetThreadExecutionState call: the flags asked for, the release, the
#      pin, a refused request, and the line said once.
#   3. main.cpp, read: the Request is declared once, after the one-board claim and before
#      the Scorer -- so it is held through calibration, where device/2 slept, and let go
#      after ~Scorer.
#   4. the mutation proofs, on a COPY of src/ (never the worktree), each of which must turn
#      the Windows build red:
#        flags   ES_CONTINUOUS | ES_SYSTEM_REQUIRED -> ES_CONTINUOUS | ES_DISPLAY_REQUIRED
#        pin     the pin compared with "of" instead of "off", so OD_KEEP_AWAKE=off asks
#      A harness that survives either is not measuring this slice.
#
# The script ends on `exit`, never on an `echo` (#1463, #1479).
#
#   testers/i1886_check.sh
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"

T0=$(date +%s)
od_run "1886-check" --network none -v "$OD_TREE_ROOT":/app:ro -w /app "$OD_IMAGE" bash -c '
  set -u
  WORK=/tmp/i1886
  rm -rf "$WORK" && mkdir -p "$WORK"
  RC=0

  build() { # <src dir> <out> [-D_WIN32]
    local src="$1" out="$2"; shift 2
    local extra=""
    [ "${1:-}" = "-D_WIN32" ] && extra="-D_WIN32 -I /app/testers/fixtures1886"
    g++ -O1 -std=c++17 -Wall -Wextra $extra -I "$src" -I "$src/utils" \
        /app/testers/i1886_check.cpp -o "$out"
  }

  echo "==== 1. the POSIX build =========================================================="
  build /app/src "$WORK/posix" || { echo COMPILE_FAILED; exit 2; }
  "$WORK/posix" < /dev/null; rc=$?; echo "posix rc=$rc"; [ $rc -eq 0 ] || RC=1

  echo "==== 2. the Windows branch, with a recording SetThreadExecutionState ============"
  build /app/src "$WORK/windows" -D_WIN32 || { echo COMPILE_FAILED; exit 2; }
  "$WORK/windows" < /dev/null; rc=$?; echo "windows rc=$rc"; [ $rc -eq 0 ] || RC=1

  echo "==== 3. main.cpp holds it through calibration ===================================="
  M=/app/src/main.cpp
  decl=$(grep -n "keep_awake::Request " "$M" | cut -d: -f1)
  claim=$(grep -n "one_board::Claim one_board_claim;" "$M" | cut -d: -f1)
  scorer=$(grep -n "^  Scorer scorer(" "$M" | cut -d: -f1)
  n=$(printf "%s\n" "$decl" | awk "NF" | wc -l)
  if [ "$n" -eq 1 ] && [ -n "$claim" ] && [ -n "$scorer" ] && [ "$decl" -gt "$claim" ] && [ "$decl" -lt "$scorer" ]; then
    echo "OK   main.cpp declares one keep_awake::Request at line $decl, after the claim ($claim) and before the Scorer ($scorer)"
  else
    echo "FAIL main.cpp: keep_awake::Request at [$decl] (count $n), claim at [$claim], Scorer at [$scorer]"
    RC=1
  fi

  echo "==== 4. mutation proofs =========================================================="
  mutate() { # <name> <sed expression> <grep that must match once before>
    rm -rf /tmp/mutated && cp -r /app/src /tmp/mutated
    F=/tmp/mutated/utils/keep_awake.hpp
    [ "$(grep -c -F "$3" "$F")" = 1 ] || { echo "MUTATION_FAILED ($1): the line is not where the mutation expects it"; RC=1; return; }
    sed -i "$2" "$F"
    grep -q -F "$3" "$F" && { echo "MUTATION_FAILED ($1): the line is still there"; RC=1; return; }
    if ! build /tmp/mutated "$WORK/mutant" -D_WIN32; then echo "MUTATION_FAILED ($1): did not compile"; RC=1; return; fi
    "$WORK/mutant" < /dev/null > "$WORK/mutant.log"; mrc=$?
    if [ $mrc -eq 0 ]; then
      echo "MUTATION NOT CAUGHT ($1): the check passed"; RC=1
    else
      echo "MUTATION CAUGHT ($1): $mrc check(s) red, the first:"
      grep -m1 "^FAIL " "$WORK/mutant.log" | sed "s/^/    /"
    fi
  }
  mutate flags "s/::SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED)/::SetThreadExecutionState(ES_CONTINUOUS | ES_DISPLAY_REQUIRED)/" "::SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED)"
  mutate pin "s/std::string(pin) == \"off\"/std::string(pin) == \"of\"/" "std::string(pin) == \"off\""
  exit $RC
'
RC=$?
T1=$(date +%s)
echo "WALL $((T1 - T0)) s"
echo "CHECK_RC=$RC"
# The harness must exit on what it measured: run_all.sh reads the exit code and
# nothing else, and an echo returns 0 whatever it printed (#1335).
exit $RC
