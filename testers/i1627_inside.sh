#!/bin/bash
# #1627, inside the container. Run by testers/i1627_run.sh; see it for the one-line story.
#
# THE FINDING. motion_processing's case STABILIZING used to answer a spike -- the AVERAGE
# of the measured cameras' board ratios over the spike threshold -- by going to IDLE
# ("probably dart removal, reset"). When the spike is a one- or two-cycle blip nothing
# re-opens the event, so the motion it was about never makes a window. On
# mocks/rig-20260922 dev under OD_LOOK_BUDGET=1605 OD_SEEK_ALIGN=1618 that motion is the
# visit-7 takeout: it settles, camera 3 alone reads 0.0107 and then 0.0426 of its board,
# the average 0.0142 crosses 0.011, and the event is gone. The next window is v8.1's own
# arrival: cameras 1 and 3 see their cumulative change FALL from three darts to one,
# #1518's CLEAN BY REVERSION reads that as the takeout, the board reconciles CLEAN and the
# clean reference is re-based with the T1 standing in it.
#
# THE REPAIR. The spike re-opens the event on the cycle it is measured (SPIKE_DETECTED,
# the start clock restarted) -- what IDLE would do one cycle later if the motion were still
# over the threshold then. OD_SETTLE_SPIKE=discard restores the old line.
#
# WHAT IS ASSERTED
#   A  THE DROPPED EVENT, pinned (OD_SETTLE_SPIKE=discard), both switches on: a SETTLE
#      SPIKE line that discards, carried by camera 3 alone; the window after it is not a
#      takeout window but v8.1's, and reads CLEAN BY REVERSION on two or more cameras;
#      v8.1 is undetected.
#   B  THE REPAIR, same switches: the same spike keeps the event; the window after it is a
#      plain CLEAN (no reversion needed) and the one after that publishes T1; v8.1 is T1;
#      nothing the pinned run gets right is lost.
#   C  THE DEFAULT (both switches off): v8.1 is T1 as it is on main.
#
# PREDICTION, stated before the repaired binary was first replayed (2026-09-25 19:45):
# one SETTLE SPIKE near cycle 2563, camera 3 about 0.043, average about 0.014; a takeout
# window reconciling CLEAN; v8.1 publishing T1; rig-22 dev 20/23 (#1618's 19 + v8.1).
# MEASURED: cycle 2563, camera 3 0.042649, average 0.014249; takeout window CLEAN; v8.1 T1
# exact; 20/23.
#
# The script ends on `exit`, never on an `echo`: #1463, #1479.
set -u

BIN=/app/build/opendartboard
RUN=/run1627
A22=/app/testers/i1511_annotations/rig-20260922.csv
T22=/app/testers/i1499_truth_rig20260922.md
FAIL=0
note() { if [ "$1" -eq 0 ]; then echo "ok   $2"; else echo "FAIL $2"; FAIL=$((FAIL + 1)); fi; }

if [ ! -x $BIN ]; then
  echo "FAIL no $BIN -- a fresh worktree has none. Build it: testers/run_all.sh 1627-takeout"
  exit 1
fi
NEWEST=$(find /app/src /app/CMakeLists.txt -newer $BIN 2>/dev/null | head -5)
if [ -n "$NEWEST" ]; then
  echo "FAIL $BIN is OLDER than the source it is supposed to be measuring:"
  echo "$NEWEST"
  exit 1
fi

R22=/app/mocks/rig-20260922
replay() { # $1 out name, $2.. env
  local out="$1"; shift
  rm -rf $RUN/cache $RUN/debug_frames
  ( cd $RUN && env OD_MAX_CYCLES=0 OD_GEO_SCORE=on OD_SHAFT_CENSUS=1 "$@" timeout 900 $BIN \
      --cams "$R22/cam_1.mp4,$R22/cam_2.mp4,$R22/cam_3.mp4" --width 1280 --height 720 \
      > $RUN/$out.out 2>&1 )
  local rc=$?
  sed 's/\x1b\[[0-9;]*m//g' $RUN/$out.out > $RUN/$out.txt
  python3 /app/testers/i1555_census.py --log $RUN/$out.txt --truth $T22 --annotations $A22 \
      --fixture rig-20260922 --window dev --min-matched 2 --no-arrival 1.1 > $RUN/census-$out.txt
  python3 /app/testers/i1627_census.py --log $RUN/$out.txt --label $out > $RUN/i1627-$out.txt
  echo "     replay $out rc=$rc lines=$(wc -l < $RUN/$out.txt)"
  return $rc
}
correct_set() { grep '^I1555 PAIR ' "$1" | grep -E 'published=[^ ]+ exact' | awk '{print $3}' | sort; }
dart_line() { grep -E "^I1555 (PAIR|ACCURACY-DART) $2 " "$1" | head -1; }
field() { echo "$1" | grep -oE "(^| )$2=[^ ]*" | head -1 | sed "s/^ *$2=//"; }

