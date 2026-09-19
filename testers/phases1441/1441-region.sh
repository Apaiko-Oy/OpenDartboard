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
#      trust.
#
#   C  THE OTHER FIVE CLIPS DO NOT MOVE. Asserted against the same binary's own `doubles`
#      run rather than against five literals, so it goes on being true of a fixture
#      somebody re-shoots. This is the half that would catch a constant chosen for one
#      camera.
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
clips_of() { [ "$1" = mocks ] && ls /app/mocks/cam_*.mp4 2>/dev/null | sort || ls "/app/mocks/$1"/cam_*.mp4 2>/dev/null | sort; }
FIXTURES="$(ls -d /app/mocks/*/ 2>/dev/null | sed 's#/app/mocks/##;s#/$##') mocks"
SUBJECT=/app/mocks/rig-20260918/cam_2.mp4

measure() { env $2 "$BIN" "$1" $BAND 2>/dev/null | grep '^I1437' > "$3"; }
refused() { grep -c 'wires_ok=0' "$1" || true; }
counted() { grep -c '^I1437' "$1" || true; }
# The smallest fitted board over the frames this clip ANSWERED for. #1378's collapse is
# 91,849 px against 258,582, so the floor is nowhere near either and needs no tuning.
smallest_board() { sed -n 's/.*wires_ok=1 .*board=\([0-9]*\).*/\1/p' "$1" | sort -n | head -1; }

echo "=== A. the clip this issue is about answers at every frame, about a repaired board ==="
measure "$SUBJECT" "X=x" /run1441/after_subject.txt
R="$(refused /run1441/after_subject.txt)"; N="$(counted /run1441/after_subject.txt)"
B="$(smallest_board /run1441/after_subject.txt)"
echo "  rig-20260918/cam_2: refused $R of $N, smallest fitted board it answered about: ${B:-none} px"
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
measure "$SUBJECT" "OD_WIRE_REGION=doubles" /run1441/before_subject.txt
RB="$(refused /run1441/before_subject.txt)"; NB="$(counted /run1441/before_subject.txt)"
WAS=8
echo "  rig-20260918/cam_2 with OD_WIRE_REGION=doubles: refused $RB of $NB"
if [ "$RB" = "$WAS" ]; then
  say "OK   the region before this issue is caught, at the $WAS of $NB it really refused" ok
elif [ "$RB" = 0 ]; then
  say "FAIL the switch changed nothing, so A above is measuring one region twice and cannot fail" no
else
  say "FAIL it refused $RB of $NB where the pre-#1441 region refused $WAS; one of the two regions has moved and this tester cannot say which" no
fi

echo
echo "=== C. every other clip of every fixture answers exactly as it did ==="
MOVED=""
for f in $FIXTURES; do
  for c in $(clips_of "$f"); do
    [ "$c" = "$SUBJECT" ] && continue
    t="$(basename "$c" .mp4)_$f"
    measure "$c" "X=x" "/run1441/after_$t.txt"
    measure "$c" "OD_WIRE_REGION=doubles" "/run1441/before_$t.txt"
    A="$(refused "/run1441/after_$t.txt")"; P="$(refused "/run1441/before_$t.txt")"
    T="$(counted "/run1441/after_$t.txt")"
    echo "  $f $(basename "$c"): refused $P of $T before, $A of $T after"
    [ "$A" = "$P" ] || MOVED="$MOVED $f/$(basename "$c") ($P->$A)"
  done
done
if [ -z "$MOVED" ]; then
  say "OK   the region moved one clip's answer and no other" ok
else
  say "FAIL these clips answer differently with the region than without it, and this slice is about one of them:$MOVED" no
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
  say "FAIL a region of 1.30 answers exactly as the default does, so nothing here shows the switch is read at all" no
elif [ "$DP" != "$R" ] || [ "$DW" != "$R" ]; then
  say "FAIL a value naming no region was obeyed: 1.34 gave $DP and a word gave $DW where the default gives $R" no
else
  say "OK   1.30 is obeyed and moves the count to $DI; 1.34 and a word name no region and are ignored rather than clamped" ok
fi

echo
echo "=== the verdict ==="
if [ "$FAILED" = 0 ]; then echo "1441-region: PASS"; else echo "1441-region: FAIL"; fi
exit $FAILED
