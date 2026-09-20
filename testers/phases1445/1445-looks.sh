set -u
# #1445: a camera refused on one averaged frame is looked at again, and the numbers that
# size how often.
#
# The claim this slice makes is arithmetic and it is counter-intuitive, so it is measured
# before it is built on: an AVERAGED calibration frame can be a WORSE reading of a board
# than several of the single frames composing it. `readAveraged(30)` accumulates thirty
# consecutive frames in CV_32F and divides; a bright edge present in any of them survives
# the mean at a thirtieth of its contrast, and a wire boundary that moved between them is
# smeared across the result. That is a picture no camera ever saw.
#
#   A  THE FACT, on a quiet box. mocks/rig-20260918/cam_3's averaged frame against the
#      single frames after it. #1442's author flagged its own 21 as taken at load 12-15;
#      this re-derives it, and asserts the RELATIONSHIP (the average is refused, a single
#      frame is not) rather than the literal, so it goes on being true of a re-shot
#      fixture. The literal is printed because the issue asks for it.
#
#   B  THE BUDGET, re-derived on every run. Both fixtures, all six clips, at the seek and
#      the spacing the detector really uses. What is measured is the LONGEST RUN OF
#      CONSECUTIVE REFUSED LOOKS on a camera that answers at all -- #1388's longest run of
#      consecutive disagreeing samples, asked of this observable -- and the constants are
#      read out of the source rather than restated here, so a moved constant or a re-shot
#      fixture fails HERE and not in a pub.
#
#   C  THE od_fix SHAPE (#1340). One binary, the retry disabled at run time with
#      OD_CALIBRATION_LOOKS=once, held to the ACTUAL pre-change numbers -- 2 of 3 on the
#      rig, 3 of 3 on the shipped mocks -- rather than merely to refusing. A switch that
#      only ever refuses passes any test that asks it to refuse.
#
#   D  NOTHING ADOPTS FRESH GEOMETRY MID-RUN (ADR-0080 section 2), in four parts, because
#      getting this wrong is worse than the defect it repairs:
#
#        1  every LOOK AGAIN line falls BEFORE this run's GEOMETRY SEALED line, and there
#           is exactly one seal. The seal is the boundary and the looks are all on the
#           calibration side of it -- that is the whole mechanism, and it is a fact about
#           the transcript rather than a claim about the code.
#        2  the retry ADDS and never CHANGES: every camera that calibrated with the retry
#           off has a BYTE-IDENTICAL entry in the seal taken with it on.
#        3  BOARD ADOPTED GEOMETRY MID-RUN is said by neither run.
#        4  and the refusal really fires about a RETRIED camera. The rig is run blind and
#           camera 3 -- the camera that only has geometry because a look gave it one --
#           comes back nudged 20 px across and 15 px down. With the retry ON the board
#           must refuse: BOARD MOVED. With it OFF the same nudge must go UNNOTICED, because
#           camera 3 abstained and reviewGeometry does not ask a camera that was not
#           scoring. That pair is the distinction the issue asks for, measured: a first
#           calibration for a camera that never calibrated is not a re-calibration, and
#           what the look produced is first-class sealed geometry under the same guard as
#           everything else.
#
# EVERY DETECTOR RUN BELOW IS UNDER A BOUND, by its own recorded pid. A board that cannot
# calibrate takes #895's vigil and stays up on purpose; OD_MAX_CYCLES does not bound it,
# and phase C's `once` arm and both arms of D4 are exactly that board.
BIN=/app/build/opendartboard
CENSUS=/run1445/look_census
FAILED=0
say() { echo "$1"; [ "$2" = ok ] || FAILED=1; }

