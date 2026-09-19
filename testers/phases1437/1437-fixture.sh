set -u
# #1437: a fixture answers for every clip it holds, or something goes red.
#
# The issue this comes from is not a red label -- it is the absence of one. On
# detector-integration-w128, `mocks/rig-20260918/cam_2.mp4` stopped answering a
# fixture-wide measurement and run_all.sh stayed at 55 of 58. #1416's ring-identity
# census printed `rig doubles 27/27, --, 35/35` and passed, so every figure taken across
# that fixture on that tree is one camera short of what its author believes.
#
# So the claim here is about the FIXTURE and not about any camera's aim: a measurement
# over N clips must account for N clips. The count comes from the directory listing on
# every line below -- never from the literal 3 -- because a fixture that grows a fourth
# camera must widen what is asserted rather than silently keep passing on three.
#
#   A  THE CONTROL. Each fixture calibrates for every clip it holds, in an ordinary run.
#      This is the claim, and on its own it is worth little: it is one grep that would
#      also pass if the detector printed the line for a reason unrelated to cameras.
#
#   B  THE MUTATION PROOF, which is what makes A a rule. The same fixture with ONE clip
#      replaced by footage of a warm room and no dartboard (#1318's generator). The run
#      must come back one camera short AND name that camera. A check that has never been
#      shown to fail is a check nobody can trust -- #1389's census plants a fourth quorum
#      for the same reason, and #997 measured a scanner reporting zero across 117
#      components because its marker was in the wrong form.
#
#   C  THE HOLD SCAN. A calibrates on whatever frame the detector seeks to; a measurement
#      that HOLDS a frame -- #1317's cut, which is the shape #1416's census used -- asks a
#      different question and is where cam_2 went quiet. Every clip of every fixture must
#      answer at some hold in the scan. A clip that answers at NONE of them has stopped
#      contributing, which is this issue, and it is named rather than left as a gap.
#
# EVERY DETECTOR RUN BELOW IS UNDER A BOUND. A board that cannot calibrate goes to #895's
# fault vigil and stays up on purpose, and OD_MAX_CYCLES does not bound it -- phase B and
# the failing holds in C both produce exactly that board, so each run is backgrounded and
# ended by its own recorded pid. Never by pattern.
BIN=/app/build/opendartboard
FAILED=0
say() { echo "$1"; [ "$2" = ok ] || FAILED=1; }

# The fixtures, and what each one HOLDS, read from the directory rather than stated.
fixture_dir() { [ "$1" = mocks ] && echo /app/mocks || echo "/app/mocks/$1"; }
clips_of() { ls "$(fixture_dir "$1")"/cam_*.mp4 2>/dev/null | sort; }
holds() { clips_of "$1" | wc -l | tr -d ' '; }
cams_of() { clips_of "$1" | tr '\n' ',' | sed 's/,$//'; }

await() {
  local file="$1" needle="$2" limit="$3" i=0
  while [ $i -lt "$limit" ]; do
    grep -qaE "$needle" "$file" 2>/dev/null && { echo "$i"; return 0; }
    i=$((i + 1)); sleep 1
  done
  echo "TIMEOUT-$limit"; return 1
}

# One bounded detector run. $1 names the transcript, $2 is the --cams list, rest is
# environment. The cache and the config go with each run: a calibration read back from
# cache is not a calibration this tester measured.
run() {
  local tag="$1" cams="$2"; shift 2
  rm -rf /run1437/cache "$HOME/.config" 2>/dev/null; mkdir -p "$HOME/.config"
  env "$@" $BIN --debug --cams "$cams" --width 1280 --height 720 > "/run1437/$tag.out" 2>&1 &
  local P=$!
  await "/run1437/$tag.out" 'CAMERAS: [0-9]+ of|BOARD FAULTED|Scorer running with' 150 > /dev/null
  sleep 4
  kill -TERM $P 2>/dev/null; wait $P 2>/dev/null
  sed 's/\x1b\[[0-9;]*m//g' "/run1437/$tag.out" > "/run1437/$tag.txt"
}

# What the run's own census line said, as two numbers: answered, of.
answered() { grep -aoE 'CAMERAS: [0-9]+ of [0-9]+' "/run1437/$1.txt" | head -1 | awk '{print $2}'; }
asked()    { grep -aoE 'CAMERAS: [0-9]+ of [0-9]+' "/run1437/$1.txt" | head -1 | awk '{print $4}'; }

FIXTURES="$(ls -d /app/mocks/*/ 2>/dev/null | sed 's#/app/mocks/##;s#/$##') mocks"

echo "=== the fixtures this tree holds, and how many clips each one is ==="
for f in $FIXTURES; do echo "  $f: $(holds "$f") clips -- $(cams_of "$f")"; done
echo

echo "=== A. every fixture answers for every clip it holds ==="
for f in $FIXTURES; do
  N="$(holds "$f")"
  run "a_$f" "$(cams_of "$f")"
  GOT="$(answered "a_$f")"; OF="$(asked "a_$f")"
  echo "  $f: the run says CAMERAS: ${GOT:-none} of ${OF:-none}, and the directory holds $N"
  if [ -z "$GOT" ]; then
    say "FAIL $f: the run printed no camera census at all, so nothing here was measured" no
  elif [ "$OF" != "$N" ]; then
    say "FAIL $f: the detector was asked about $OF cameras and the fixture holds $N" no
  elif [ "$GOT" != "$N" ]; then
    grep -aE '^\[ERROR\]\[GEOMETRY_CALIBRATION\] - Camera' "/run1437/a_$f.txt" | sort -u || true
    say "FAIL $f answers for $GOT of its $N clips; a fixture-wide figure taken here is $((N - GOT)) camera(s) short" no
  else
    say "OK   $f answers for all $N of its clips" ok
  fi
