set -u
# #1441: the wire stage's region is its own, and the numbers that say so.
#
# The repair is one constant -- how much of the FITTED doubles ellipse this stage reads
# inside -- and a constant is exactly the kind of thing a later slice moves back without
# anything going red. #1437's fixture tester, which this one sits beside, fails when a
# clip answers NOWHERE in the calibration window; it cannot see mocks/rig-20260918/cam_2
# going from 15 answers to 7, which is the whole of this issue.
#
#   A  THE REPAIR, both halves. rig/cam_2 answers at every frame of #1437's window AND
#      the board it answers about is the repaired one. Either alone is passable by a
#      defect: #1378's collapse gave this clip twenty wires at every frame by fitting the
#      TREBLE ring, so a wire count with no board beside it would call that a pass.
#
#   B  THE MUTATION PROOF. OD_WIRE_REGION=doubles puts the pre-#1441 region back on this
#      same binary, and the refusals must come back with it -- at the number they really
#      were, 8 of 15, and not merely "more than none". #1340's rule: a switch that only
#      ever refuses passes any test that asks it to refuse. A is worth nothing without
#      this, because a census that has never been shown to fail is a census nobody can
#      trust. (#1729: true of the counting stage. Since #1467 the model decides this
#      clip's count and the switch moves it by none; B is re-pinned to that and D's 1.30
#      carries the proof. The paragraph at B says what was measured.)
#
#   C  THE OTHER FIVE CLIPS DO NOT MOVE. Asserted against the same binary's own `doubles`
#      run rather than against five literals, so it goes on being true of a fixture
#      somebody re-shoots. This is the half that would catch a constant chosen for one
#      camera. (#1730: on the twenty-fold model two cameras answer a region with a
#      bimodal fit rather than a count, and are held to a floor instead of to the
#      doubles run's number. The paragraph at C says what was measured.)
#
#   D  THE SWITCH IS OBEYED AND IS REFUSED, and both are measured. A region past the
#      board's rim names nothing on any board and is ignored rather than clamped; a
#      region inside it is obeyed even when what it does is terrible. 1.30 and 1.34 are
#      four hundredths apart and answer 15 and 0.
#
# No detector process is started here and so nothing needs bounding: this measures the
# calibration stage directly, through #1437's own census program.
BIN=/run1441/wire_census
BAND="${BAND:-30 60 90 120 150 180 210 240 270 300 330 360 390 420 450}"
FAILED=0
say() { echo "$1"; [ "$2" = ok ] || FAILED=1; }

g++ -std=c++17 -O1 -I /app/src -I /app/src/utils -I /app/src/detector/geometry/calibration \
  -o "$BIN" /app/testers/i1437_wire_census.cpp \
  /app/src/detector/geometry/calibration/*.cpp $(pkg-config --cflags --libs opencv4) \
  > /run1441/build.log 2>&1 || { echo "FAIL could not build the wire census; nothing below measures anything"; exit 2; }

# The fixtures and their clips, from the directory rather than from a literal (#1437).
# #1478: the shipped mocks (`mocks`, the clips at the top of /app/mocks) left this
# population; it is the rig directories alone (docs/shipped-mock-census.md, A2).
clips_of() { ls "/app/mocks/$1"/cam_*.mp4 2>/dev/null | sort; }
FIXTURES="$(ls -d /app/mocks/*/ 2>/dev/null | sed 's#/app/mocks/##;s#/$##')"
SUBJECT=/app/mocks/rig-20260918/cam_2.mp4

measure() { env $2 "$BIN" "$1" $BAND 2>/dev/null | grep '^I1437' > "$3"; }
# #1445: counted from the WIRE NUMBER rather than from the verdict, and #1442 is the
# reason rather than a preference.
#
# `wires_ok` became TWO-SIDED in that slice, so a frame proposing twenty-two now reads
# `wires_ok=0` here. This tester's subject is a REGION, and what a wrong region does is
# COLLAPSE the count -- #1378's twenty wires found on the treble ring, #1317's nine found
# on a partial ring, and the 1.30 region in D below, which answers 6, 14, 14, 16, 15.
# An over-count is the opposite fault: a camera reading structure the board does not have,
# answered by what else is in its picture, and it is #1442's subject. Folding it in here
# turns this tester red for a thing it cannot name and its reader cannot act on, which is
# exactly what happened -- mocks/cam_1 went 6 -> 7 on a frame that moved from 20 to 21.
#
# MEASURED, on the run that made this change, and it is what says this is a NARROWING of
# the population rather than a loosening of the test: every one of the 8 refusals phase B
# asserts is an under-count (18, 18, 18, 18, 19, 17, 17, 19), every one of D's 15 is
# (6, 14, 14, 16, 15, 16, 14, 14, 15, 14, 15, 14, 14, 15, 12), and phase C's five clips
# agree before and after. So every literal in this file is the number it always was, and
# the only line that moves is the one #1442 predicted would.
#
# The over-counts are PRINTED beside the under-counts rather than dropped in silence
# (#923's rule: an exclusion is a number in the report, so a check quietly growing or
# shrinking is visible). What they mean is #1442's tester's business, and it asserts them
# over this same population.
refused()   { sed -n 's/.*wires=\([0-9]*\) kept.*/\1/p' "$1" | awk -v req=20 '$1 < req' | wc -l | tr -d ' '; }
overcount() { sed -n 's/.*wires=\([0-9]*\) kept.*/\1/p' "$1" | awk -v req=20 '$1 > req' | wc -l | tr -d ' '; }
counted() { grep -c '^I1437' "$1" || true; }
# The smallest fitted board over the frames this clip ANSWERED for. #1378's collapse is
# 91,849 px against 258,582, so the floor is nowhere near either and needs no tuning.
smallest_board() { sed -n 's/.*wires_ok=1 .*board=\([0-9]*\).*/\1/p' "$1" | sort -n | head -1; }

