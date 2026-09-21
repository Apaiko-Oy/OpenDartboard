set -u
# #1486: an anchor a camera did not measure itself, and the refusal that keeps it honest.
#
# WHAT THIS IS ABOUT. `chooseScore` needs TWO cameras that measured a wedge before a dart
# can be published at 0.9, and exactly one branch of `orientation_processing` ever set
# `anchored`: the star camera's own measurement. The other two branches derive an index
# from an assumption about where the camera is BOLTED -- and #797 measured that guess
# loose, so #1346 refused to trust it. One anchored camera can never be two, so 0.9 had
# never been published on either fixture in this repository.
#
# #1486 derives the missing anchors from a dart every camera saw: an anchored camera says
# which WEDGE the tip is in, an unanchored one says which of its own wire slots, and the
# difference is the rotation between the two rings -- a rigid fact about the rig, measured
# rather than assumed. The plane #1486 was filed proposing cannot do this on its own and
# the header says why: `wire_model::planeOf` pins each camera's board frame only up to an
# unknown rotation, which is the very quantity wanted.
#
# THREE PHASES.
#
#   1  the decisions, compiled from the pure header. No detector and no footage.
#   2  five PLANTED mutations of that header, each of which the phase-1 check must catch.
#      A check nothing has been measured against is a check nobody knows the value of.
#   3  the detector on the shipped mocks, twice on ONE binary -- ordinary, and under
#      OD_ANCHOR=own, which is the rule every build before this issue had. Same binary,
#      the spelling chosen at run time, so "a different build" is never a confound
#      (#1339, #1442, #1450, #1489).
#
# WHY THE SHIPPED MOCKS AND NOT THE RIG. They are the only footage here where any camera
# anchors itself: one star camera and two that cannot. #1486 PROPAGATES an anchor and
# cannot create one, so the rig -- which anchors nobody, measured -- has nothing to
# propagate from, and what phase 3 asks of it is exactly that nothing is derived there.
#
# It asserts nothing about a SCORE being right: the shipped mocks have no ground truth
# (#1478), and a threshold fitted to them would be #1322's mistake. What it asserts is
# that the derivation happened, that the falsifier removes it, and that the refusal fires.

BIN=/app/build/opendartboard
MOCKS=/app/mocks/cam_1.mp4,/app/mocks/cam_2.mp4,/app/mocks/cam_3.mp4
RIG_DIR=/app/mocks/rig-20260918
RIG=$RIG_DIR/cam_1.mp4,$RIG_DIR/cam_2.mp4,$RIG_DIR/cam_3.mp4
HEADER=/app/src/detector/geometry/calibration/orientation_processing.hpp
CHECK=/app/testers/i1486_anchor_check.cpp
RUN=/run1486
CYCLES="${OD_1486_CYCLES:-900}"
RUN_TIMEOUT="${OD_1486_TIMEOUT:-900}"

FAILED=0
say() { echo "$1"; [ "$2" = ok ] || FAILED=1; }
CVFLAGS=$(pkg-config --cflags --libs opencv4)

# ---- phase 1: the decisions, on their own ------------------------------------------------
echo "############ phase 1: the derivation's own decisions ############"
if ! g++ -std=c++17 -O1 -o $RUN/anchor_check $CHECK $CVFLAGS 2> $RUN/anchor_check.err; then
  echo "FAIL testers/i1486_anchor_check.cpp did not compile; nothing below it means anything."
  sed 's/^/    /' $RUN/anchor_check.err | tail -20
  exit 1
fi
if $RUN/anchor_check > $RUN/anchor_check.out 2>&1; then
  say "PASS the derivation answers every one of its own decisions: $(tail -1 $RUN/anchor_check.out)" ok
else
  say "FAIL the derivation does not answer its own decisions on this tree:" no
  grep -a '^FAIL' $RUN/anchor_check.out | sed 's/^/    /'
fi

