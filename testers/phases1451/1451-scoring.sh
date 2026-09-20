set -u
# #1451: whether this board says, AT START, how many of its cameras a dart can actually be
# SCORED from -- and whether saying it turned a legal board into a refused one.
#
# THE BOARD THIS IS ABOUT. `scorePoint` refuses a camera whose doubles ring was not fitted
# or whose wire ring is not whole, and returns the default PointScore -- whose `score` is
# "MISS", so `may_vote` is false and the camera is absent from `chooseScore` altogether.
# The startup census asked `sees_board && hasValidDoubles` and never the ring. So such a
# camera was counted a full voter, the board reported itself whole, and the camera then
# contributed NOTHING at every dart. #1449 one field over, and worse: that issue's camera
# still scored, wrongly, as an asserted 20. This one does not score at all.
#
# It was also SILENT. `scorePoint`'s refusal is `log_debug`, below the default level, and
# the one abstain line that is at warning level covers `!sees_board` -- which this camera
# is not. Measured on the branch point in phase BEFORE: zero lines at default level.
#
# HOW THE BOARD IS BUILT, AND WHY IT IS NOT A HATCH. `calibrateSingleCamera` refuses a
# camera whose ring is not whole and clears `sees_board`, so a FRESHLY calibrated camera
# cannot reach this state -- which is why a fresh run of either fixture is not this board
# and the issue could not be reproduced by starting the detector normally. The CACHE is the
# door, and #1442's own comment in wire_processing.hpp says so: that issue moved the
# decision from `wireEndpoints.size()` to `wiresDetected` WITHOUT moving
# `sizeof(DartboardCalibration)`, so a calibration fwritten by an older binary loads
# cleanly carrying `sees_board` true, `isValid` true and a ring the guards now refuse.
#
# So the stale cache is written by a binary that really accepts the long ring, using
# #1442's own falsification switch OD_WIRE_COUNT=atleast, and then read by this binary with
# the switch off and `--reuse-calibration`. That is not a state the program is told to
# pretend to be in: it is the pre-#1442 binary's cache file, byte for byte, met by a
# post-#1442 guard. Camera 3 of mocks/rig-20260918 finds TWENTY-ONE wire boundaries, which
# is the shipped footage this needs and the reason that fixture is carried (#1317, #1322).
#
# WHAT THIS TESTER DELIBERATELY DOES NOT DO, and it is a measurement rather than a gap.
# The WARN fires at ZERO scorable cameras, and a board with zero cannot be built out of
# this repository's footage. The census reads the CACHE on this path, so zero needs a cache
# whose three slots are all refused, and `rig-20260918/cam_3.mp4` -- the only shipped clip
# whose wire stage finds more than twenty -- yields twenty-one in the THIRD slot only:
# three slots filled from that one file calibrate 20, 20, 21, because the three captures do
# not open on the same frame. Measured, not assumed: a first draft of this tester ran that
# board and got 2 of 3, identical to phase STALE. So the zero board is built out of structs
# in testers/i1451_scoring_check.cpp (label 1451-scorable), where it is exact, and this
# tester proves the same expression on a board the program really becomes.
#
# Four phases, and the first two exist to make the others mean anything:
#
#   WSTALE  write the cache with OD_WIRE_COUNT=atleast on the rig fixture. Camera 3
#           calibrates on 21 wires and the file records it. Not asserted on; it is setup.
#   BEFORE  the branch point (75f9d2b), compiled here, reading that cache. It must be
#           ADMITTED three of three, beat READY -- and say NOTHING at default level about
#           the camera the scorer is about to refuse at every dart. That is the issue's
#           whole claim and nothing in the repository demonstrated it.
#   STALE   this binary, the SAME cache. Still admitted, still READY -- naming is not
#           refusing -- and it must say 2 of 3 at start and name camera 3 with its own
#           reason and its own remedy (#1389).
#   WHOLE   this binary, the three shipped mocks, fresh: 3 of 3, named, NOT warned, and no
#           new ERROR or WARN. Without WHOLE the warning could be one that fires on every
#           board, and "the board is fine" would read as an alarm every night until nobody
#           read it.
#
# Every detector started here can end in #895's fault vigil, which never returns, so each
# is backgrounded and ended by its own recorded pid. Never by pattern.

