set -u
# #1442: twenty is a ceiling as well as a floor, and the numbers that say so.
#
# The repair is one comparison -- `wiresDetected == kWiresRequired` where three files each
# asked a store bounded at twenty whether it was short -- and a comparison is exactly the
# kind of thing a later slice loosens back without anything going red. #1437's fixture
# tester cannot see it: it asks whether a clip ANSWERS in the calibration window, and
# before this issue an over-counting frame answered. #1441's tester cannot see it either:
# it holds each clip's refusals against the same binary's own `doubles` run, and the region
# is not what moves here.
#
#   A  THE PURE HALF, four ways. The count, both guards and a dart, with no footage: a
#      calibration filled the way processWires fills one, twenty-two proposed and twenty
#      stored. It is run in each mode saying truly what it expects, once with a typo'd
#      value that must read two-sided, and once MISMATCHED -- which must FAIL. #1340's
#      rule: a switch that only ever refuses passes any test that asks it to refuse, and a
#      check never shown to fail proves nothing.
#
#   B  THE CENSUS, re-taken. Both fixtures, all six clips, #1437's fifteen-frame band,
#      one binary, the two conditions being OD_WIRE_COUNT and nothing else. The three
#      claims are asserted as RELATIONSHIPS over the frames rather than as literals, so
#      they go on being true of a fixture somebody re-shoots:
#
#        a frame proposing MORE than twenty is accepted under the old test and refused
#        under the new one -- this is the population the issue is about;
#
#        a frame proposing EXACTLY twenty is accepted under BOTH -- the regression this
#        slice could most easily cause, asserted over every such frame of every clip;
#
#        a frame proposing FEWER than twenty is refused under both -- #1317's half, which
#        this must not have moved.
#
#      And the reading itself must be identical between the conditions: same `wires`, same
#      `kept`, every frame. The switch changes the VERDICT, not what was seen. Without that
#      the two censuses could be about two different populations and the arithmetic above
#      would still come out.
#
#   C  THE POPULATION IS NOT EMPTY. If no frame of either fixture proposes more than
#      twenty, every assertion in B is vacuously true and this tester is green having
#      measured nothing. That is a real possible outcome -- #1441 asked whether its region
#      split made the case unreachable -- so it is announced as a FAILURE OF THIS TESTER
#      rather than passed over, because it means the measurement, not the repair, has
#      stopped being about anything.
#
# The per-clip table is printed whether or not anything fails: the issue asks for the six
# clips before and after, and a number nobody printed is a claim.
#
# No detector process is started and so nothing needs bounding by a pid or a timeout: this
# calls the calibration stage directly, through #1437's own census program (#895).
BIN=/run1442/wire_census
UNIT=/run1442/count_check
BAND="${BAND:-30 60 90 120 150 180 210 240 270 300 330 360 390 420 450}"
FAILED=0
say() { echo "$1"; [ "$2" = ok ] || FAILED=1; }

