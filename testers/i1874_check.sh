#!/bin/bash
# turnaus#1874: the geometric flag's default threshold is 0.6 of the solve's own across-wire
# sigmas (0.7 from turnaus#1815; OD_FLAG_SIGMAS=<K> still moves it, =off is #1556's 1.0). A
# truth-line replay of device/2's 2026-10-13, -15 and -16 corrected spans (409 flagged
# geometric darts, rows in the tester; the kept frames are stills, not footage). One compile
# of the tree plus two of a MUTATED SCRATCH COPY of the header, in i1875_check.sh's shape:
# the tree is mounted read-only and never written.
#
#   1. the tree                 i1874_flag_width_check -- must pass whole, and prints the
#                                per-day flags at 1.0, 0.7 and the default, and the
#                                corrected darts that lose their flag
#   2. mutation A: 0.7 again    `kDefaultFlagSigmas = 0.6;` reads `= 0.7;` (v0.1.16).
#                                PREDICTION: exactly 44 FAIL lines: 37 `cleared:` (the darts
#                                that stood between 0.6 and 0.7), 5 `count:`, 1 `lost:`
#                                (2026-10-13 dart 43 flagged again), 1 `account:`; no `kept:`,
#                                no `edge:`, no `fixed:`.
#   3. mutation B: the issue's  `= 0.5;`. PREDICTION: exactly 7 FAIL lines: 2 `edge:` (#1815's
#      0.5                      board 20 at 0.524 and rig-20260929 dev v9.3 at 0.569), 4
#                                `count:` (the three days and the total; the lost count is 1
#                                either way), 1 `account:` (it names 0.50); no `kept:`, no
#                                `lost:`, no `cleared:`, no `fixed:`.
#
# The script ends on `exit`, never on an `echo`: #1463, #1479.
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"

if [ ! -d "$OD_TREE_ROOT/build/_deps" ]; then
  echo "i1874_check: $OD_TREE_ROOT/build/_deps is not there; build this worktree first" >&2
  exit 2
fi

od_run "1874-check" --network none \
  -v "$OD_TREE_ROOT":/app:ro -w /app "$OD_IMAGE" bash -c '
  set -u
  HDR=src/detector/geometry/detection/score_processing.hpp
  NEEDLE="kDefaultFlagSigmas = 0.6;"
  compile() { # $1 include root for src, $2 output
    g++ -std=c++17 -O1 -Wall -Wextra \
        -I "$1" -I "$1/utils" \
        -I build/_deps/nlohmann_json-src/include \
        -I build/_deps/httplib-src \
        -o "$2" testers/i1874_flag_width_check.cpp \
        $(pkg-config --cflags --libs opencv4)
  }
  mutate() { # $1 scratch dir, $2 sed expression
    rm -rf "$1"; mkdir -p "$1"; cp -r src "$1/src"
    local before after
    before=$(grep -c -F -- "$NEEDLE" "$1/$HDR")
    sed -i "$2" "$1/$HDR"
    after=$(grep -c -F -- "$NEEDLE" "$1/$HDR")
    echo "mutation in $1: needle found $before time(s) before, $after after"
    [ "$before" -eq 1 ] && [ "$after" -eq 0 ]
  }
  fails() { grep -c "^FAIL " "$1"; }                   # the space: FAILURES: n is not one
  prefixed() { grep -c "^FAIL $2:" "$1"; }
  tally() { # $1 output; prints "fails count lost kept edge cleared account fixed"
    echo "$(fails "$1") $(prefixed "$1" count) $(prefixed "$1" lost) $(prefixed "$1" kept) $(prefixed "$1" edge) $(prefixed "$1" cleared) $(prefixed "$1" account) $(prefixed "$1" fixed)"
  }

  compile src /tmp/i1874_tree || { echo "COMPILE_FAILED"; exit 2; }
  echo "==== 1. the tree rule ========================================================="
  /tmp/i1874_tree < /dev/null > /tmp/tree.out 2>&1
  RC_TREE=$?
  cat /tmp/tree.out

  echo "==== 2. mutation A: the default is 0.7 again ==================================="
  echo "PREDICTION: exactly 44 FAIL lines: 37 cleared:, 5 count:, 1 lost:, 1 account:."
  mutate /tmp/mutA "s/kDefaultFlagSigmas = 0.6;/kDefaultFlagSigmas = 0.7;/" || { echo "FAIL mutation A did not land"; exit 1; }
  compile /tmp/mutA/src /tmp/i1874_mutA || { echo "COMPILE_FAILED (mutation A)"; exit 2; }
  /tmp/i1874_mutA < /dev/null > /tmp/mutA.out 2>&1
  RC_A=$?
  grep -E "^(FAIL |I1874 )" /tmp/mutA.out
  read A_F A_C A_L A_K A_E A_X A_N A_I <<< "$(tally /tmp/mutA.out)"

  echo "==== 3. mutation B: the default is the issue'"'"'s 0.5 ==============================="
  echo "PREDICTION: exactly 7 FAIL lines: 2 edge:, 4 count:, 1 account:."
  mutate /tmp/mutB "s/kDefaultFlagSigmas = 0.6;/kDefaultFlagSigmas = 0.5;/" || { echo "FAIL mutation B did not land"; exit 1; }
  compile /tmp/mutB/src /tmp/i1874_mutB || { echo "COMPILE_FAILED (mutation B)"; exit 2; }
  /tmp/i1874_mutB < /dev/null > /tmp/mutB.out 2>&1
  RC_B=$?
  grep -E "^(FAIL |I1874 )" /tmp/mutB.out
  read B_F B_C B_L B_K B_E B_X B_N B_I <<< "$(tally /tmp/mutB.out)"

  echo
  echo "tree rc=$RC_TREE"
  echo "mutA rc=$RC_A fails=$A_F predicted=44 count=$A_C lost=$A_L kept=$A_K edge=$A_E cleared=$A_X account=$A_N fixed=$A_I"
  echo "mutB rc=$RC_B fails=$B_F predicted=7 count=$B_C lost=$B_L kept=$B_K edge=$B_E cleared=$B_X account=$B_N fixed=$B_I"
  [ $RC_TREE -eq 0 ] || { echo "FAIL the tree rule did not hold"; exit 1; }
  [ $RC_A -ne 0 ] && [ "$A_F" -eq 44 ] && [ "$A_X" -eq 37 ] && [ "$A_C" -eq 5 ] && [ "$A_L" -eq 1 ] \
    && [ "$A_N" -eq 1 ] && [ "$A_K" -eq 0 ] && [ "$A_E" -eq 0 ] && [ "$A_I" -eq 0 ] \
    || { echo "FAIL mutation A did not fail as predicted"; exit 1; }
  [ $RC_B -ne 0 ] && [ "$B_F" -eq 7 ] && [ "$B_E" -eq 2 ] && [ "$B_C" -eq 4 ] && [ "$B_N" -eq 1 ] \
    && [ "$B_L" -eq 0 ] && [ "$B_K" -eq 0 ] && [ "$B_X" -eq 0 ] && [ "$B_I" -eq 0 ] \
    || { echo "FAIL mutation B did not fail as predicted"; exit 1; }
  echo "ALL THREE HELD"
  exit 0
'
exit $?
