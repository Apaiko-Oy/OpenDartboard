set -u
# #1618, inside the container: why a correctly calibrated third camera made rig-20260922 dev
# WORSE (15/23 -> 11/23 under #1605's 31 looks), and the alignment that repairs it --
# the default since #1631, with OD_SEEK_ALIGN=off as the pin.
#
# THE FINDING. It is the dev replay, not the third camera. A dev build (DEBUG_SEEK_VIDEO)
# seeks file camera i by 3 - 0.18 i seconds -- frames 90, 84 and 79 -- and nothing ever
# brings them back into step, so a dev-window replay watches camera 1 six frames ahead of
# camera 2 and eleven ahead of camera 3. With cameras 2 and 3 scoring alone that skew is
# five frames and mostly harmless. Once #1605's budget admits camera 1, camera 1 calls a
# throw the moment it lands, and cameras 2 and 3 call the SAME throw again 7-11 cycles
# later, when their copy of the recording reaches it: an ECHO window. Visit 2's third dart,
# v5.3 and v7.3 are lost to echoes, v2.2 and v5.2 are solved through a camera 3 that has
# not yet seen them, and v3.2's vote meets a camera 3 still showing the thrower. Camera 1's
# own axes are as good as the opening window's. The alignment (default since #1631) reads the lagging
# files forward once calibration is done (scorer.cpp, seekAlignIsOn); a release build and
# a live rig never seek at all.
#
# #1631 MADE BOTH THE DEFAULT, so the three replays are the same three configurations
# under new names: `r22-dev-pinned` (OD_LOOK_BUDGET=12 OD_SEEK_ALIGN=off) is what was
# the default, `r22-dev-noalign` (OD_SEEK_ALIGN=off) is what was #1605's opt-in alone,
# and `r22-dev` (no switch) is what was the opt-in aligned.
#
# WHAT IS ASSERTED, predictions first in the output:
#   A  THE CAUSE: the dev seek staggers the three files; under OD_SEEK_ALIGN=off, with
#      either budget, nothing is aligned (no SEEK ALIGN line) and the pin says it is set.
#   B  THE MECHANISM, 31 looks and OD_SEEK_ALIGN=off: echo windows, each closer behind
#      the previous call than the 11-frame skew plus one cycle; none with both pins.
#   C  CAMERA 1 IS NOT THE FAULT: its valid axes in that run sit a median <= 1.0 deg and
#      <= 6 px (annotated tip off the axis) from the hand annotations.
#   D  THE REPAIR, the default (31 looks, aligned): the files are read forward by
#      exactly the seek differences, no echo is left, rig-22 dev reads ABOVE 15/23 and
#      above the pinned run, and every dart the account attributes to the skew is
#      correct again.
#   E  WHAT THE DEFAULT STILL LOSES: the darts correct with both pins and not by default
#      are exactly v7.2 -- a lone camera-1 reading past the 3/19 wire, not an echo
#      (#1628, no safe rule). Before #1627 it was v7.2 and v8.1; #1627 repaired v8.1's
#      takeout. The set is asserted exactly, so a new loss or a v7.2 repair is red here.
#   F  AN UNSEEKED WINDOW IS UNTOUCHED: at the clip's opening, under the default,
#      nothing is read forward.
#
# Three whole-clip rig-20260922 replays and one calibration-only run.
# The script ends on `exit`, never on an `echo`: #1463, #1479.

BIN=/app/build/opendartboard
RUN=/run1618
A22=/app/testers/i1511_annotations/rig-20260922.csv
T22=/app/testers/i1499_truth_rig20260922.md
FAIL=0
note() { if [ "$1" -eq 0 ]; then echo "ok   $2"; else echo "FAIL $2"; FAIL=$((FAIL + 1)); fi; }

if [ ! -x $BIN ]; then
  echo "FAIL no $BIN -- a fresh worktree has none. Build it: testers/run_all.sh 1618"
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
  python3 /app/testers/i1618_census.py --log $RUN/$out.txt --annotations $A22 --label $out \
      --no-arrival 1.1 > $RUN/i1618-$out.txt
  echo "     replay $out rc=$rc lines=$(wc -l < $RUN/$out.txt)"
  return $rc
}
correct_set() { grep '^I1555 PAIR ' "$1" | grep -E 'published=[^ ]+ exact' | awk '{print $3}' | sort; }
accuracy_of() { grep -oE 'correct [0-9.]+/[0-9]+' "$1" | head -1; }
correct_n() { grep -oE 'correct [0-9]+' "$1" | head -1 | grep -oE '[0-9]+$'; }