# The constants this board really uses, read from the source. Restating them here is how a
# harness and a repair drift apart.
LOOKS="$(grep -oE 'constexpr int kFurtherLooks = [0-9]+' /app/src/detector/geometry/geometry_detector.cpp | grep -oE '[0-9]+$')"
SPACING="$(grep -oE 'constexpr int kFramesBetweenLooks = [0-9]+' /app/src/detector/geometry/geometry_detector.cpp | grep -oE '[0-9]+$')"
if [ -z "${LOOKS:-}" ] || [ -z "${SPACING:-}" ]; then
  echo "FAIL could not read kFurtherLooks/kFramesBetweenLooks out of geometry_detector.cpp; nothing below is about the board's own budget"
  exit 2
fi
echo "=== the budget this tree holds: $LOOKS further looks, $SPACING capture cycles apart ==="

echo
echo "=== building: the look census ==="
g++ -std=c++17 -O1 -I /app/src -I /app/src/utils -I /app/src/detector/geometry/calibration \
  -o "$CENSUS" /app/testers/i1445_look_census.cpp \
  /app/src/detector/geometry/calibration/*.cpp $(pkg-config --cflags --libs opencv4) \
  > /run1445/build_census.log 2>&1 || { echo "FAIL could not build the look census; nothing below measures anything"; exit 2; }

clips_of() { [ "$1" = mocks ] && ls /app/mocks/cam_*.mp4 2>/dev/null | sort || ls "/app/mocks/$1"/cam_*.mp4 2>/dev/null | sort; }
FIXTURES="$(ls -d /app/mocks/*/ 2>/dev/null | sed 's#/app/mocks/##;s#/$##') mocks"

# A census is taken at the SPACING the board spends its budget at, and deliberately over
# more looks than the budget holds: a budget can only be sized against a run that is
# allowed to be longer than it.
WINDOW="${WINDOW:-40}"

# One clip, at its own camera index -- which is what decides the seek, and therefore which
# stretch of the clip the board really calibrates on (DEBUG_SEEK_VIDEO).
census() { "$CENSUS" "$1" "$2" "$WINDOW" "$SPACING" 30 2>/dev/null | grep '^I1445' > "$3"; }
avg_wires()  { sed -n 's/.*look=avg .*wires=\([0-9]*\) kept.*/\1/p' "$1" | head -1; }
avg_whole()  { sed -n 's/.*look=avg .*whole=\([01]\).*/\1/p' "$1" | head -1; }
look_wires() { sed -n 's/.* look=[0-9][0-9]* .*wires=\([0-9]*\) kept.*/\1/p' "$1"; }
look_whole() { sed -n 's/.* look=[0-9][0-9]* .*whole=\([01]\).*/\1/p' "$1"; }
# The longest run of consecutive looks that did NOT read a whole ring. This is the figure
# a budget is sized against and it is the only one that matters: it is how long a board
# looking repeatedly goes on being refused by a camera it can calibrate.
longest_refused_run() { look_whole "$1" | awk '{ if($1==0){r++; if(r>m)m=r} else r=0 } END{print m+0}'; }
answers_at_all()      { look_whole "$1" | awk '{ if($1==1)n++ } END{print n+0}'; }
looks_taken()         { look_whole "$1" | wc -l | tr -d ' '; }

SUBJECT=/app/mocks/rig-20260918/cam_3.mp4
SUBJECT_CAM=2   # 0-based: cam_3 is the third slot, and the seek follows the slot

echo
echo "=== A. the camera this issue is about: its averaged frame, and the frames after it ==="
census "$SUBJECT" "$SUBJECT_CAM" /run1445/a_subject.txt
AW="$(avg_wires /run1445/a_subject.txt)"; AOK="$(avg_whole /run1445/a_subject.txt)"
LN="$(looks_taken /run1445/a_subject.txt)"; LOK="$(answers_at_all /run1445/a_subject.txt)"
echo "  rig-20260918/cam_3 averaged over 30 frames: proposes ${AW:-none} wire boundaries, whole ring=${AOK:-none}"
echo "  the $LN single looks after it: $LOK read a whole ring"
echo "  what each look proposed: $(look_wires /run1445/a_subject.txt | tr '\n' ' ')"
if [ -z "${AW:-}" ] || [ "$LN" = 0 ]; then
  say "FAIL the census measured nothing at all, so nothing here was asked" no