echo "=== building: the census program, and the pure check ==="
g++ -std=c++17 -O1 -I /app/src -I /app/src/utils -I /app/src/detector/geometry/calibration \
  -o "$BIN" /app/testers/i1437_wire_census.cpp \
  /app/src/detector/geometry/calibration/*.cpp $(pkg-config --cflags --libs opencv4) \
  > /run1442/build_census.log 2>&1 || { echo "FAIL could not build the wire census; nothing below measures anything"; exit 2; }
g++ -std=c++17 -O1 -o "$UNIT" /app/testers/i1442_count_check.cpp \
  /app/src/detector/geometry/calibration/wire_processing.cpp \
  /app/src/detector/geometry/calibration/perspective_processing.cpp \
  /app/src/detector/geometry/detection/score_processing.cpp \
  -I/app/src -I/app/src/utils \
  -I/app/src/detector/geometry/calibration -I/app/src/detector/geometry/detection \
  $(pkg-config --cflags --libs opencv4) -lpthread \
  > /run1442/build_unit.log 2>&1 || { echo "FAIL could not build the pure check; nothing below measures anything"; exit 2; }

echo
echo "=== A. the count, both guards and a dart -- four ways ==="
"$UNIT" two-sided > /run1442/unit_two_sided.txt 2>&1
A1=$?
OD_WIRE_COUNT=atleast "$UNIT" at-least > /run1442/unit_at_least.txt 2>&1
A2=$?
OD_WIRE_COUNT=banana "$UNIT" two-sided > /run1442/unit_typo.txt 2>&1
A3=$?
OD_WIRE_COUNT=atleast "$UNIT" two-sided > /run1442/unit_mismatch.txt 2>&1
A4=$?
sed 's/^/  /' /run1442/unit_two_sided.txt
echo "  --- and with OD_WIRE_COUNT=atleast, the lines that move ---"
grep -E 'read .yes|whole=yes|intersections=yes|different number' /run1442/unit_at_least.txt | sed 's/^/  /'
echo "  two-sided=$A1  atleast=$A2  a typo'd value=$A3  MISMATCHED=$A4 (this one must be non-zero)"
if [ "$A1" != 0 ] || [ "$A2" != 0 ]; then
  say "FAIL the pure check does not hold in one of its two modes (two-sided=$A1, atleast=$A2)" no
elif [ "$A3" != 0 ]; then
  say "FAIL OD_WIRE_COUNT=banana was obeyed as something; a value naming no test must be ignored" no
elif [ "$A4" = 0 ]; then
  say "FAIL the check passes when told to expect the wrong mode, so it cannot fail and proves nothing" no
else
  say "OK   both modes hold, a typo reads two-sided, and the check really fails when the mode is not what it was told" ok
fi

# ---- the census -------------------------------------------------------------------
# The fixtures and their clips, from the directory rather than from a literal (#1437).
clips_of() { [ "$1" = mocks ] && ls /app/mocks/cam_*.mp4 2>/dev/null | sort || ls "/app/mocks/$1"/cam_*.mp4 2>/dev/null | sort; }
FIXTURES="$(ls -d /app/mocks/*/ 2>/dev/null | sed 's#/app/mocks/##;s#/$##') mocks"

measure() { env $2 "$BIN" "$1" $BAND 2>/dev/null | grep '^I1437' > "$3"; }
# frame, what the ensemble proposed, what was kept, and the verdict -- one line per frame
# that reached the wire stage at all. A NO-FRAME line carries none of these and is dropped
# here rather than counted as a refusal.
# Sorted on the frame, because `join` collates rather than counts and the band is decimal.
triples() { sed -n 's/^I1437 .*frame=\([0-9]*\) .*wires=\([0-9]*\) kept=\([0-9]*\) wires_ok=\([01]\).*/\1 \2 \3 \4/p' "$1" | sort -k1,1; }

echo
echo "=== B. both fixtures, all six clips, #1437's fifteen-frame band ==="
printf "  %-28s %6s %6s %6s %8s %8s\n" clip ">20" "==20" "<20" "ok:old" "ok:new"
TOT_OVER=0; TOT_EXACT=0; TOT_UNDER=0; TOT_OK_OLD=0; TOT_OK_NEW=0; TOT_FRAMES=0
BROKE=""
for f in $FIXTURES; do
  for c in $(clips_of "$f"); do
    t="$(basename "$c" .mp4)_$f"
    measure "$c" "OD_WIRE_COUNT=atleast" "/run1442/old_$t.txt"
    measure "$c" "X=x" "/run1442/new_$t.txt"
    triples "/run1442/old_$t.txt" > "/run1442/old_$t.tri"
    triples "/run1442/new_$t.txt" > "/run1442/new_$t.tri"
    # frame wiresOld keptOld okOld wiresNew keptNew okNew
    join "/run1442/old_$t.tri" "/run1442/new_$t.tri" > "/run1442/join_$t.txt"

    STATS="$(awk -v req=20 '
      { n++
        if      ($2 >  req) over++
        else if ($2 == req) exact++
        else                under++
        okold += $4; oknew += $7 }
      END { printf "%d %d %d %d %d %d\n", over+0, exact+0, under+0, okold+0, oknew+0, n+0 }
    ' "/run1442/join_$t.txt")"
    set -- $STATS
    OVER="$1"; EXACT="$2"; UNDER="$3"; OKOLD="$4"; OKNEW="$5"; N="$6"
    MOVED="$(awk '($2 != $5 || $3 != $6) { printf " frame%s(%s/%s vs %s/%s)", $1, $2, $3, $5, $6 }' "/run1442/join_$t.txt")"

    printf "  %-28s %6s %6s %6s %8s %8s\n" "$f/$(basename "$c")" "$OVER" "$EXACT" "$UNDER" "$OKOLD" "$OKNEW"
    TOT_OVER=$((TOT_OVER + OVER)); TOT_EXACT=$((TOT_EXACT + EXACT)); TOT_UNDER=$((TOT_UNDER + UNDER))
    TOT_OK_OLD=$((TOT_OK_OLD + OKOLD)); TOT_OK_NEW=$((TOT_OK_NEW + OKNEW)); TOT_FRAMES=$((TOT_FRAMES + N))
    [ -z "${MOVED:-}" ] || BROKE="$BROKE $f/$(basename "$c"):$MOVED"
  done