BIN=/app/build/opendartboard
MOCKS=/app/mocks/cam_1.mp4,/app/mocks/cam_2.mp4,/app/mocks/cam_3.mp4
RIG=/app/mocks/rig-20260918/cam_1.mp4,/app/mocks/rig-20260918/cam_2.mp4,/app/mocks/rig-20260918/cam_3.mp4
STUB_URL=http://127.0.0.1:8899

await() {
  local file="$1" needle="$2" limit="$3" i=0
  while [ $i -lt "$limit" ]; do
    grep -qaE "$needle" "$file" 2>/dev/null && { echo "$i"; return 0; }
    i=$((i + 1)); sleep 1
  done
  echo "TIMEOUT-$limit"; return 1
}

# ---- the binary this tester measures is the binary this tree would build ---------------
#
# #1449's guard, inherited whole and for its reason: its first run measured a
# build/opendartboard seven minutes older than the source it was supposed to be measuring,
# and the phase duly reported that the census said nothing -- which is exactly what the
# defect looks like. A stale binary does not fail loudly here, it fails as the ISSUE.
#
# Two questions, because they catch different mistakes. The mtime catches a tree edited
# after its last build; the string catches a binary built from some OTHER tree, which an
# mtime can never see.
if [ ! -x $BIN ]; then
  echo "FAIL no $BIN -- a fresh worktree has none."
  echo "     Build it:  make build   (or run testers/run_all.sh, which builds first)"
  exit 1
fi
NEWEST=$(find /app/src /app/CMakeLists.txt -newer $BIN 2>/dev/null | head -5)
if [ -n "$NEWEST" ]; then
  echo "FAIL $BIN is OLDER than the source it is supposed to be measuring:"
  echo "$NEWEST" | sed 's/^/       /'
  echo "     Every phase below would measure a binary without this tree's changes in it,"
  echo "     and phase STALE would report the census saying nothing -- which is precisely"
  echo "     what the defect being measured looks like. Rebuild and re-run."
  exit 1
fi
if ! strings $BIN | grep -q 'cameras can be scored from'; then
  echo "FAIL $BIN does not carry #1451's census line at all."
  echo "     It was built from a tree without this change in it. Nothing below would be"
  echo "     measuring this branch. Rebuild and re-run."
  exit 1
fi
# The fixture this whole tester is built on. It is the only shipped footage holding a
# camera whose wire stage finds more than twenty, so its absence is not a board that
# scores differently -- it is a tester about nothing.
if [ ! -s /app/mocks/rig-20260918/cam_3.mp4 ]; then
  echo "FAIL mocks/rig-20260918/cam_3.mp4 is missing -- it is the 21-wire camera every"
  echo "     phase below is built from, and without it this tester measures nothing."
  exit 1
fi
echo "--- $BIN carries the census and is newer than every source under /app/src ---"

# ---- the branch point, compiled offline against this build's fetched sources ----------
# #1317's spelling: FetchContent cannot reach the network in here, and it does not need to
# -- /app/build/_deps holds what this tree already fetched, and the parent needs the same
# two at the same tags.
BASE_SRC=/run1451/pre-1451
BASE_BUILD=/run1451/build-before
if [ ! -f $BASE_SRC/CMakeLists.txt ]; then
  echo "FAIL no base source at $BASE_SRC -- the harness unpacks it"
  exit 1
fi
echo "--- compiling the branch point for phase BEFORE ---"
cmake -S $BASE_SRC -B $BASE_BUILD -DCMAKE_PREFIX_PATH=/usr/local \
  -DCMAKE_CXX_FLAGS="-DDEBUG_SEEK_VIDEO -DDEBUG_VIA_VIDEO_INPUT" \
  -DAPP_VERSION=0.0.0-dev \
  -DFETCHCONTENT_SOURCE_DIR_NLOHMANN_JSON=/app/build/_deps/nlohmann_json-src \
  -DFETCHCONTENT_SOURCE_DIR_HTTPLIB=/app/build/_deps/httplib-src > /run1451/before_cmake.log 2>&1 || {
  tail -20 /run1451/before_cmake.log; exit 1; }