done

echo
echo "=== B. the same check, against a fixture with one clip that sees no dartboard ==="
# The control for A. Built from #1318's generator rather than by deleting a clip: a
# missing file would be refused by the capture stage and would prove only that a path
# can be wrong. This is a camera that OPENS, answers, and is looking at a warm room.
g++ -std=c++17 -O1 -o /run1437/make_source /app/testers/i1318_make_source.cpp \
  $(pkg-config --cflags --libs opencv4) > /run1437/make_source.log 2>&1 \
  || { say "FAIL could not build #1318's source generator, so B measures nothing" no; }
if [ -x /run1437/make_source ]; then
  /run1437/make_source face /run1437/face.avi 1280 720 15 120 > /dev/null 2>&1
  N="$(holds mocks)"
  # The second clip replaced, so the hole is in the middle and a census that stops at the
  # first gap is not enough to pass.
  MUT="$(clips_of mocks | awk -v f=/run1437/face.avi 'NR==2{print f;next}{print}' | tr '\n' ',' | sed 's/,$//')"
  echo "  cams: $MUT"
  run b_mutant "$MUT"
  GOT="$(answered b_mutant)"; OF="$(asked b_mutant)"
  echo "  the run says CAMERAS: ${GOT:-none} of ${OF:-none}"
  if [ "$GOT" = "$N" ]; then
    say "FAIL a camera looking at no dartboard was counted as answering, so A cannot fail" no
  elif [ "$GOT" = "$((N - 1))" ] && [ "$OF" = "$N" ]; then
    if grep -qaE '^\[ERROR\]\[GEOMETRY_CALIBRATION\] - Camera 2 ' /run1437/b_mutant.txt; then
      grep -aE '^\[ERROR\]\[GEOMETRY_CALIBRATION\] - Camera 2 ' /run1437/b_mutant.txt | head -1
      say "OK   the short fixture is caught, at $GOT of $N, and camera 2 is named" ok
    else
      say "FAIL it came back $GOT of $N but nothing named camera 2, so the count is not attributable" no
    fi
  else
    say "FAIL the mutant answered $GOT of $OF, which is neither the fixture nor one short of it" no
  fi
fi

echo
echo "=== C. the hold scan: every clip answers at some held frame ==="
# #1317's cut. The dev build seeks a file source three seconds in, so a clip cut at
# frame F is calibrated a little after F -- the hold is the stretch, not the frame.
g++ -std=c++17 -O1 -o /run1437/partial /app/testers/i1317_partial_footage.cpp \
  $(pkg-config --cflags --libs opencv4) > /run1437/partial.log 2>&1 \
  || { say "FAIL could not build #1317's cutter, so C measures nothing" no; }
HOLDS="${HOLDS:-60 300 600 900 1200}"
if [ -x /run1437/partial ]; then
  for f in $FIXTURES; do
    N="$(holds "$f")"
    ANSWERED_SOMEWHERE=""
    for h in $HOLDS; do
      START="$(python3 -c "print($h/30.0)")"
      CUT=""; i=0
      for c in $(clips_of "$f"); do
        i=$((i + 1))
        /run1437/partial "$c" "/run1437/${f}_${h}_$i.avi" "$START" 0 0 300 > /dev/null 2>&1 || break
        CUT="$CUT/run1437/${f}_${h}_$i.avi,"
      done
      CUT="${CUT%,}"
      run "c_${f}_${h}" "$CUT"
      GOT="$(answered "c_${f}_${h}")"
      SEE="$(grep -aoE 'are looking at the dartboard \([0-9,]*\)' "/run1437/c_${f}_${h}.txt" | head -1 | grep -oE '[0-9,]+' | tr ',' ' ')"
      echo "  $f hold $h: ${GOT:-none} of $N answered${SEE:+ (cameras $SEE)}"
      grep -aoE 'Camera [0-9]+ did not calibrate: the wire stage found [0-9]+ wire boundaries' \
        "/run1437/c_${f}_${h}.txt" | sort -u | sed 's/^/       /'
      ANSWERED_SOMEWHERE="$ANSWERED_SOMEWHERE $SEE"
    done
    MISSING=""
    i=0
    for c in $(clips_of "$f"); do
      i=$((i + 1))
      case " $ANSWERED_SOMEWHERE " in
        *" $i "*) ;;
        *) MISSING="$MISSING $(basename "$c")" ;;
      esac
    done
    if [ -z "$MISSING" ]; then
      say "OK   $f: every one of its $N clips answered at some hold in the scan" ok
    else
      say "FAIL $f: these clips answered at NO hold in the scan and have stopped contributing:$MISSING" no
    fi
  done
fi

echo
echo "=== the verdict ==="
if [ "$FAILED" = 0 ]; then echo "1437-fixture: PASS"; else echo "1437-fixture: FAIL"; fi
exit $FAILED