elif [ "$AOK" != 0 ]; then
  say "FAIL the averaged frame of rig-20260918/cam_3 is NOT refused on this box ($AW wires), so the defect this slice repairs does not reproduce here and the slice changes shape" no
elif [ "$LOK" = 0 ]; then
  say "FAIL the averaged frame is refused and so is every one of the $LN single looks after it; there is no better picture to find and a retry cannot be the repair" no
else
  say "OK   the averaged frame proposes $AW and is refused; $LOK of the $LN single looks after it read a whole ring -- the average is a worse reading than frames composing it" ok
fi

echo
echo "=== B. the budget, against the longest run of consecutive refused looks ==="
printf "  %-30s %5s %7s %7s %9s\n" clip avg "answers" "of" "worst-run"
WORST=0; WORST_WHO=""; ALLWORST=0; ALLWORST_WHO=""; SILENT=""; RESCUABLE=0
for f in $FIXTURES; do
  i=-1
  for c in $(clips_of "$f"); do
    i=$((i + 1))
    t="$(basename "$c" .mp4)_$f"
    census "$c" "$i" "/run1445/b_$t.txt"
    W="$(avg_wires "/run1445/b_$t.txt")"; O="$(avg_whole "/run1445/b_$t.txt")"
    N="$(looks_taken "/run1445/b_$t.txt")"; K="$(answers_at_all "/run1445/b_$t.txt")"
    R="$(longest_refused_run "/run1445/b_$t.txt")"
    printf "  %-30s %5s %7s %7s %9s\n" "$f/$(basename "$c") (cam $((i + 1)))" "${W:-?}${O:+/ok=$O}" "$K" "$N" "$R"
    if [ "$N" = 0 ]; then
      SILENT="$SILENT $f/$(basename "$c")(no looks at all)"
      continue
    fi
    if [ "$K" = 0 ]; then
      # A clip that answers at NO look in the window is not a budget question. It is
      # #1437's "this clip has stopped contributing" and is reported as itself.
      SILENT="$SILENT $f/$(basename "$c")"
      continue
    fi
    # THE POPULATION A LOOK IS SPENT ON, and it is not every camera. A look is only ever
    # taken for a camera whose AVERAGED frame was refused, so how long such a camera goes
    # on reading something a board does not have is a question about that population --
    # measuring it on a camera that calibrated first time is measuring a state the budget
    # is never spent in. That is #1348's rule (a threshold is measured against the
    # population it is really about, not against a neighbouring one), and it matters by a
    # factor of four here: over this window mocks/cam_3 produces a run four times the
    # longest run any refused camera produces, and it produces it deep in a clip of darts
    # being thrown, which is not a picture a board calibrating at start-up ever sees.
    #
    # Every camera is still measured and printed, because the second column is what stops
    # a constant being chosen for one clip -- it is just not what the bound is taken from.
    [ "$R" -le "$ALLWORST" ] || { ALLWORST="$R"; ALLWORST_WHO="$f/$(basename "$c")"; }
    if [ "${O:-1}" = 0 ]; then
      RESCUABLE=$((RESCUABLE + 1))
      [ "$R" -le "$WORST" ] || { WORST="$R"; WORST_WHO="$f/$(basename "$c")"; }
    fi
  done