cmake --build $BASE_BUILD -- -j4 --no-print-directory > /run1451/before_build.log 2>&1 || {
  tail -30 /run1451/before_build.log; exit 1; }
BEFORE_BIN=$BASE_BUILD/opendartboard
ls -l $BEFORE_BIN | sed 's/^/    /'
# The branch point must NOT carry the census, or "it said nothing" below would be a
# statement about the wrong binary. The mirror of the strings check above.
if strings $BEFORE_BIN | grep -q 'cameras can be scored from'; then
  echo "FAIL the branch point already carries #1451's census line, so BASE_COMMIT is not"
  echo "     before this issue and phase BEFORE cannot show the silence it is about."
  exit 1
fi

# ONE stub for the whole script: the credential a pairing issues lives in the stub's
# memory, and a stub restarted between phases answers every heartbeat 401, which reads in
# the transcript as a board that never beat.
TRANSCRIPT=/run1451/transcript.jsonl
STUB_TRANSCRIPT=$TRANSCRIPT STUB_INTERVAL_SECONDS=5 STUB_SILENCE_SECONDS=60 STUB_SAMPLE_SECONDS=0 \
  python3 /app/testers/turnaus_stub.py > /run1451/stub.out 2> /run1451/stub.err &
STUB=$!
sleep 1

# The phases share one transcript, so each is a slice of it, marked at both ends -- #1338's
# rule, and it was bought there: a slice open at the top end reads a later phase's READY as
# this phase's.
mark() { wc -l < "$TRANSCRIPT" 2>/dev/null || echo 0; }
beats() {
  python3 -c "
import json, sys
start, end = int(sys.argv[2]), int(sys.argv[3])
for n, line in enumerate(open(sys.argv[1])):
    if n < start or n >= end:
        continue
    try: r = json.loads(line)
    except ValueError: continue
    if r.get('event') in ('beat', 'beat_refused'):
        print(r.get('condition', ''))
" "$TRANSCRIPT" "$1" "$2"
}

echo "--- pairing, once, against the stub ---"
$BIN --pair 483920 --turnaus $STUB_URL --allow-plaintext > /run1451/pair.out 2> /run1451/pair.err
echo "PAIR_RC=$?"

# A phase: a binary, a camera list, whether the cache is kept or cleared, and an
# environment prefix. Each detector is backgrounded, waited on by a sentinel it prints
# itself, and then ended by its own recorded pid.
#
# `keep` is the whole mechanism of this tester and is the opposite of #1449's rule: that
# issue cleared cache/ on every phase because it was about a FRESH calibration's census.
# This one is about the cached path, which is the only path that can reach the state at
# all, so a reading phase must inherit exactly the file the writing phase left.
phase() {
  local tag="$1" bin="$2" cams="$3" keep="$4"; shift 4
  local from to pid extra=""
  echo "=== $tag ==="
  [ "$keep" = keep ] && extra="--reuse-calibration" || rm -rf /run1451/cache
  from=$(mark)
  env "$@" $bin --cams "$cams" --width 1280 --height 720 $extra \
    --turnaus $STUB_URL --allow-plaintext > /run1451/$tag.out 2> /run1451/$tag.err &
  pid=$!
  echo "$tag reached its verdict after $(await /run1451/$tag.out 'Scorer running with|BOARD FAULTED' 150)s"
  sleep 30
  kill -TERM $pid 2>/dev/null; wait $pid 2>/dev/null
  to=$(mark)
  # Colour codes are in every console line; strip them once and read the plain text.
  sed 's/\x1b\[[0-9;]*m//g' /run1451/$tag.out > /run1451/$tag.txt
  beats "$from" "$to" > /run1451/$tag.beats
}

phase wstale "$BIN"        "$RIG"      fresh OD_WIRE_COUNT=atleast
phase before "$BEFORE_BIN" "$RIG"      keep
phase stale  "$BIN"        "$RIG"      keep
phase whole  "$BIN"        "$MOCKS"    fresh

kill $STUB 2>/dev/null; wait $STUB 2>/dev/null