echo "---- the replays: rig-20260922, dev window ----"
replay r22-on-discard OD_LOOK_BUDGET=1605 OD_SEEK_ALIGN=1618 OD_SETTLE_SPIKE=discard; RC1=$?
replay r22-on         OD_LOOK_BUDGET=1605 OD_SEEK_ALIGN=1618;                         RC2=$?
replay r22-default;                                                                    RC3=$?
[ "$RC1$RC2$RC3" = 000 ]; note $? "all three replays ended cleanly (rc $RC1 $RC2 $RC3)"
for r in r22-on-discard r22-on r22-default; do
  echo "     $r: $(grep '^I1555 ACCURACY ' $RUN/census-$r.txt | sed 's/^I1555 ACCURACY //')"
  sed 's/^/     /' $RUN/i1627-$r.txt
done

# The spike in question: the one whose following window is where v8.1's T1 is decided.
# Each run has it as its only SETTLE SPIKE line (measured: one per run, 2026-09-25); the
# census prints them all, and the assertions below name which.
echo
echo "---- A. the dropped event, pinned (OD_SETTLE_SPIKE=discard) ----"
SA="$(grep '^I1627 SPIKE r22-on-discard ' $RUN/i1627-r22-on-discard.txt | head -1)"
echo "     PREDICTED: a discard carried by camera 3 alone (about 0.043 of its board, average about 0.014 over 0.011);"
echo "                the next window is v8.1's, reverts on 2+ cameras and publishes nothing; v8.1 undetected"
echo "     measured:  ${SA:-no SETTLE SPIKE line}"
[ "$(field "$SA" action)" = discarded ] && [ "$(field "$SA" peak_cam)" = 3 ] && [ "$(field "$SA" others_under)" = 1 ] &&
  awk -v a="$(field "$SA" average)" -v t="$(field "$SA" threshold)" 'BEGIN{exit !(a > t)}'
note $? "the pinned line drops the event on camera 3's spike alone (average $(field "$SA" average) over $(field "$SA" threshold), camera 3 at $(field "$SA" peak))"
[ "$(field "$SA" first)" = END ] && [ "${SA:+$(field "$SA" reversions)}" -ge 2 ] 2> /dev/null
note $? "the window after it reconciles CLEAN BY REVERSION on $(field "$SA" reversions) camera(s): v8.1's arrival read as the takeout"
D1="$(dart_line $RUN/census-r22-on-discard.txt v8.1)"
echo "     $D1"
echo "$D1" | grep -q 'UNDETECTED'; note $? "pinned: v8.1 is undetected -- its T1 is in the re-based reference"

echo
echo "---- B. the repair: the spike keeps the event ----"
SB="$(grep '^I1627 SPIKE r22-on ' $RUN/i1627-r22-on.txt | head -1)"
echo "     PREDICTED: the same spike, kept; a plain CLEAN takeout window, then T1"
echo "     measured:  ${SB:-no SETTLE SPIKE line}"
[ "$(field "$SB" action)" = kept ] && [ "$(field "$SB" cycle)" = "$(field "$SA" cycle)" ] && [ "$(field "$SB" peak_cam)" = 3 ]
note $? "the same spike (cycle $(field "$SB" cycle)) now keeps the event"
[ "$(field "$SB" first)" = END ] && [ "$(field "$SB" reversions)" = 0 ] && [ "$(field "$SB" second)" = T1 ]
note $? "the takeout gets its own window (CLEAN, $(field "$SB" reversions) reversion(s)), and the next one publishes $(field "$SB" second)"
D2="$(dart_line $RUN/census-r22-on.txt v8.1)"
echo "     $D2"
echo "$D2" | grep -qE 'published=T1 exact'; note $? "repaired: v8.1 publishes T1"
correct_set $RUN/census-r22-on-discard.txt > $RUN/c1
correct_set $RUN/census-r22-on.txt > $RUN/c2
LOST="$(comm -23 $RUN/c1 $RUN/c2 | tr '\n' ' ')"
GAINED="$(comm -13 $RUN/c1 $RUN/c2 | tr '\n' ' ')"
echo "     pinned -> repaired: lost ${LOST:-none}, gained ${GAINED:-none}"
[ -z "$LOST" ] && echo " $GAINED" | grep -q ' v8.1 '
note $? "nothing correct pinned is lost, and v8.1 is gained"

echo
echo "---- C. the default: both switches off ----"
D3="$(dart_line $RUN/census-r22-default.txt v8.1)"
echo "     $D3"
echo "$D3" | grep -qE 'published=T1 exact'; note $? "default: v8.1 publishes T1, as on main"

echo
if [ "$FAIL" -gt 0 ]; then echo "RESULT: $FAIL failure(s)"; exit 1; fi
echo "RESULT: a spike while an event settles no longer drops the event; rig-20260922's visit-7 takeout gets its window and v8.1 publishes T1 with OD_LOOK_BUDGET=1605 OD_SEEK_ALIGN=1618 on"
exit 0