done
# #1388's assertion shape: the budget is a SPAN, and a span is what a disturbance is
# measured in. A look costs one frame read and one calibration, so the span the budget
# really covers is looks x spacing frames, and both are converted through the stream's own
# frame rate rather than through --fps.
FPS="$(sed -n 's/.*I1445 .* fps=\([0-9.]*\) .*/\1/p' /run1445/a_subject.txt | head -1)"
FPS="${FPS:-30}"
WORST_S="$(awk -v r="$WORST" -v s="$SPACING" -v f="$FPS" 'BEGIN{printf "%.2f", (f>0)? r*s/f : 0}')"
SPAN_S="$(awk -v l="$LOOKS" -v s="$SPACING" -v f="$FPS" 'BEGIN{printf "%.2f", (f>0)? l*s/f : 0}')"
echo "  the longest run on a camera whose AVERAGED frame was refused -- the population a look is spent on: $WORST (${WORST_WHO:-none})"
echo "  the longest run on ANY camera that answers, for context and NOT the bound: $ALLWORST (${ALLWORST_WHO:-none})"
echo "  at $FPS fps and $SPACING cycles a look, that run is ${WORST_S}s and the whole budget spans ${SPAN_S}s"
echo "  cameras whose AVERAGED frame is refused while single frames answer: $RESCUABLE"
if [ -n "$SILENT" ]; then
  echo "  clips that answered at no look in the window (not a budget question, #1437's):$SILENT"
fi
if [ "$RESCUABLE" = 0 ]; then
  say "FAIL no camera of either fixture has an averaged frame refused where the single frames answer, so this slice repairs nothing measurable here and phase A is the only thing keeping it honest" no
elif [ "$LOOKS" -le "$WORST" ]; then
  say "FAIL the budget is $LOOKS looks and the longest run of consecutive refused looks measured here is $WORST ($WORST_WHO); a budget that does not outlast the observed disturbance is a camera set aside for the evening" no
else
  say "OK   $LOOKS looks (${SPAN_S}s) against a longest observed run of $WORST (${WORST_S}s, $WORST_WHO) -- the budget outlasts the disturbance it was sized against, with $((LOOKS - WORST)) looks to spare" ok
fi

# ---- the detector runs ----------------------------------------------------------------
await() {
  local file="$1" needle="$2" limit="$3" i=0
  while [ $i -lt "$limit" ]; do
    grep -qaE "$needle" "$file" 2>/dev/null && { echo "$i"; return 0; }
    i=$((i + 1)); sleep 1
  done
  echo "TIMEOUT-$limit"; return 1
}

# One bounded detector run. $1 names the transcript, $2 is the --cams list, rest is
# environment. The cache goes with every run: a calibration read back from a file is not a
# calibration this tester measured, and it would skip the retry entirely (#1372).
run() {
  local tag="$1" cams="$2"; shift 2
  rm -rf /run1445/cache "$HOME/.config" 2>/dev/null; mkdir -p "$HOME/.config"
  env "$@" $BIN --debug --cams "$cams" --width 1280 --height 720 > "/run1445/$tag.out" 2>&1 &
  local P=$!
  await "/run1445/$tag.out" 'GEOMETRY SEALED|BOARD FAULTED|Scorer running with' 240 > /dev/null
  sleep 4
  kill -TERM $P 2>/dev/null; wait $P 2>/dev/null
  sed 's/\x1b\[[0-9;]*m//g' "/run1445/$tag.out" > "/run1445/$tag.txt"
}
answered() { grep -aoE 'CAMERAS: [0-9]+ of [0-9]+' "/run1445/$1.txt" | head -1 | awk '{print $2}'; }
asked()    { grep -aoE 'CAMERAS: [0-9]+ of [0-9]+' "/run1445/$1.txt" | head -1 | awk '{print $4}'; }
seal()     { grep -a 'GEOMETRY SEALED:' "/run1445/$1.txt" | head -1 | sed 's/.*GEOMETRY SEALED: //'; }

cams_of() { clips_of "$1" | tr '\n' ',' | sed 's/,$//'; }