FAILED=0
say() { echo "$1"; [ "$2" = ok ] || FAILED=1; }
readied() { grep -qx READY /run1451/$1.beats; }
# #1449's lesson, bought in 5673f56: match a census by its own SHAPE, never by being the
# first line with its prefix on it. `applyConfiguredAnchors` logs ORIENTATION lines before
# the ORIENTATION census, and a per-camera reason here can contain the word "scored".
census() { grep -aoE 'SCORING: [0-9]+ of [0-9]+ cameras can be scored from' /run1451/$1.txt | head -1; }
censusN() { census "$1" | grep -oE '[0-9]+' | head -1; }

echo
echo "=== 0. the cache this tester is built on really holds a ring the guard refuses ==="
if grep -qa 'SCORING: 3 of 3 cameras can be scored from' /run1451/wstale.txt; then
  say "OK   with OD_WIRE_COUNT=atleast the 21-wire camera calibrates and is written down" ok
else say "FAIL the writing phase did not admit all three, so no stale cache was written" no; fi

echo
echo "=== 1. BEFORE: the board this issue is about, on the binary that has the defect ==="
grep -aE 'Using the cached calibration|Cached calibration accepted|BOARD FAULTED|Scorer running with' /run1451/before.txt | head -4 || true
if grep -qa 'Cached calibration accepted on 3 of 3 cameras' /run1451/before.txt; then
  say "OK   a board one camera cannot be scored from is ADMITTED, three of three" ok
else say "FAIL the cached board was not admitted three of three, so nothing below is about it" no; fi
if grep -qa 'Scorer running with 3 of 3 cameras' /run1451/before.txt; then
  say "OK   and it reports itself scoring with all three" ok
else say "FAIL it did not claim three cameras, which is the over-count this issue names" no; fi
if readied before; then
  say "OK   and it beats READY, so the Station shows a board ready to be thrown at" ok
else say "FAIL it never beat READY, so this phase is not the state the issue describes" no; fi
# The silence, measured. Not "the census is absent" alone -- NOTHING at default level
# anywhere in the run names the camera the scorer is refusing at every dart.
if [ -n "$(census before)" ]; then
  say "FAIL the branch point already said it at start; this issue has no subject" no
else say "OK   the branch point says nothing at start about what can be scored from" ok; fi
REFUSALS=$(grep -ca 'Invalid calibration data' /run1451/before.txt || true)
echo "    lines at default level naming the refusal: $REFUSALS"
if [ "$REFUSALS" = "0" ]; then
  say "OK   and not one line at default level names it -- the refusal is log_debug (#1451)" ok
else say "FAIL $REFUSALS lines named the refusal, so it was not silent and the premise is wrong" no; fi

echo
echo "=== 2. STALE: the same board on this binary -- named, and still admitted ==="
grep -aE 'SCORING:|Cached calibration accepted|Scorer running with' /run1451/stale.txt | head -4 || true
if grep -qa 'Cached calibration accepted on 3 of 3 cameras' /run1451/stale.txt; then
  say "OK   still admitted -- #1451 is naming and not refusing, as #1449 was" ok
else say "FAIL the census turned a legal board into a refused one; that is the one thing it must not do" no; fi
if readied stale; then
  say "OK   still beats READY" ok
else say "FAIL a board that was READY before this change is not READY after it" no; fi
if [ "$(censusN stale)" = "2" ]; then
  say "OK   and it says so AT START: 2 of 3 can be scored from, not 3 of 3" ok
else say "FAIL the census says '$(census stale)' on a board with one unscorable camera" no; fi
# #1389: the camera by name and its own reason, and the reason must send the reader to the
# CACHE rather than to the rig -- this camera is looking at the dartboard.
CLINE=$(grep -a 'SCORING: 2 of 3' /run1451/stale.txt | head -1)
NAMED=$(echo "$CLINE" | grep -oE 'camera [0-9]+: ' | wc -l)
if [ "$NAMED" = "3" ]; then
  say "OK   all three cameras named, each with its own reason (#1389)" ok
