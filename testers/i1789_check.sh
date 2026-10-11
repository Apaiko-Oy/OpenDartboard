#!/bin/bash
# turnaus#1789: a session's truth line and uploaded log read back on the detector side --
# the four accuracy counts per path, each corrected dart with its log window, its fault
# class. A pure check: no detector binary, no footage, no network, no build. The census
# runs in $OD_IMAGE's python 3.11 for #1560's reason (the box that runs the suite has no
# host python3). The fixture is mounted read-only; mutations are made on copies under the
# container's /tmp, each with its prediction printed first.
#
#   1. self-test            one hand-built account per fault class and the figure either
#                           side of each threshold, plus the two loud format refusals
#   2. the fixture          testers/fixtures/i1789: the 2026-10-10 truth line and log
#                           excerpt; every '# expect' line in its header must hold
#   3. mutation A: a pick   the 16:49:15 dart's `picked` alternative -> typed. PREDICTION:
#                           rc 1 with exactly 4 count mismatches -- geometry.alternative
#                           2->1 (the other is #1817's real dart), geometry.typed 0->1, and
#                           the same two in total -- no class mismatch, and one
#                           PICK-DISAGREES naming that dart, because the
#                           sectors (S1 published, S20 offered, S20 corrected) say alternative
#   4. mutation B: header   `degraded` struck from the columns line only, the drift a v1
#                           header already carries. PREDICTION: rc 3, FORMAT-REFUSED on the
#                           first dart line (23 fields where the columns line names 22), and
#                           not one COUNT line printed: refused, never miscounted
#   5. mutation C: the END  the 16:24:29.105 `SCORE: END` struck from the log. PREDICTION:
#                           rc 1 with exactly 2 class mismatches and no count mismatch: the
#                           D11 falls to rim-one-tip (a lone double) and the S2 to ring-wire
#                           (4.35 mm), which is why the takeout's class is decided first
#   6. the real lines       #1817: casual/20's 2026-10-11 push line for 01M4KYTXNW... is
#                           found with its whole window, and 01M4KWAS5R..., which that log
#                           names only on a `frames ... are gone` line, is ABSENT placed
#                           between files with its one mention quoted
#   7. mutation D: a push   that real push line struck from the log. PREDICTION: rc 1 with
#                           exactly 1 mismatch, `absent 01M4KYTXNW... ... the header does
#                           not say so`; its ABSENT line reads where=within-a-file
#                           mentioned=0 (the board was logging and named it nowhere), and no
#                           class or count moves, because a two-line-wire needs no log
#   8. mutation E: mention  the `frames ... are gone` line struck. PREDICTION: rc 1 with
#                           exactly 1 mismatch, `absent 01M4KWAS5R....mentioned expected 1
#                           got 0`; `where` stays between-files
#
# The script ends on `exit`, never on an `echo`: #1463, #1479.
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"