echo "=== A. the clip this issue is about answers at every frame, about a repaired board ==="
measure "$SUBJECT" "X=x" /run1441/after_subject.txt
R="$(refused /run1441/after_subject.txt)"; N="$(counted /run1441/after_subject.txt)"
B="$(smallest_board /run1441/after_subject.txt)"
echo "  rig-20260918/cam_2: refused $R of $N by under-count (and $(overcount /run1441/after_subject.txt) of $N read MORE than twenty, which is #1442's), smallest fitted board it answered about: ${B:-none} px"
if [ "$N" = 0 ]; then
  say "FAIL the census measured nothing at all, so nothing here was asked" no
elif [ "$R" != 0 ]; then
  say "FAIL rig-20260918/cam_2 refuses $R of its $N frames; #1441 is the slice that took that to 0" no
elif [ -z "$B" ] || [ "$B" -lt 200000 ]; then
  say "FAIL it answers, but about a board of ${B:-no} px -- #1378's collapse is 91,849 px against 258,582, so this is twenty wires found on the treble ring" no
else
  say "OK   rig-20260918/cam_2 answers for all $N frames, about a board of at least $B px" ok
fi

echo
echo "=== B. the same binary with the pre-#1441 region put back ==="
# #1729: RE-MEASURED, because the count stopped being the region's to decide. #1441's
# eight refusals were the COUNTING stage's: bisected, the parent of #1467's merge
# (9d699de) refuses exactly 18,18,18,18,19,17,17,19 here with OD_WIRE_REGION=doubles,
# and the merge itself (2bcb9c3, the twenty-fold wire model) refuses none -- the model
# FITS twenty boundaries to the candidates the region admits and generates the ring from
# the fit, so a trusted fit is twenty whatever the region. On 61f9bcb both regions answer
# 20 at all fifteen frames of this clip. That is #1467's design, not a defect, so the
# pinned figure is now 0 and B asserts it: a region that decides this clip's count again
# is a change somebody should see.
#
# What B used to prove -- that A can fail on this binary -- is now D's 1.30, which the
# model DOES feel (5 of 15 refused on 61f9bcb). A without D is still worth nothing.
measure "$SUBJECT" "OD_WIRE_REGION=doubles" /run1441/before_subject.txt
RB="$(refused /run1441/before_subject.txt)"; NB="$(counted /run1441/before_subject.txt)"
WAS=0
echo "  rig-20260918/cam_2 with OD_WIRE_REGION=doubles: refused $RB of $NB by under-count (and $(overcount /run1441/before_subject.txt) over-count)"
if [ "$NB" = 0 ]; then
  say "FAIL the census measured nothing with the switch thrown" no
elif [ "$RB" = "$WAS" ]; then
  say "OK   the pre-#1441 region refuses $WAS of $NB here too: since #1467 the fitted model, not the region, decides this clip's count" ok
else
  say "FAIL it refused $RB of $NB where the model refused $WAS on 61f9bcb; the region decides this clip's count again, and this tester cannot say which half moved" no
fi