# ---- phase 2: what those assertions are worth --------------------------------------------
#
# Each plant is one edit to a copy of the header, and each is a mistake somebody could
# really make: trusting a contradiction, believing one dart, ignoring the residual, losing
# the sign, dropping the fraction. A plant the check does NOT catch is reported by name --
# it means the assertion covering it is decoration.
echo
echo "############ phase 2: five planted mutations, each of which must be caught ############"
plant() {
  local label="$1" from="$2" to="$3"
  local dir=$RUN/plant-$label
  rm -rf $dir; mkdir -p $dir/src/detector/geometry/calibration $dir/testers
  cp $CHECK $dir/testers/
  python3 - "$HEADER" "$dir/src/detector/geometry/calibration/orientation_processing.hpp" "$from" "$to" <<'PY'
import sys
src, dest, frm, to = sys.argv[1:5]
s = open(src).read()
if frm not in s:
    sys.stderr.write("NEEDLE-MISSING\n"); sys.exit(2)
open(dest, 'w').write(s.replace(frm, to, 1))
PY
  case $? in
    2) say "FAIL plant '$label' could not be applied: the line it names is no longer in the header." no; return;;
    0) ;;
    *) say "FAIL plant '$label' could not be written." no; return;;
  esac
  if ! g++ -std=c++17 -O1 -o $dir/check $dir/testers/i1486_anchor_check.cpp $CVFLAGS 2> $dir/build.err; then
    say "FAIL plant '$label' did not compile, so it measures nothing:" no
    tail -5 $dir/build.err | sed 's/^/    /'
    return
  fi
  if $dir/check > $dir/out.txt 2>&1; then
    say "FAIL plant '$label' was NOT caught -- the check passes on a tree carrying it." no
  else
    say "PASS plant '$label' is caught: $(grep -ac '^FAIL' $dir/out.txt) of $(grep -ac '^ok\|^FAIL' $dir/out.txt) assertions go red" ok
  fi
}

plant trusts-a-contradiction \
  '            anchor.refused = true;
            anchor.trusted = false;
            return;' \
  '            anchor.wedge20WireIndex = sighting.wedge20WireIndex;
            anchor.agreeing++;'
plant believes-one-dart \
  'inline constexpr int kAnchorSightingsNeeded = 2;' \
  'inline constexpr int kAnchorSightingsNeeded = 1;'
plant ignores-the-residual \
  '        if (sighting.residualWedges > kAnchorResidualWedges)' \
  '        if (false)'
plant loses-the-sign \
  '        const double offset = followerPosition - leaderPosition;' \
  '        const double offset = leaderPosition - followerPosition;'
plant drops-the-fraction \
  '        const double followerPosition = (double)follower.wireSlot + (double)follower.fraction;' \
  '        const double followerPosition = (double)follower.wireSlot;'

# ---- phase 3: the detector, twice on one binary ------------------------------------------
echo
echo "############ phase 3: the detector, with the derivation and without it ############"
if [ ! -x $BIN ]; then
  echo "FAIL no $BIN -- a fresh worktree has none."
  echo "     Build it:  make build   (or run testers/run_all.sh, which builds first)"
  exit 1
fi
NEWEST=$(find /app/src /app/CMakeLists.txt -newer $BIN 2>/dev/null | head -5)
if [ -n "$NEWEST" ]; then
  echo "FAIL $BIN is OLDER than the source it is supposed to be measuring:"
  echo "$NEWEST" | sed 's/^/       /'
  exit 1
fi
for needle in 'ANCHOR: ' 'Consensus score: '; do
  if ! strings $BIN | grep -qF "$needle"; then
    echo "FAIL $BIN does not carry '$needle', so phase 3 would be reading a binary that"
    echo "     cannot say what it is being asked about."
    exit 1
  fi
done