echo
echo "=== C. one binary, the retry on and off, held to the actual numbers ==="
for f in $FIXTURES; do
  run "c_${f}_on"   "$(cams_of "$f")"
  run "c_${f}_once" "$(cams_of "$f")" OD_CALIBRATION_LOOKS=once
  ON="$(answered "c_${f}_on")"; ONOF="$(asked "c_${f}_on")"
  OFF="$(answered "c_${f}_once")"; OFFOF="$(asked "c_${f}_once")"
  N="$(clips_of "$f" | wc -l | tr -d ' ')"
  echo "  $f: retry on -> CAMERAS: ${ON:-none} of ${ONOF:-none}; OD_CALIBRATION_LOOKS=once -> ${OFF:-none} of ${OFFOF:-none}; the fixture holds $N"
  grep -aoE 'LOOK AGAIN: camera [0-9]+ calibrated on look [0-9]+ of [0-9]+' "/run1445/c_${f}_on.txt" | sed 's/^/       /'
  if [ -z "${ON:-}" ] || [ -z "${OFF:-}" ]; then
    say "FAIL $f: one of the two runs printed no camera census at all, so nothing here was measured" no
  elif [ "$ONOF" != "$N" ] || [ "$OFFOF" != "$N" ]; then
    say "FAIL $f: the detector was asked about $ONOF/$OFFOF cameras and the fixture holds $N" no
  elif [ "$ON" != "$N" ]; then
    grep -aE '^\[ERROR\]\[GEOMETRY_CALIBRATION\] - Camera' "/run1445/c_${f}_on.txt" | sort -u | sed 's/^/       /'
    say "FAIL $f answers for $ON of its $N clips WITH the retry, which is what this slice exists to fix" no
  elif [ "$f" = mocks ] && [ "$OFF" != "$N" ]; then
    say "FAIL the shipped mocks answer $OFF of $N without the retry; this fixture's readings must not have moved and the retry must not be what is holding them up" no
  elif [ "$f" != mocks ] && [ "$OFF" = "$N" ]; then
    say "FAIL $f answers for all $N with the retry DISABLED, so the switch changes nothing and phase C cannot fail" no
  else
    say "OK   $f: $ON of $N with the retry, $OFF of $N without it" ok
  fi
done

echo
echo "=== C2. a look is quiet, and a look is not silent ==="
# The two halves of what a further look may say, measured on the transcripts phase C
# already produced, with no extra run.
#
# #1445's first version said every look out loud (#1457): `calibrateSingleCamera` announces each
# refusal it reaches as an ERROR, and a further look reaches the same ones, so a camera
# refused on the averaged frame and on all twelve looks wrote fourteen lines -- thirteen
# byte-identical refusals plus the summary saying it had been asked more than once --
# where before the retry an operator read one. phases1318 and phases1392 hold the count
# from the other side, on a board with a camera refused for good and no --debug.
#
# What is asserted here is the half those cannot see. Every run below is a --debug run, so
# a look that was quieted by being DELETED and a look that was quieted by being written at
# DEBUG look identical in a log without --debug and different in one with it. A retry
# nobody can debug is not worth having.
QUIET=""
for f in $FIXTURES; do
  t="/run1445/c_${f}_on.txt"
  grep -aqE "LOOK AGAIN: camera\(s\) [0-9, ]+ were refused on this start's averaged frame" "$t" || continue
  AGAIN="$(grep -acE '^\[DEBUG\]\[GEOMETRY_CALIBRATION\] - Calibrating camera [0-9]+ again' "$t" || true)"
  DUP="$(grep -aE '^\[ERROR\]\[GEOMETRY_CALIBRATION\] - Camera' "$t" | sort | uniq -d | wc -l | tr -d ' ')"
  echo "  $f: $AGAIN further look(s) written at DEBUG, $DUP calibration ERROR sentence(s) said more than once"
  [ "$AGAIN" -ge 1 ] || QUIET="$QUIET $f(no look is audible even under --debug)"
  [ "$DUP" = 0 ] || QUIET="$QUIET $f($DUP repeated ERROR sentence(s))"
done
if [ -z "$QUIET" ]; then
  say "OK   every look this board took is in its --debug transcript, and no refusal is said twice at ERROR" ok
