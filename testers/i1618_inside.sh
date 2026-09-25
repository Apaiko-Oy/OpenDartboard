set -u
# #1618, inside the container: why a correctly calibrated third camera made rig-20260922 dev
# WORSE (15/23 -> 11/23 under #1605's OD_LOOK_BUDGET=1605), and the pin that shows it.
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
# own axes are as good as the opening window's. OD_SEEK_ALIGN=1618 reads the lagging
# files forward once calibration is done (scorer.cpp, seekAlignIsOn); a release build and
# a live rig never seek at all.
#
# WHAT IS ASSERTED, predictions first in the output:
#   A  THE CAUSE: the dev seek staggers the three files; the default and #1605's opt-in
#      are not aligned (no SEEK ALIGN line), so this pin changes neither.
#   B  THE MECHANISM, under OD_LOOK_BUDGET=1605: echo windows, each closer behind the
#      previous call than the 11-frame skew plus one cycle; none on the default run.
#   C  CAMERA 1 IS NOT THE FAULT: its valid axes in that run sit a median <= 1.0 deg and
#      <= 6 px (annotated tip off the axis) from the hand annotations.
#   D  THE REPAIR, OD_LOOK_BUDGET=1605 OD_SEEK_ALIGN=1618: the files are read forward by
#      exactly the seek differences, no echo is left, rig-22 dev reads ABOVE 15/23, and
#      every dart the account attributes to the skew is correct again.
#   E  WHY IT IS STILL A PIN: the darts correct on the default run and not on the aligned
#      one are exactly v7.2 and v8.1, neither of which is an echo (a lone camera-1 reading
#      past the 3/19 wire; a takeout whose event is abandoned). #1618's bar is none.
#   F  AN UNSEEKED WINDOW IS UNTOUCHED: at the clip's opening nothing is read forward.
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
replay r22-dev;                                          RC1=$?
replay r22-dev-1605       OD_LOOK_BUDGET=1605;           RC2=$?
replay r22-dev-1605-align OD_LOOK_BUDGET=1605 OD_SEEK_ALIGN=1618; RC3=$?
[ "$RC1$RC2$RC3" = 000 ]; note $? "all three replays ended cleanly (rc $RC1 $RC2 $RC3)"
for r in r22-dev r22-dev-1605 r22-dev-1605-align; do
  echo "     $r: $(grep '^I1555 ACCURACY ' $RUN/census-$r.txt | sed 's/^I1555 ACCURACY //')"
done

echo
echo "---- A. the cause: the dev seek staggers the files ----"
SEEKS="$(grep -oE 'video [0-9] seeked forward by [0-9.]+ seconds \(frame [0-9]+\)' $RUN/r22-dev.txt | sed -E 's/video ([0-9]).*frame ([0-9]+).*/\1:\2/' | tr '\n' ' ')"
echo "     PREDICTED: frames 90, 84 and 79.  measured: $SEEKS"
[ "$SEEKS" = "1:90 2:84 3:79 " ]; note $? "the three file cameras start 0, 6 and 11 frames apart"
! grep -q 'SEEK ALIGN' $RUN/r22-dev.txt; note $? "the default run is not aligned"
! grep -q 'SEEK ALIGN' $RUN/r22-dev-1605.txt; note $? "#1605's opt-in on its own is not aligned"

echo
echo "---- B. the mechanism: echo windows once camera 1 votes ----"
grep -E '^I1618 (ECHO|ECHOES) ' $RUN/i1618-r22-dev-1605.txt | sed 's/^/     /'
E1="$(sed -n 's/^I1618 ECHOES r22-dev n=\([0-9]*\).*/\1/p' $RUN/i1618-r22-dev.txt)"
E2="$(sed -n 's/^I1618 ECHOES r22-dev-1605 n=\([0-9]*\).*/\1/p' $RUN/i1618-r22-dev-1605.txt)"
G2="$(sed -n 's/^I1618 ECHOES r22-dev-1605 .*max_gap=\([0-9]*\).*/\1/p' $RUN/i1618-r22-dev-1605.txt)"
echo "     PREDICTED: three or more echoes under the opt-in, each within 12 cycles (the 11-frame skew + 1); none by default"
[ "${E2:-0}" -ge 3 ] && [ -n "$G2" ] && [ "$G2" -le 12 ]
note $? "OD_LOOK_BUDGET=1605: ${E2:-0} echo window(s), the widest ${G2:-none} cycles behind the call before it"
[ "${E1:-x}" = 0 ]; note $? "default: ${E1:-?} echo windows"