echo
echo "=== C. every other clip of every fixture answers exactly as it did ==="
# #1730: TWO CLIPS ARE NAMED HERE, and what was measured is why. On the counting stage a
# region moved a COUNT, and a count that moved between two regions was the region. Since
# #1467 (2bcb9c3) a refusal is a FIT that missed the coherence minimum (0.6), and on these
# two cameras the fit is bimodal: a frame reads R ~0.85-0.93 or R ~0.25-0.42 and almost
# nothing between, and which of the two it reads flips with a handful of candidates --
# rig-20260922/cam_3 frame 60 is R=0.33 at 0.9076 and 0.91 at 1.00; rig-20260929/cam_3
# frame 420 is 0.30 and 0.88. So the refusal count is the number of frames that tipped,
# and it is not monotone in the region. Swept on 820cfd1, refusals of 15:
#
#   scale    0.84 0.88 0.9076 0.94 0.96 0.98 1.00 1.02 1.05 1.10
#   r22/c3     5    6    5      4    3    6    3    4    4    4
#   r29/c3     4    5    5      5    4    4    2    2    4    4
#
# against r18/c1, r18/c3, r22/c1 (3), r22/c2 (4), r29/c1 and r29/c2, which answer the
# same at every scale from 0.84 to 1.05. The doubles region's fewer refusals are NOT the
# better region: the 1555 bakeoff with OD_WIRE_REGION=doubles (capture clock) took
# rig-20260922 from 21/23 to 13/23 in the dev window and 23/23 to 17/23 in the opening,
# every new miss a wedge, and rig-20260929's opening from 27..31 to 26..30; rig-20260918
# row for row the same, phantoms 0. A refused frame is retried, so the default's extra
# refusals cost calibration time (rig-0922 dev: 46 fits to the doubles region's 4) and no
# dart.
#
# So the two are held to what the model guarantees of a camera that calibrates -- most of
# its window answers: at least BIMODAL_FLOOR of the frames, where the worst row of the
# sweep is 6 refused of 15 -- and every OTHER clip is held to the exact equality this
# phase always asked. A clip not named here that moves is still a FAIL, so this does not
# widen: if a third camera turns bimodal, it is a change somebody has to look at.
BIMODAL="rig-20260922/cam_3.mp4 rig-20260929/cam_3.mp4"
BIMODAL_FLOOR=8
MOVED=""
SHORT=""
for f in $FIXTURES; do
  for c in $(clips_of "$f"); do
    [ "$c" = "$SUBJECT" ] && continue
    t="$(basename "$c" .mp4)_$f"
    measure "$c" "X=x" "/run1441/after_$t.txt"
    measure "$c" "OD_WIRE_REGION=doubles" "/run1441/before_$t.txt"
    A="$(refused "/run1441/after_$t.txt")"; P="$(refused "/run1441/before_$t.txt")"
    T="$(counted "/run1441/after_$t.txt")"
    name="$f/$(basename "$c")"
    case " $BIMODAL " in
      *" $name "*)
        echo "  $f $(basename "$c"): refused $P of $T before, $A of $T after -- a bimodal fit (#1730), held to answering at least $BIMODAL_FLOOR of $T"
        [ "$T" -gt 0 ] && [ $(( T - A )) -ge "$BIMODAL_FLOOR" ] || SHORT="$SHORT $name (answered $(( T - A )) of $T)"
        ;;
      *)
        echo "  $f $(basename "$c"): refused $P of $T before, $A of $T after (over-counts, not the subject: $(overcount "/run1441/before_$t.txt") -> $(overcount "/run1441/after_$t.txt"))"
        [ "$A" = "$P" ] || MOVED="$MOVED $name ($P->$A)"
        ;;
    esac
  done
done
if [ -n "$MOVED" ]; then
  say "FAIL these clips answer differently with the region than without it, and this slice is about one of them:$MOVED" no
elif [ -n "$SHORT" ]; then
  say "FAIL a bimodal camera answers fewer than $BIMODAL_FLOOR frames of its window:$SHORT" no
else
  say "OK   the region moved one clip's answer and no other; the two bimodal cameras each answer at least $BIMODAL_FLOOR frames" ok
fi

echo
echo "=== D. the switch is obeyed inside the board and ignored past its rim ==="
# 1.33 of the fitted doubles ellipse is the board's own rim (225.5/170), and there is
# nothing outside it for a stage that puts every endpoint it returns on the doubles ring.
measure "$SUBJECT" "OD_WIRE_REGION_MARGIN=1.30" /run1441/d_inside.txt
measure "$SUBJECT" "OD_WIRE_REGION_MARGIN=1.34" /run1441/d_past.txt
measure "$SUBJECT" "OD_WIRE_REGION_MARGIN=banana" /run1441/d_word.txt
DI="$(refused /run1441/d_inside.txt)"; DP="$(refused /run1441/d_past.txt)"; DW="$(refused /run1441/d_word.txt)"
echo "  1.30 (inside the rim): refused $DI    1.34 (past it): refused $DP    a word: refused $DW    default: refused $R"
if [ "$DI" = "$R" ]; then
  say "FAIL a region of 1.30 answers exactly as the default does, so nothing here shows the switch is read at all -- and since #1729 this is the half that proves A can fail" no
elif [ "$DP" != "$R" ] || [ "$DW" != "$R" ]; then
  say "FAIL a value naming no region was obeyed: 1.34 gave $DP and a word gave $DW where the default gives $R" no
else
  say "OK   1.30 is obeyed and moves the count to $DI; 1.34 and a word name no region and are ignored rather than clamped" ok
fi

echo
echo "=== the verdict ==="
if [ "$FAILED" = 0 ]; then echo "1441-region: PASS"; else echo "1441-region: FAIL"; fi
exit $FAILED