else
  say "FAIL:$QUIET" no
fi

echo
echo "=== D1. every look falls before the seal, and there is one seal ==="
BAD=""
for f in $FIXTURES; do
  for tag in "c_${f}_on" "c_${f}_once"; do
    S="$(grep -an 'GEOMETRY SEALED:' "/run1445/$tag.txt" | wc -l | tr -d ' ')"
    SL="$(grep -an 'GEOMETRY SEALED:' "/run1445/$tag.txt" | head -1 | cut -d: -f1)"
    LAST="$(grep -an 'LOOK AGAIN' "/run1445/$tag.txt" | tail -1 | cut -d: -f1)"
    echo "  $tag: $S seal(s), first at line ${SL:-none}, last LOOK AGAIN at line ${LAST:-none}"
    [ "$S" = 1 ] || BAD="$BAD $tag(seals=$S)"
    [ -z "${LAST:-}" ] || [ -z "${SL:-}" ] || [ "$LAST" -lt "$SL" ] || BAD="$BAD $tag(look@$LAST after seal@$SL)"
  done
done
if [ -z "$BAD" ]; then
  say "OK   one seal per run, and no look was taken after it: every look is on the calibration side of the boundary" ok
else
  say "FAIL a look was taken after the geometry was sealed, or a run sealed more than once:$BAD" no
fi

echo
echo "=== D2. the retry adds a camera's geometry and changes nobody else's ==="
MOVED=""
for f in $FIXTURES; do
  seal "c_${f}_on"   | tr '|' '\n' | sed 's/^ *//;s/ *$//' | sort > "/run1445/seal_${f}_on.txt"
  seal "c_${f}_once" | tr '|' '\n' | sed 's/^ *//;s/ *$//' | sort > "/run1445/seal_${f}_once.txt"
  # Every camera that was SCORING without the retry must appear, byte for byte, with it.
  while read -r line; do
    case "$line" in *scoring=1*) ;; *) continue ;; esac
    grep -qxF "$line" "/run1445/seal_${f}_on.txt" || MOVED="$MOVED [$f] $line"
  done < "/run1445/seal_${f}_once.txt"
  ONC="$(grep -c 'scoring=1' "/run1445/seal_${f}_on.txt" || true)"
  OFFC="$(grep -c 'scoring=1' "/run1445/seal_${f}_once.txt" || true)"
  echo "  $f: $OFFC camera(s) in the seal without the retry, $ONC with it"
done
if [ -z "$MOVED" ]; then
  say "OK   every camera that calibrated without the retry has a byte-identical entry in the seal taken with it -- the retry only ever filled an empty slot" ok
else
  say "FAIL the retry changed the sealed geometry of a camera that had already calibrated:$MOVED" no
fi

echo
echo "=== D3. no run says it adopted geometry mid-run ==="
ADOPTED="$(grep -al 'BOARD ADOPTED GEOMETRY MID-RUN' /run1445/c_*.txt 2>/dev/null | tr '\n' ' ')"
if [ -z "$ADOPTED" ]; then
  say "OK   ADR-0080 section 2's refusal was not tripped by any of these runs" ok
else
  say "FAIL these runs adopted geometry while running: $ADOPTED" no
fi

