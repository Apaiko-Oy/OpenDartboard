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
WORST=0; WORST_WHO=""; SILENT=""; RESCUED=0; RESCUABLE=0
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
    # The run that a budget has to outlast is measured on cameras that CAN be calibrated,
    # which is the population a retry is spent on.
    [ "$R" -le "$WORST" ] || { WORST="$R"; WORST_WHO="$f/$(basename "$c")"; }
    if [ "${O:-1}" = 0 ]; then
      RESCUABLE=$((RESCUABLE + 1))
      RESCUED=$((RESCUED + 1))
    fi
  done
done
echo "  the longest run of consecutive refused looks on any camera that answers: $WORST (${WORST_WHO:-none})"
echo "  cameras whose AVERAGED frame is refused while single frames answer: $RESCUABLE"
if [ -n "$SILENT" ]; then
  echo "  clips that answered at no look in the window (not a budget question, #1437's):$SILENT"
fi
if [ "$RESCUABLE" = 0 ]; then
  say "FAIL no camera of either fixture has an averaged frame refused where the single frames answer, so this slice repairs nothing measurable here and phase A is the only thing keeping it honest" no
elif [ "$LOOKS" -le "$WORST" ]; then
  say "FAIL the budget is $LOOKS looks and the longest run of consecutive refused looks measured here is $WORST ($WORST_WHO); a budget that does not outlast the observed disturbance is a camera set aside for the evening" no
else
  say "OK   $LOOKS looks against a longest observed run of $WORST ($WORST_WHO) -- the budget outlasts the disturbance it was sized against, with $((LOOKS - WORST)) to spare" ok
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
echo "=== D4. the refusal fires about the camera the retry calibrated, and only then ==="
# The camera that only has geometry because a look gave it one comes back nudged. With the
# retry ON it is a witness and the board must refuse; with it OFF it abstained, so the same
# nudge must go unnoticed -- which is what makes the ON arm evidence rather than a claim.
g++ -std=c++17 -O1 -o /run1445/moved /app/testers/i899_moved_footage.cpp \
  $(pkg-config --cflags --libs opencv4) > /run1445/moved.log 2>&1 \
  || { say "FAIL could not build #899's warp, so D4 measures nothing" no; }
if [ -x /run1445/moved ]; then
  RIG=/app/mocks/rig-20260918
  # What the board calibrates on: the opening of each clip, re-encoded the same way both
  # arms are, so the only difference between them is the nudge (#899's control argument).
  for i in 1 2 3; do
    /run1445/moved "$RIG/cam_$i.mp4" "/run1445/held_$i.avi" 0 0 0 400 0 > /dev/null 2>&1 || true
  done
  # And what camera 3 comes back as: a later stretch of its own clip, 20 px across and 15
  # px down. Cameras 1 and 2 come back as themselves.
  /run1445/moved "$RIG/cam_3.mp4" "/run1445/nudged_3.avi" 20 15 0 400 400 > /dev/null 2>&1 || true
  if [ -s /run1445/held_1.avi ] && [ -s /run1445/nudged_3.avi ]; then
    for arm in on once; do
      # The exchange happens while the board is blind: the running process holds its own
      # file handles and only the reopen sees the new file.
      rm -rf /run1445/cache "$HOME/.config" 2>/dev/null; mkdir -p "$HOME/.config"
      cp /run1445/held_3.avi /run1445/slot3.avi
      ENVARGS="OD_BLIND_AFTER=40 OD_BLIND_FOR_MS=12000"
      [ "$arm" = once ] && ENVARGS="$ENVARGS OD_CALIBRATION_LOOKS=once"
      env $ENVARGS $BIN --debug \
        --cams "/run1445/held_1.avi,/run1445/held_2.avi,/run1445/slot3.avi" \
        --width 1280 --height 720 > "/run1445/d4_$arm.out" 2>&1 &
      P=$!
      await "/run1445/d4_$arm.out" 'GEOMETRY SEALED' 180 > /dev/null
      await "/run1445/d4_$arm.out" 'BLINDING' 180 > /dev/null
      cp /run1445/nudged_3.avi /run1445/slot3.avi
      i=0
      while [ $i -lt 120 ]; do
        grep -qaE 'BOARD MOVED|BOARD SETTLED|BOARD FAULTED' "/run1445/d4_$arm.out" 2>/dev/null && break
        i=$((i + 1)); sleep 1
      done
      sleep 2
      kill -TERM $P 2>/dev/null; wait $P 2>/dev/null
      sed 's/\x1b\[[0-9;]*m//g' "/run1445/d4_$arm.out" > "/run1445/d4_$arm.txt"
      SEEING="$(grep -aoE 'CAMERAS: [0-9]+ of [0-9]+' "/run1445/d4_$arm.txt" | head -1)"
      echo "  arm=$arm: $SEEING; moved=$(grep -ca 'BOARD MOVED' "/run1445/d4_$arm.txt" || true) settled=$(grep -ca 'BOARD SETTLED' "/run1445/d4_$arm.txt" || true) review=$(grep -ca 'GEOMETRY REVIEW' "/run1445/d4_$arm.txt" || true)"
    done
    ON_MOVED="$(grep -ca 'BOARD MOVED' /run1445/d4_on.txt || true)"
    OFF_MOVED="$(grep -ca 'BOARD MOVED' /run1445/d4_once.txt || true)"
    ON_SEES="$(grep -aoE 'CAMERAS: [0-9]+ of' /run1445/d4_on.txt | head -1 | awk '{print $2}')"
    OFF_SEES="$(grep -aoE 'CAMERAS: [0-9]+ of' /run1445/d4_once.txt | head -1 | awk '{print $2}')"
    if [ "${ON_SEES:-0}" = "${OFF_SEES:-0}" ]; then
      say "FAIL both arms calibrated the same cameras (${ON_SEES:-none}), so camera 3's geometry did not come from a look and D4 is not about this slice" no
    elif [ "${ON_MOVED:-0}" = 0 ]; then
      grep -aE 'GEOMETRY REVIEW|BOARD SETTLED|BOARD MOVED' /run1445/d4_on.txt | head -5 | sed 's/^/       /'
      say "FAIL camera 3 was nudged and the board that calibrated it BY A LOOK did not refuse; a retry's geometry is not under ADR-0080's guard" no
    elif [ "${OFF_MOVED:-0}" != 0 ]; then
      say "FAIL the arm with the retry disabled also refused, so the ON arm's refusal is not attributable to the retried camera" no
    else
      grep -aE 'BOARD MOVED' /run1445/d4_on.txt | head -1 | sed 's/^/       /'
      say "OK   nudging camera 3 refuses the board that calibrated it by a look, and goes unnoticed by the board that set it aside -- the retry's geometry is sealed geometry" ok
    fi
  else
    say "FAIL #899's warp produced no footage, so D4 measures nothing" no
  fi
fi

echo
echo "=== the verdict ==="
if [ "$FAILED" = 0 ]; then echo "1445-looks: PASS"; else echo "1445-looks: FAIL"; fi
exit $FAILED
