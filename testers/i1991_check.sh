#!/bin/bash
# turnaus#1991: every published dart's window kept on disk (OD_KEEP_WINDOWS,
# src/detector/geometry/detection/window_keep.hpp), and a session's darts graded against
# its truth line (i1991_grade.py). Two pure checks in one container: no detector binary, no
# network, no build directory -- so OD_SKIP_BUILD changes nothing. The tree is mounted
# read-only; every mutation is made on a copy under the container's /tmp, with its
# prediction printed first, and must go red BY NAME.
#
#   1. i1991_keep_check.cpp   the switch off writes nothing; a three-dart visit is 12
#                             pictures and 3 accounts, each picture exact under its role;
#                             the bound holds oldest first across a restart, and every
#                             account on disk has every picture it names
#   2. mutation K1            Ledger::evict puts a dart's pictures BEFORE its account.
#                             PREDICTION: rc 1, the `bounded: every account on disk has
#                             every picture it names` check FAILS and names a `fresh`
#                             picture that is gone: a dart's working background is dated
#                             by that dart, so it is the first picture to go before the
#                             account that names it. (First written predicting `settled`,
#                             and the run said `fresh`: a dart's OWN settled picture is
#                             re-dated by the next dart, which reads it as its reference.)
#   3. mutation K2            Ledger::touch no longer re-dates a picture used again.
#                             PREDICTION: rc 1, the same check FAILS and names a `clean`
#                             or `fresh` picture: a reference evicted under a later dart
#   4. grader self-test       one hand-built line: the span, the three kinds of truth,
#                             fixed / broke / moved / phantom-unread, the loud refusals
#   5. the fixture, board     testers/fixtures/i1789's 2026-10-10 truth line graded as the
#                             board published it: graded=28 exact=9 corrected_exact=0
#                             three_camera=1 vote=23 (span darts 4-37)
#   6. the identity replay    the board's own reads as I1991 REPLAY lines: diffs=0 same=37
#   7. mutation G1: a read    one replay line (01M4JFX0000000000000155149, published T19,
#                             corrected to S19) made to read S19. PREDICTION: rc 1 held
#                             against step 6's expectations with exactly 4 mismatches --
#                             exact 9->10, diffs 0->1, same 37->36, fixed 0->1 -- and one
#                             DIFF line naming that reference kind=fixed
#   8. mutation G2: the rule  truth_of() made to return what was published for every dart.
#                             PREDICTION: the self-test goes rc 1 and names `replay: exact
#                             is the one dart it now reads as its correction`
#
# MEASURED (#1341's rule): see run_all.sh beside the row; the WALL line prints it.
# The script ends on `exit`, never on an `echo`: #1463, #1479.
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"