echo
echo "---- C. camera 1's own evidence under the opt-in ----"
C1="$(grep '^I1618 CAM1 ' $RUN/i1618-r22-dev-1605.txt)"
echo "     $C1"
MA="$(echo "$C1" | sed -n 's/.*median_dAng=\([0-9.]*\).*/\1/p')"
MP="$(echo "$C1" | sed -n 's/.*median_tipPerp=\([0-9.]*\).*/\1/p')"
[ -n "$MA" ] && [ -n "$MP" ] && awk -v a="$MA" -v p="$MP" 'BEGIN{exit !(a <= 1.0 && p <= 6.0)}'
note $? "camera 1's valid axes: median ${MA:-?} deg and ${MP:-?} px from the annotations (<= 1.0 deg, <= 6 px)"

echo
echo "---- D. the pin: OD_LOOK_BUDGET=1605 OD_SEEK_ALIGN=1618 ----"
AL="$(grep -oE 'SEEK ALIGN: video 2 read forward [0-9]+ of [0-9]+ frame\(s\), video 3 read forward [0-9]+ of [0-9]+ frame\(s\)' $RUN/r22-dev-1605-align.txt)"
echo "     PREDICTED: video 2 read forward 6, video 3 read forward 11; no echo; above 15/23"
echo "     measured:  ${AL:-no SEEK ALIGN line}"
[ "$AL" = "SEEK ALIGN: video 2 read forward 6 of 6 frame(s), video 3 read forward 11 of 11 frame(s)" ]
note $? "the lagging files are read forward by exactly the seek differences"
E3="$(sed -n 's/^I1618 ECHOES r22-dev-1605-align n=\([0-9]*\).*/\1/p' $RUN/i1618-r22-dev-1605-align.txt)"
[ "${E3:-x}" = 0 ]; note $? "aligned: ${E3:-?} echo windows"
N1="$(correct_n $RUN/census-r22-dev.txt)"; N2="$(correct_n $RUN/census-r22-dev-1605.txt)"
N3="$(correct_n $RUN/census-r22-dev-1605-align.txt)"
echo "     correct: default ${N1:-?}/23, opt-in ${N2:-?}/23, opt-in aligned ${N3:-?}/23"
[ -n "$N3" ] && [ -n "$N1" ] && [ "$N3" -gt 15 ] && [ "$N3" -gt "$N1" ]
note $? "aligned, the opt-in reads above 15/23 and above the default"
correct_set $RUN/census-r22-dev-1605.txt > $RUN/c2
correct_set $RUN/census-r22-dev-1605-align.txt > $RUN/c3
correct_set $RUN/census-r22-dev.txt > $RUN/c1
SKEW_DARTS="v2.2 v2.3 v3.2 v5.2 v5.3 v7.3"
BACK=0
for d in $SKEW_DARTS; do
  in2=$(grep -cx "$d" $RUN/c2); in3=$(grep -cx "$d" $RUN/c3)
  echo "     $d: opt-in $( [ "$in2" = 1 ] && echo correct || echo wrong ) -> aligned $( [ "$in3" = 1 ] && echo correct || echo wrong )"
  [ "$in2" = 0 ] && [ "$in3" = 1 ] && BACK=$((BACK + 1))
done
[ "$BACK" = 6 ]; note $? "all six darts the account lays on the skew are wrong under the opt-in and correct aligned ($BACK of 6)"

echo
echo "---- E. why it stays a pin: what the default gets right that the aligned opt-in does not ----"
LOST="$(comm -23 $RUN/c1 $RUN/c3 | tr '\n' ' ')"
GAINED="$(comm -13 $RUN/c1 $RUN/c3 | tr '\n' ' ')"
echo "     PREDICTED lost: v7.2 v8.1   measured lost: ${LOST:-none}   gained: ${GAINED:-none}"
[ "$LOST" = "v7.2 v8.1 " ]; note $? "exactly v7.2 and v8.1 regress against the default"
grep -E '^I1618 DART r22-dev-1605-align v(7\.2|8\.1) ' $RUN/i1618-r22-dev-1605-align.txt | cut -c1-260 | sed 's/^/     /'

echo
echo "---- F. an unseeked window is untouched ----"
rm -rf $RUN/cache $RUN/debug_frames
( cd $RUN && env OD_MAX_CYCLES=1 OD_SEEK_VIDEO=off OD_SEEK_ALIGN=1618 timeout 600 $BIN \
    --cams "$R22/cam_1.mp4,$R22/cam_2.mp4,$R22/cam_3.mp4" --width 1280 --height 720 \
    > $RUN/r22-open-align.out 2>&1 ); RC4=$?
[ "$RC4" = 0 ] && ! grep -q 'SEEK ALIGN' $RUN/r22-open-align.out
note $? "OD_SEEK_VIDEO=off with the pin on: nothing is read forward (rc $RC4)"

echo
if [ "$FAIL" -gt 0 ]; then echo "RESULT: $FAIL failure(s)"; exit 1; fi
echo "RESULT: rig-20260922 dev lost darts under OD_LOOK_BUDGET=1605 because the dev seek runs its cameras 0/6/11 frames apart and a voting camera 1 calls each throw twice; aligned, the opt-in reads ${N3}/23, and it stays a pin because v7.2 and v8.1 still regress"
exit 0