echo "---- the three replays: rig-20260922, dev window ----"
replay r22-dev-pinned     OD_LOOK_BUDGET=12 OD_SEEK_ALIGN=off; RC1=$?
replay r22-dev-noalign    OD_SEEK_ALIGN=off;             RC2=$?
replay r22-dev;                                          RC3=$?
[ "$RC1$RC2$RC3" = 000 ]; note $? "all three replays ended cleanly (rc $RC1 $RC2 $RC3)"
for r in r22-dev-pinned r22-dev-noalign r22-dev; do
  echo "     $r: $(grep '^I1555 ACCURACY ' $RUN/census-$r.txt | sed 's/^I1555 ACCURACY //')"
done

echo
echo "---- A. the cause: the dev seek staggers the files ----"
SEEKS="$(grep -oE 'video [0-9] seeked forward by [0-9.]+ seconds \(frame [0-9]+\)' $RUN/r22-dev-pinned.txt | sed -E 's/video ([0-9]).*frame ([0-9]+).*/\1:\2/' | tr '\n' ' ')"
echo "     PREDICTED: frames 90, 84 and 79.  measured: $SEEKS"
[ "$SEEKS" = "1:90 2:84 3:79 " ]; note $? "the three file cameras start 0, 6 and 11 frames apart"
! grep -q 'SEEK ALIGN' $RUN/r22-dev-pinned.txt; note $? "both pins (the old default): not aligned"
! grep -q 'SEEK ALIGN' $RUN/r22-dev-noalign.txt; note $? "31 looks with OD_SEEK_ALIGN=off (the old opt-in alone): not aligned"
grep -q 'OD_SEEK_ALIGN=off is set' $RUN/r22-dev-pinned.txt && grep -q 'OD_SEEK_ALIGN=off is set' $RUN/r22-dev-noalign.txt
note $? "and both say the alignment pin is set"

echo
echo "---- B. the mechanism: echo windows once camera 1 votes ----"
grep -E '^I1618 (ECHO|ECHOES) ' $RUN/i1618-r22-dev-noalign.txt | sed 's/^/     /'
E1="$(sed -n 's/^I1618 ECHOES r22-dev-pinned n=\([0-9]*\).*/\1/p' $RUN/i1618-r22-dev-pinned.txt)"
E2="$(sed -n 's/^I1618 ECHOES r22-dev-noalign n=\([0-9]*\).*/\1/p' $RUN/i1618-r22-dev-noalign.txt)"
G2="$(sed -n 's/^I1618 ECHOES r22-dev-noalign .*max_gap=\([0-9]*\).*/\1/p' $RUN/i1618-r22-dev-noalign.txt)"
echo "     PREDICTED: three or more echoes with 31 looks and OD_SEEK_ALIGN=off, each within 12 cycles (the 11-frame skew + 1); none with both pins"
[ "${E2:-0}" -ge 3 ] && [ -n "$G2" ] && [ "$G2" -le 12 ]
note $? "31 looks, OD_SEEK_ALIGN=off: ${E2:-0} echo window(s), the widest ${G2:-none} cycles behind the call before it"
[ "${E1:-x}" = 0 ]; note $? "both pins: ${E1:-?} echo windows"

echo
echo "---- C. camera 1's own evidence, 31 looks and OD_SEEK_ALIGN=off ----"
C1="$(grep '^I1618 CAM1 ' $RUN/i1618-r22-dev-noalign.txt)"
echo "     $C1"
MA="$(echo "$C1" | sed -n 's/.*median_dAng=\([0-9.]*\).*/\1/p')"
MP="$(echo "$C1" | sed -n 's/.*median_tipPerp=\([0-9.]*\).*/\1/p')"
[ -n "$MA" ] && [ -n "$MP" ] && awk -v a="$MA" -v p="$MP" 'BEGIN{exit !(a <= 1.0 && p <= 6.0)}'
note $? "camera 1's valid axes: median ${MA:-?} deg and ${MP:-?} px from the annotations (<= 1.0 deg, <= 6 px)"