T0=$(date +%s)
od_run "1991-check" --network none \
  -v "$OD_TREE_ROOT":/app:ro -w /app "$OD_IMAGE" bash -c '
  set -u
  RC=0
  FLAGS="-std=c++17 -O1 -Wall -Wextra"
  LIBS="$(pkg-config --cflags --libs opencv4) -lpthread"
  fail() { echo "FAIL $*"; RC=1; }

  echo "==== 1. i1991_keep_check ============================================="
  g++ $FLAGS -I src -I src/utils -o /tmp/keep testers/i1991_keep_check.cpp $LIBS || { echo "COMPILE_FAILED i1991_keep_check"; exit 2; }
  /tmp/keep < /dev/null; rc=$?
  echo "i1991_keep_check rc=$rc"
  [ $rc -eq 0 ] || RC=1

  mutate() { # label, sed expression, the role(s) the red check must name
    local label="$1" expr="$2" roles="$3"
    rm -rf /tmp/mut && mkdir -p /tmp/mut && cp -r src /tmp/mut/src
    sed -i "$expr" /tmp/mut/src/detector/geometry/detection/window_keep.hpp
    if cmp -s src/detector/geometry/detection/window_keep.hpp /tmp/mut/src/detector/geometry/detection/window_keep.hpp; then
      fail "$label: the mutation changed nothing -- the line it rewrites is no longer in window_keep.hpp"; return
    fi
    g++ $FLAGS -I /tmp/mut/src -I /tmp/mut/src/utils -o /tmp/keep_mut testers/i1991_keep_check.cpp $LIBS || { fail "$label: the mutant does not compile"; return; }
    /tmp/keep_mut < /dev/null > /tmp/mut.out 2>&1; rc=$?
    grep "^FAIL" /tmp/mut.out
    if [ $rc -eq 1 ] && grep -Eq "^FAIL bounded: every account on disk has every picture it names.* names the ($roles) picture [0-9a-f]{16} and it is gone" /tmp/mut.out; then
      echo "PASS $label: rc=1 and the account check is red, naming a ($roles) picture"
    else
      fail "$label: predicted rc 1 with the account check naming a ($roles) picture; got rc=$rc"
    fi
  }
  echo "==== 2. mutation K1: pictures evicted before their account ============"
  mutate K1 "s|return a->account; // the account before its pictures|return !a->account;|" "fresh"
  echo "==== 3. mutation K2: a picture used again is not re-dated =============="
  mutate K2 "s|it->second.ordinal = ordinal;|(void)ordinal;|" "clean|fresh"

  echo "==== 4. grader self-test =============================================="
  python3 testers/i1991_grade.py --self-test || fail "the grader self-test"

  TRUTH=testers/fixtures/i1789/truth-line-2026-10-10.txt
  echo "==== 5. the fixture, as the board published it ========================"
  python3 testers/i1991_grade.py --truth $TRUTH --board \
    --expect first=4 last=37 graded=28 exact=9 corrected_exact=0 uncorrected_exact=9 three_camera=1 vote=23 missing=0 \
    || fail "the fixture graded as the board published it"

  echo "==== 6. the identity replay ==========================================="
  python3 testers/i1991_grade.py --truth $TRUTH --emit-board-replay > /tmp/identity.txt
  SAME="graded=28 exact=9 diffs=0 same=37 fixed=0 broke=0 moved=0 missing=0"
  python3 testers/i1991_grade.py --truth $TRUTH --replay /tmp/identity.txt --expect $SAME || fail "the identity replay"

  echo "==== 7. mutation G1: one read changed to its correction ==============="
  sed "s|ref=01M4JFX0000000000000155149 sector=T19 |ref=01M4JFX0000000000000155149 sector=S19 |" /tmp/identity.txt > /tmp/g1.txt
  python3 testers/i1991_grade.py --truth $TRUTH --replay /tmp/g1.txt --expect $SAME > /tmp/g1.out; rc=$?
  grep -E "DIFF|MISMATCH|EXPECT" /tmp/g1.out
  if [ $rc -eq 1 ] && [ "$(grep -c "^I1991 MISMATCH" /tmp/g1.out)" = 4 ] \
     && grep -q "^I1991 MISMATCH fixed expected 0 got 1" /tmp/g1.out \
     && grep -q "^I1991 MISMATCH exact expected 9 got 10" /tmp/g1.out \
     && grep -q "^I1991 MISMATCH diffs expected 0 got 1" /tmp/g1.out \
     && grep -q "^I1991 MISMATCH same expected 37 got 36" /tmp/g1.out \
     && [ "$(grep -c "^I1991 DIFF" /tmp/g1.out)" = 1 ] \
     && grep -q "^I1991 DIFF ref=01M4JFX0000000000000155149 .*truth=S19 board=T19 replay=S19 .*kind=fixed" /tmp/g1.out; then
    echo "PASS G1: rc=1, exactly exact/diffs/same/fixed moved, and the one DIFF names the dart as fixed"
  else
    fail "G1: predicted rc 1 with 4 mismatches and one DIFF kind=fixed; got rc=$rc"
  fi

  echo "==== 8. mutation G2: every dart graded against what was published ====="
  mkdir -p /tmp/g2 && cp testers/i1991_grade.py testers/i1789_truth_census.py /tmp/g2/
  sed -i "s|    return row\[\"corrected\"\] if kind_of(row) == \"corrected\" else row\[\"published\"\]|    return row[\"published\"]|" /tmp/g2/i1991_grade.py
  if cmp -s testers/i1991_grade.py /tmp/g2/i1991_grade.py; then fail "G2: the mutation changed nothing"; fi
  python3 /tmp/g2/i1991_grade.py --self-test > /tmp/g2.out; rc=$?
  grep "^FAIL" /tmp/g2.out
  if [ $rc -eq 1 ] && grep -q "^FAIL replay: exact is the one dart it now reads as its correction" /tmp/g2.out; then
    echo "PASS G2: the self-test is red and names the corrected dart"
  else
    fail "G2: predicted the self-test red on the corrected dart; got rc=$rc"
  fi
  exit $RC
'
RC=$?
T1=$(date +%s)
echo "WALL $((T1 - T0)) s"
echo "CHECK_RC=$RC"
# The harness must exit on what it measured: run_all.sh reads the exit code and nothing else.
exit $RC