else say "FAIL the census names $NAMED cameras of 3; a count alone has told nobody anything" no; fi
if echo "$CLINE" | grep -q 'camera 3: calibrated with 21 of the 20 wire boundaries'; then
  say "OK   camera 3 is named with the count that refused it, against the threshold (#1321)" ok
else say "FAIL camera 3's reason does not state its wire count against the threshold" no; fi
if echo "$CLINE" | grep -q 'delete cache/'; then
  say "OK   and the remedy is the cache, not the rig -- this camera IS looking at the board" ok
else say "FAIL the census says what is wrong and not what to do about it" no; fi
# NOT warned: two of three is degraded, not broken, and #1449's threshold is inherited.
if grep -qa 'No camera on this board can be scored from' /run1451/stale.txt; then
  say "FAIL a board with two scorable cameras was warned as though it were broken" no
else say "OK   not warned: some is the ordinary case, and only NONE is the alarming one" ok; fi
echo "    the line, verbatim:"
echo "$CLINE" | sed 's/^/      /'

echo
echo "=== 3. WHOLE: the shipped mocks still calibrate 3 of 3, named and not warned ==="
grep -aE 'SCORING:|Initial calibration completed|Scorer running with' /run1451/whole.txt | head -3 || true
if grep -qa 'Initial calibration completed successfully on 3 of 3 cameras' /run1451/whole.txt; then
  say "OK   mocks/cam_*.mp4 still calibrates 3 of 3" ok
else say "FAIL the shipped mocks no longer calibrate three of three" no; fi
if [ "$(censusN whole)" = "3" ]; then
  say "OK   and all three can be scored from" ok
else say "FAIL the census says '$(census whole)' on the shipped mocks" no; fi
if grep -qa 'No camera on this board can be scored from' /run1451/whole.txt; then
  say "FAIL a healthy board was warned" no
else say "OK   not warned" ok; fi
# The issue's last criterion, measured rather than asserted: no NEW error or warning. The
# census is an INFO line and the WARN fires at zero alone, so a healthy board's ERROR and
# WARN lines must be the ones the branch point already produced on the same footage.
grep -aoE '^\[(ERROR|WARN)\]\[[A-Z_]+\]' /run1451/whole.txt | sort | uniq -c | sed 's/^/    /' || true
if grep -aE '^\[(ERROR|WARN)\]' /run1451/whole.txt | grep -qa 'scored from'; then
  say "FAIL this change put a new ERROR or WARN on a healthy board" no
else say "OK   nothing this issue added reaches ERROR or WARN on a healthy board" ok; fi

echo
echo "=== 4. the census reads the field the SCORER reads, on every phase ==="
# #1451's structural half: `scorePoint`'s guard and the census are one expression now
# (`canScoreAPoint`), so a camera reported scorable at start is by construction one the
# scorer reads. Measured rather than asserted: a camera the census counts out must be one
# that abstains, and the count must never exceed the cameras the board says it sees.
for t in stale whole; do
  C=$(censusN "$t")
  SEEN=$(grep -aoE '(Initial calibration completed successfully|Cached calibration accepted) on [0-9]+ of [0-9]+' /run1451/$t.txt | head -1 | grep -oE '[0-9]+' | head -1)
  echo "    $t: census says $C scorable; the board says it calibrated on $SEEN"
  if [ -z "$C" ] || [ -z "$SEEN" ]; then
    say "FAIL $t: could not read both counts, so they cannot be compared" no
  elif [ "$C" -gt "$SEEN" ]; then
    say "FAIL $t: more cameras reported scorable than calibrated at all" no
  else
    say "OK   $t: the scoring census never exceeds the calibration census" ok
  fi
done
# And the one that is this issue: on the stale board the two counts DISAGREE, which is the
# whole point. A tester in which they always agree would pass on the defect.
if [ "$(censusN stale)" = "2" ] && grep -qa 'Cached calibration accepted on 3 of 3' /run1451/stale.txt; then
  say "OK   stale: the board calibrated on 3 and can be scored from 2 -- the gap is now SAID" ok
else say "FAIL stale: the gap this issue is about is not visible in the two census lines" no; fi

echo "CHECK_RC=$FAILED"
exit $FAILED