od_run "1789-check" --network none \
  -v "$OD_TREE_ROOT":/app:ro -w /app "$OD_IMAGE" bash -c '
  set -u
  PY="python3 testers/i1789_truth_census.py"
  FX=testers/fixtures/i1789
  TRUTH=$FX/truth-line-2026-10-10.txt
  LOG=$FX/log-2026-10-10.txt
  fails=0
  no() { echo "FAIL $*"; fails=$((fails + 1)); }

  echo "==== 1. self-test =============================================================="
  $PY --self-test; RC=$?
  [ $RC -eq 0 ] || no "the self-test failed (rc=$RC)"

  echo "==== 2. the 2026-10-10 fixture ================================================="
  $PY --truth $TRUTH --log $LOG --expect > /tmp/fixture.out; RC=$?
  cat /tmp/fixture.out
  [ $RC -eq 0 ] || no "the fixture did not census to its header (rc=$RC)"
  grep -q "^I1789 EXPECT HELD" /tmp/fixture.out || no "the fixture printed no EXPECT HELD"
  # Every corrected dart is printed: a DART line for each standing correction, and each
  # either followed by its window or named ABSENT. 15 corrections stand in the fixture.
  DARTS=$(grep -c "^I1789 DART " /tmp/fixture.out)
  [ "$DARTS" -eq 15 ] || no "the fixture printed $DARTS DART lines where 15 corrections stand"

  echo "==== 3. mutation A: the 16:49:15 dart picked typed, not alternative ============"
  echo "PREDICTION: rc 1, exactly 4 count mismatches (geometry.alternative, geometry.typed,"
  echo "            total.alternative, total.typed), 0 class mismatches, 1 PICK-DISAGREES"
  echo "            naming 01M4JFX0000000000000164915."
  sed "s/^\(01M4JFX0000000000000164915 S1 S20 S20\) alternative /\1 typed /" $TRUTH > /tmp/mutA.txt
  [ "$(diff $TRUTH /tmp/mutA.txt | grep -c "^>")" -eq 1 ] || no "mutation A did not change exactly one line"
  $PY --truth /tmp/mutA.txt --log $LOG --expect > /tmp/mutA.out; RC_A=$?
  grep -E "^I1789 (MISMATCH|PICK-DISAGREES|EXPECT)" /tmp/mutA.out
  A_COUNT=$(grep -c "^I1789 MISMATCH count " /tmp/mutA.out)
  A_CLASS=$(grep -c "^I1789 MISMATCH class " /tmp/mutA.out)
  A_NAMED=$(grep -cE "^I1789 MISMATCH count (geometry|total)\.(alternative expected 2 got 1|typed expected 0 got 1)$" /tmp/mutA.out)
  A_PICK=$(grep -c "^I1789 PICK-DISAGREES ref=01M4JFX0000000000000164915 " /tmp/mutA.out)
  echo "mutA rc=$RC_A count=$A_COUNT named=$A_NAMED class=$A_CLASS pick=$A_PICK"
  [ $RC_A -eq 1 ] && [ "$A_COUNT" -eq 4 ] && [ "$A_NAMED" -eq 4 ] && [ "$A_CLASS" -eq 0 ] && [ "$A_PICK" -eq 1 ] \
    || no "mutation A did not move exactly the four predicted counts"

  echo "==== 4. mutation B: degraded struck from the columns line ======================"
  echo "PREDICTION: rc 3, FORMAT-REFUSED naming the first dart line, no COUNT line."
  sed "s/ flagged degraded candidates / flagged candidates /" $TRUTH > /tmp/mutB.txt
  [ "$(diff $TRUTH /tmp/mutB.txt | grep -c "^>")" -eq 1 ] || no "mutation B did not change exactly one line"
  FIRST=$(( $(grep -n "^# turnaus truth line" /tmp/mutB.txt | cut -d: -f1) + 2 ))
  $PY --truth /tmp/mutB.txt --log $LOG --expect > /tmp/mutB.out; RC_B=$?
  cat /tmp/mutB.out
  echo "mutB rc=$RC_B first_dart_line=$FIRST"
  [ $RC_B -eq 3 ] || no "mutation B was not refused (rc=$RC_B)"
  grep -q "^I1789 FORMAT-REFUSED line $FIRST: 23 fields where the columns line names 22" /tmp/mutB.out \
    || no "mutation B was not refused on line $FIRST by its field count"
  ! grep -q "^I1789 COUNT " /tmp/mutB.out || no "mutation B printed a count from a misread export"

  echo "==== 5. mutation C: the 16:24:29.105 END struck from the log ==================="
  echo "PREDICTION: rc 1, exactly 2 class mismatches -- 01M4JZS12A... got rim-one-tip,"
  echo "            01M4JZS2R1... got ring-wire -- and no count mismatch."
  grep -v "^\[16:24:29.105\]" $LOG > /tmp/mutC.txt
  [ "$(diff $LOG /tmp/mutC.txt | grep -c "^<")" -eq 1 ] || no "mutation C did not strike exactly one line"
  $PY --truth $TRUTH --log /tmp/mutC.txt --expect > /tmp/mutC.out; RC_C=$?
  grep -E "^I1789 (MISMATCH|EXPECT)" /tmp/mutC.out
  C_CLASS=$(grep -c "^I1789 MISMATCH class " /tmp/mutC.out)
  C_COUNT=$(grep -c "^I1789 MISMATCH count " /tmp/mutC.out)
  C_D11=$(grep -c "^I1789 MISMATCH class 01M4JZS12A0000000000162428 expected phantom-takeout got rim-one-tip$" /tmp/mutC.out)
  C_S2=$(grep -c "^I1789 MISMATCH class 01M4JZS2R10000000000162430 expected phantom-takeout got ring-wire$" /tmp/mutC.out)
  echo "mutC rc=$RC_C class=$C_CLASS count=$C_COUNT d11=$C_D11 s2=$C_S2"
  [ $RC_C -eq 1 ] && [ "$C_CLASS" -eq 2 ] && [ "$C_COUNT" -eq 0 ] && [ "$C_D11" -eq 1 ] && [ "$C_S2" -eq 1 ] \
    || no "mutation C did not move exactly the two predicted classes"

  echo "==== 6. the real casual/20 lines (#1817) ======================================"
  REAL=01M4KYTXNWXP184NBAJ0TMKNNW
  GONE=01M4KWAS5RTFSTE99GK40DGPVH
  # The DART line, then its window: the four shown lines and the push, nothing between.
  grep -A5 "^I1789 DART ref=$REAL " /tmp/fixture.out > /tmp/real.out
  cat /tmp/real.out
  [ "$(sed -n 5p /tmp/real.out)" = "    01:27:16.284 SCORE: S19 | Position: (572,305) | Confidence: 0.700000 | Camera: 0 | Processing: 64ms" ] \
    || no "the real dart was not shown its SCORE line"
  sed -n 6p /tmp/real.out | grep -q "^    01:27:16.489 TURNAUS: COUNTED {.*\"reference\":\"$REAL\"" \
    || no "the real dart was not shown its push line"
  ! grep -q "^I1789 ABSENT ref=$REAL " /tmp/fixture.out || no "the real push line was not found"
  grep -q "^I1789 ABSENT ref=$GONE where=between-files posted=00:43:30.000 .* mentioned=1 no push line names the reference; first mention: TURNAUS: frames for $GONE are gone$" /tmp/fixture.out \
    || no "the real absence did not say where it falls and what the log holds"
  ! grep -q "the reference is not in the log" /tmp/fixture.out || no "an absence still says the reference is not in the log"

  echo "==== 7. mutation D: the real push line struck =================================="
  echo "PREDICTION: rc 1, exactly 1 mismatch -- absent $REAL is absent and the header does"
  echo "            not say so -- placed where=within-a-file mentioned=0; no class or count."
  grep -v "^\[01:27:16.489\]" $LOG > /tmp/mutD.txt
  [ "$(diff $LOG /tmp/mutD.txt | grep -c "^<")" -eq 1 ] || no "mutation D did not strike exactly one line"
  $PY --truth $TRUTH --log /tmp/mutD.txt --expect > /tmp/mutD.out; RC_D=$?
  grep -E "^I1789 (MISMATCH|EXPECT|ABSENT ref=$REAL)" /tmp/mutD.out
  D_ALL=$(grep -c "^I1789 MISMATCH " /tmp/mutD.out)
  D_NAMED=$(grep -c "^I1789 MISMATCH absent $REAL is absent from the log and the header does not say so$" /tmp/mutD.out)
  D_WHERE=$(grep -c "^I1789 ABSENT ref=$REAL where=within-a-file .* mentioned=0 " /tmp/mutD.out)
  echo "mutD rc=$RC_D mismatches=$D_ALL named=$D_NAMED within=$D_WHERE"
  [ $RC_D -eq 1 ] && [ "$D_ALL" -eq 1 ] && [ "$D_NAMED" -eq 1 ] && [ "$D_WHERE" -eq 1 ] \
    || no "mutation D did not lose exactly the real push"

  echo "==== 8. mutation E: the frames-are-gone line struck ============================"
  echo "PREDICTION: rc 1, exactly 1 mismatch -- absent $GONE.mentioned expected 1 got 0 --"
  echo "            and its ABSENT line still where=between-files."
  grep -v "^\[00:56:57.409\]" $LOG > /tmp/mutE.txt
  [ "$(diff $LOG /tmp/mutE.txt | grep -c "^<")" -eq 1 ] || no "mutation E did not strike exactly one line"
  $PY --truth $TRUTH --log /tmp/mutE.txt --expect > /tmp/mutE.out; RC_E=$?
  grep -E "^I1789 (MISMATCH|EXPECT|ABSENT ref=$GONE)" /tmp/mutE.out
  E_ALL=$(grep -c "^I1789 MISMATCH " /tmp/mutE.out)
  E_NAMED=$(grep -c "^I1789 MISMATCH absent $GONE.mentioned expected 1 got 0$" /tmp/mutE.out)
  E_WHERE=$(grep -c "^I1789 ABSENT ref=$GONE where=between-files .* mentioned=0 " /tmp/mutE.out)
  echo "mutE rc=$RC_E mismatches=$E_ALL named=$E_NAMED between=$E_WHERE"
  [ $RC_E -eq 1 ] && [ "$E_ALL" -eq 1 ] && [ "$E_NAMED" -eq 1 ] && [ "$E_WHERE" -eq 1 ] \
    || no "mutation E did not move exactly the mention"

  echo
  if [ $fails -eq 0 ]; then echo "ALL EIGHT HELD"; exit 0; fi
  echo "FAILURES: $fails"; exit 1
' < /dev/null
RC=$?
echo "RUN=1789-truth rc=$RC"
exit $RC