echo
echo "---- D. the repair, now the default: 31 looks, aligned ----"
AL="$(grep -oE 'SEEK ALIGN: video 2 read forward [0-9]+ of [0-9]+ frame\(s\), video 3 read forward [0-9]+ of [0-9]+ frame\(s\)' $RUN/r22-dev.txt)"
echo "     PREDICTED: video 2 read forward 6, video 3 read forward 11; no echo; above 15/23"
echo "     measured:  ${AL:-no SEEK ALIGN line}"
[ "$AL" = "SEEK ALIGN: video 2 read forward 6 of 6 frame(s), video 3 read forward 11 of 11 frame(s)" ]
note $? "the lagging files are read forward by exactly the seek differences"
E3="$(sed -n 's/^I1618 ECHOES r22-dev n=\([0-9]*\).*/\1/p' $RUN/i1618-r22-dev.txt)"
[ "${E3:-x}" = 0 ]; note $? "aligned: ${E3:-?} echo windows"
N1="$(correct_n $RUN/census-r22-dev-pinned.txt)"; N2="$(correct_n $RUN/census-r22-dev-noalign.txt)"
N3="$(correct_n $RUN/census-r22-dev.txt)"
echo "     correct: both pins ${N1:-?}/23, OD_SEEK_ALIGN=off ${N2:-?}/23, default ${N3:-?}/23"
[ -n "$N3" ] && [ -n "$N1" ] && [ "$N3" -gt 15 ] && [ "$N3" -gt "$N1" ]
note $? "the default reads above 15/23 and above both pins"
correct_set $RUN/census-r22-dev-noalign.txt > $RUN/c2
correct_set $RUN/census-r22-dev.txt > $RUN/c3
correct_set $RUN/census-r22-dev-pinned.txt > $RUN/c1
SKEW_DARTS="v2.2 v2.3 v3.2 v5.2 v5.3 v7.3"
BACK=0
for d in $SKEW_DARTS; do
  in2=$(grep -cx "$d" $RUN/c2); in3=$(grep -cx "$d" $RUN/c3)
  echo "     $d: unaligned $( [ "$in2" = 1 ] && echo correct || echo wrong ) -> aligned $( [ "$in3" = 1 ] && echo correct || echo wrong )"
  [ "$in2" = 0 ] && [ "$in3" = 1 ] && BACK=$((BACK + 1))
done
[ "$BACK" = 6 ]; note $? "all six darts the account lays on the skew are wrong unaligned and correct aligned ($BACK of 6)"

echo
echo "---- E. what the default still loses: correct with both pins and not by default ----"
LOST="$(comm -23 $RUN/c1 $RUN/c3 | tr '\n' ' ')"
GAINED="$(comm -13 $RUN/c1 $RUN/c3 | tr '\n' ' ')"
echo "     PREDICTED lost: v7.2 (v8.1 repaired by #1627)   measured lost: ${LOST:-none}   gained: ${GAINED:-none}"
[ "$LOST" = "v7.2 " ]; note $? "exactly v7.2 regresses against the pinned run (the old default)"
grep -qx v8.1 $RUN/c3; note $? "v8.1 is correct by default (#1627's takeout repair holds)"
grep -E '^I1618 DART r22-dev v(7\.2|8\.1) ' $RUN/i1618-r22-dev.txt | cut -c1-260 | sed 's/^/     /'

echo
echo "---- F. an unseeked window is untouched ----"
rm -rf $RUN/cache $RUN/debug_frames
( cd $RUN && env OD_MAX_CYCLES=1 OD_SEEK_VIDEO=off timeout 600 $BIN \
    --cams "$R22/cam_1.mp4,$R22/cam_2.mp4,$R22/cam_3.mp4" --width 1280 --height 720 \
    > $RUN/r22-open-align.out 2>&1 ); RC4=$?
[ "$RC4" = 0 ] && ! grep -q 'SEEK ALIGN' $RUN/r22-open-align.out
note $? "OD_SEEK_VIDEO=off under the default alignment: nothing is read forward (rc $RC4)"

echo
if [ "$FAIL" -gt 0 ]; then echo "RESULT: $FAIL failure(s)"; exit 1; fi
echo "RESULT: rig-20260922 dev lost darts under the 31-look budget unaligned because the dev seek runs its cameras 0/6/11 frames apart and a voting camera 1 calls each throw twice; the default (aligned) reads ${N3}/23, and against both pins it loses only v7.2"
exit 0