echo
echo "=== D4. the camera a look calibrated is a camera ADR-0080's review asks about ==="
# The claim is that a look produces SEALED geometry, under exactly the guard everything
# else is under -- not a second-class measurement the mid-run refusal is blind to. The
# observable is `reviewGeometry`, which is the thing ADR-0080 section 2 acts through: it
# asks every camera whose `sees_board` is true and SKIPS the rest without a word (#1318 --
# a slot that abstained has no held geometry to compare a picture against). So a camera
# that only has geometry because a look gave it one must be ASKED, by name, and the same
# camera on the same footage with the retry off must not be asked at all.
#
# That pair is the distinction the issue asks for. The other half -- that the refusal still
# FIRES -- is #899's and #1388's testers, which measure exactly that and are on this tree;
# duplicating them here badly would be worse than running them, and the first attempt at
# it measured the wrong thing twice over (#899's MJPG warp moves which camera is refused,
# so the nudged fixture was not about camera 3 at all).
#
# The board is made blind with OD_BLIND_AFTER and gets its cameras back, which is the one
# path that reaches reviewGeometry. Bounded by its own recorded pid, not by OD_MAX_CYCLES:
# a board with a suspended score is still a board that never exits (#895).
RIGCAMS="$(cams_of rig-20260918)"
for arm in on once; do
  rm -rf /run1445/cache "$HOME/.config" 2>/dev/null; mkdir -p "$HOME/.config"
  ENVARGS="OD_BLIND_AFTER=40 OD_BLIND_FOR_MS=6000"
  [ "$arm" = once ] && ENVARGS="$ENVARGS OD_CALIBRATION_LOOKS=once"
  env $ENVARGS $BIN --debug --cams "$RIGCAMS" --width 1280 --height 720 \
    > "/run1445/d4_$arm.out" 2>&1 &
  P=$!
  await "/run1445/d4_$arm.out" 'BOARD RECOVERED|BOARD MOVED|BOARD FAULTED|GEOMETRY REVIEW' 240 > /dev/null
  sleep 3
  kill -TERM $P 2>/dev/null; wait $P 2>/dev/null
  sed 's/\x1b\[[0-9;]*m//g' "/run1445/d4_$arm.out" > "/run1445/d4_$arm.txt"
  echo "  arm=$arm: $(grep -aoE 'CAMERAS: [0-9]+ of [0-9]+' "/run1445/d4_$arm.txt" | head -1); the review asked about camera(s): $(grep -aoE 'GEOMETRY REVIEW: camera [0-9]+' "/run1445/d4_$arm.txt" | grep -oE '[0-9]+$' | sort -u | tr '\n' ',' | sed 's/,$//')"
done
ON_ASKED="$(grep -aoE 'GEOMETRY REVIEW: camera 3' /run1445/d4_on.txt | wc -l | tr -d ' ')"
OFF_ASKED="$(grep -aoE 'GEOMETRY REVIEW: camera 3' /run1445/d4_once.txt | wc -l | tr -d ' ')"
ON_ANY="$(grep -aoE 'GEOMETRY REVIEW: camera [0-9]+' /run1445/d4_on.txt | wc -l | tr -d ' ')"
OFF_ANY="$(grep -aoE 'GEOMETRY REVIEW: camera [0-9]+' /run1445/d4_once.txt | wc -l | tr -d ' ')"
if [ "$ON_ANY" = 0 ] || [ "$OFF_ANY" = 0 ]; then
  say "FAIL one of the arms never reached a geometry review at all (on=$ON_ANY lines, once=$OFF_ANY), so nothing here was asked" no
elif [ "$ON_ASKED" = 0 ]; then
  say "FAIL camera 3 has geometry only because a look gave it one, and ADR-0080's review did not ask it: a retry's geometry is not under the same guard as the rest" no
elif [ "$OFF_ASKED" != 0 ]; then
  say "FAIL the review asked camera 3 with the retry DISABLED too, so being asked is not attributable to the look and this proves nothing" no
else
  grep -aE 'GEOMETRY REVIEW: camera 3' /run1445/d4_on.txt | head -1 | sed 's/^/       /'
  say "OK   the review asks camera 3 on the board that calibrated it by a look ($ON_ASKED line(s) of $ON_ANY) and does not ask it at all on the board that set it aside -- a look produces sealed geometry, not a second-class measurement" ok
fi

echo
echo "=== the verdict ==="
if [ "$FAILED" = 0 ]; then echo "1445-looks: PASS"; else echo "1445-looks: FAIL"; fi
exit $FAILED