run_detector() {
  local tag="$1" cams="$2" cycles="$3"
  shift 3
  rm -rf $RUN/cache $RUN/debug_frames
  env "$@" OD_MAX_CYCLES="$cycles" timeout "$RUN_TIMEOUT" \
    $BIN --cams "$cams" --width 1280 --height 720 > $RUN/$tag.out 2>&1
  local rc=$?
  sed 's/\x1b\[[0-9;]*m//g' $RUN/$tag.out > $RUN/$tag.txt
  echo "    $tag: detector rc=$rc lines=$(wc -l < $RUN/$tag.txt) cycles=$cycles $*"
  if [ $rc -eq 124 ]; then
    say "FAIL $tag: still running after ${RUN_TIMEOUT}s; it was cut off by this harness." no
  elif [ $rc -ne 0 ]; then
    say "FAIL $tag: the detector exited $rc; 0 is the status of a run that reached the scoring loop." no
  fi
}
derived_count() { grep -ac 'ANCHOR: camera . derived wedge 20' $RUN/$1.txt; }
consensus_count() { grep -ac 'Consensus score: ' $RUN/$1.txt; }

run_detector mocks-derived "$MOCKS" "$CYCLES" OD_ANCHOR=
run_detector mocks-own     "$MOCKS" "$CYCLES" OD_ANCHOR=own
run_detector rig-derived   "$RIG"   "$CYCLES" OD_ANCHOR=

# The shipped mocks anchor ONE camera by its own star measurement, so they are the fixture
# where there is something to propagate from.
if grep -aq '1 of 3 cameras can be read for a wedge' $RUN/mocks-derived.txt; then
  say "PASS the shipped mocks still anchor exactly one camera by its own measurement" ok
else
  say "FAIL the shipped mocks no longer anchor exactly one camera by measurement, so this" no
  echo "     fixture no longer presents the state #1486 is about:"
  grep -a 'cameras can be read for a wedge' $RUN/mocks-derived.txt | head -1 | sed 's/^/     /'
fi

D=$(derived_count mocks-derived)
if [ "$D" -ge 1 ]; then
  say "PASS $D camera(s) derived an anchor from a dart another camera measured:" ok
  grep -a 'ANCHOR: camera . derived wedge 20' $RUN/mocks-derived.txt | sed 's/.*ANCHOR: /     /'
else
  say "FAIL no camera derived an anchor on the one fixture that has something to derive from." no
  grep -a 'ANCHOR: ' $RUN/mocks-derived.txt | tail -5 | sed 's/^/     /'
fi

C=$(consensus_count mocks-derived)
if [ "$C" -ge 1 ]; then
  say "PASS $C dart(s) published at 0.9 -- two cameras measured a wedge and agreed" ok
else
  say "FAIL no dart reached a consensus, which is what a second anchored camera is FOR." no
fi

# The falsifier. Same binary, the word chosen at run time.
if [ "$(derived_count mocks-own)" -eq 0 ]; then
  say "PASS OD_ANCHOR=own derives nothing, which is what every build before #1486 did" ok
else
  say "FAIL OD_ANCHOR=own still derived an anchor, so the falsifier does not falsify." no
fi
OWN_C=$(consensus_count mocks-own)
if [ "$OWN_C" -lt "$C" ]; then
  say "PASS and publishes fewer darts at 0.9 than the derivation does ($OWN_C against $C)" ok
else
  say "FAIL OD_ANCHOR=own published $OWN_C darts at 0.9 against the derivation's $C, so the" no
  echo "     0.9s above cannot be attributed to the derivation at all."
fi

# The rig: nothing to propagate from, so nothing propagated. This is the half that says
# #1486 propagates an anchor rather than inventing one.
if grep -aq '0 of 3 cameras can be read for a wedge' $RUN/rig-derived.txt; then
  if [ "$(derived_count rig-derived)" -eq 0 ]; then
    say "PASS the rig anchors no camera at all, and nothing was derived there: a derivation" ok
    echo "     needs a source, and this fixture has none (OD_CAMERA_WEDGES is the remedy, #1363)"
  else
    say "FAIL an anchor was derived on a fixture where NO camera measured one -- there is" no
    echo "     nothing on that board for a derived wedge 20 to be derived FROM."
  fi
else
  say "WARN the rig fixture no longer reports 0 of 3 readable cameras; the assertion above" ok
  grep -a 'cameras can be read for a wedge' $RUN/rig-derived.txt | head -1 | sed 's/^/     /'
  echo "     was about the fixture #1486 measured and is no longer the state it measured."
fi

echo
echo "CHECK_RC=$FAILED"
exit $FAILED