done
printf "  %-28s %6s %6s %6s %8s %8s\n" "ALL SIX" "$TOT_OVER" "$TOT_EXACT" "$TOT_UNDER" "$TOT_OK_OLD" "$TOT_OK_NEW"
echo "  $TOT_FRAMES frames reached the wire stage; ok:old is the one-sided test, ok:new is this slice's"

echo
if [ -n "$BROKE" ]; then
  say "FAIL the switch changed what was SEEN and not only what was decided, so the two censuses are not about one population:$BROKE" no
else
  say "OK   every frame proposes the same count and keeps the same endpoints in both conditions: the switch moves the verdict and nothing else" ok
fi

# The three relationships, over the whole population. The awk above has already checked
# each frame against the rule for its own class and put any offender in the same string,
# so this reads the same list; what is separated out is only the SENTENCE, because a
# reader meeting a red needs to know which of the three moved.
OFFENDERS="$(for f in $FIXTURES; do for c in $(clips_of "$f"); do
  t="$(basename "$c" .mp4)_$f"
  awk -v req=20 -v clip="$f/$(basename "$c")" '
    ($2 >  req) && ($4 != 1 || $7 != 0) { print "  over-counting frame not caught: " clip " frame" $1 " proposed " $2 ", old=" $4 " new=" $7 }
    ($2 == req) && ($4 != 1 || $7 != 1) { print "  REGRESSION: " clip " frame" $1 " proposed exactly " $2 " and was refused (old=" $4 " new=" $7 ")" }
    ($2 <  req) && ($4 != 0 || $7 != 0) { print "  under-counting frame not caught: " clip " frame" $1 " proposed " $2 ", old=" $4 " new=" $7 }
  ' "/run1442/join_$t.txt"
done; done)"
if [ -n "$OFFENDERS" ]; then
  echo "$OFFENDERS"
  say "FAIL the three relationships do not hold over every frame; the offenders are listed above" no
else
  say "OK   every >20 frame was accepted before and is refused now; every ==20 frame passes in BOTH; every <20 frame is refused in both" ok
fi

echo
echo "=== C. the population this issue is about is not empty ==="
echo "  frames proposing more than twenty: $TOT_OVER of $TOT_FRAMES"
if [ "$TOT_FRAMES" = 0 ]; then
  say "FAIL no frame of either fixture reached the wire stage at all, so nothing above was asked" no
elif [ "$TOT_OVER" = 0 ]; then
  say "FAIL no frame of either fixture proposes more than twenty, so every claim in B is vacuous -- this is #1441's 'the case became unreachable' outcome and it must be read, not passed" no
elif [ "$TOT_EXACT" = 0 ]; then
  say "FAIL no frame proposes exactly twenty, so the regression guard in B had nothing to guard" no
else
  say "OK   $TOT_OVER frames over-count and $TOT_EXACT propose exactly twenty, so both halves of B were really asked" ok
fi

echo
echo "=== the verdict ==="
if [ "$FAILED" = 0 ]; then echo "1442-count: PASS"; else echo "1442-count: FAIL"; fi
exit $FAILED
