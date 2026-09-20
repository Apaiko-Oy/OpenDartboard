set -u
# #1449: whether this board says, AT START, how many of its cameras can be read for a
# wedge -- and whether saying it turned a legal board into a refused one.
#
# THE BOARD THIS IS ABOUT. `scorePoint` reads a wedge only where
# `anchored && wedge20WireIndex >= 0`; everywhere else it takes #1346's asserted-twenty
# path and answers `dartboard_numbers[0]` for every tip on any ring, which is correct.
# Nothing in the startup census asked that field. So a board on which NO camera can be
# read was admitted, beat READY, logged "Initial calibration completed successfully on
# 3 of 3 cameras", and then gave every dart the same number -- the maintainer's first
# live scoring run (#1363).
#
# HOW THE BOARD IS BUILT, AND WHY NO SWITCH. On the shipped mocks exactly one camera --
# cam_2 -- is the star camera, so the three mocks together are the ORDINARY case: one
# camera readable of three. Neither cam_1 nor cam_3 is a star camera, so three slots
# filled from those two files alone is a board on which no camera can be read, built out
# of shipped footage with no instrumentation, no patched binary and no flag. That is
# worth more than a switch would be: a hatch proves a board the program can be TOLD to
# pretend to be, and this proves one it really becomes.
#
# A two-camera board was the first attempt and it is not this board: measured on this
# binary, cam_1+cam_3 is refused by `whyNoEventIsPossible` ("running 2 cameras and motion
# detection only initialises on 3") before anchoring is ever reached. So the unanchored
# board has to have three slots.
#
# Four phases, and the first is what makes the other three mean anything:
#
#   BEFORE  the branch point (1a8a57a), compiled here, on the unanchored board. It must
#           calibrate, beat READY and publish darts -- and say NOTHING about anchoring
#           until a dart has already gone out. That is the issue's whole claim and
#           nothing in the repository demonstrated it.
#   N       this binary, the SAME board. It must still calibrate, still beat READY and
#           still score -- naming is not refusing, and an unanchored camera is legal
#           (#1363) -- and it must say 0 of 3 at start, name every camera's reason
#           (#1389) and WARN.
#   S       this binary, the three shipped mocks: 1 of 3 readable. Named, and NOT warned.
#           Without S the WARN could be one that fires on every board, and "the board is
#           fine" would read as an alarm every night until nobody read it.
#   A       this binary, the same three mocks with OD_CAMERA_WEDGES stating the other
#           two: 3 of 3. Named, not warned, and the configured anchor says so in its own
#           words rather than in the star camera's.
#
# Every detector started here can end in #895's fault vigil, which never returns, so each
# is backgrounded and ended by its own recorded pid. Never by pattern.

BIN=/app/build/opendartboard
MOCKS=/app/mocks/cam_1.mp4,/app/mocks/cam_2.mp4,/app/mocks/cam_3.mp4
# Three slots, not one of them a star camera. The repetition is the point, not an oversight.
UNANCHORED=/app/mocks/cam_1.mp4,/app/mocks/cam_3.mp4,/app/mocks/cam_1.mp4
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
# "Rebuild before you measure" is advice, and advice that can be skipped will be: this
# very tester was first run against a build/opendartboard 7 minutes older than the source
# it was supposed to be measuring, and phase NONE duly reported that the census said
# nothing -- which is exactly what the defect looks like. A stale binary does not fail
# loudly here, it fails as the ISSUE, which is the worst way for it to fail.
#
# So the harness refuses instead, in #565's shape: the mistake cannot land green. Two
# questions, because they catch different mistakes. The mtime catches a tree edited after
# its last build; the string catches a binary built from some OTHER tree, which an mtime
# can never see.
if [ ! -x $BIN ]; then
  echo "FAIL no $BIN -- a fresh worktree has none, and phase 4 of i1330_run.sh needs it too."
  echo "     Build it:  make build   (or run testers/run_all.sh, which builds first)"
  exit 1
fi
NEWEST=$(find /app/src /app/CMakeLists.txt -newer $BIN 2>/dev/null | head -5)
if [ -n "$NEWEST" ]; then
  echo "FAIL $BIN is OLDER than the source it is supposed to be measuring:"
  echo "$NEWEST" | sed 's/^/       /'
  echo "     Every phase below would measure a binary without this tree's changes in it,"
  echo "     and phase NONE would report the census saying nothing -- which is precisely"
  echo "     what the defect being measured looks like. Rebuild and re-run."
  exit 1
fi
# The census line is what this whole tester is about, so a binary that does not carry it
# is not a binary this tester can say anything about -- green or red.
if ! strings $BIN | grep -q 'cameras can be read for a wedge'; then
  echo "FAIL $BIN does not carry #1449's census line at all."
  echo "     It was built from a tree without this change in it. Nothing below would be"
  echo "     measuring this branch. Rebuild and re-run."
  exit 1
fi
echo "--- $BIN carries the census and is newer than every source under /app/src ---"

# ---- the branch point, compiled offline against this build's fetched sources ----------
# #1317's spelling: FetchContent cannot reach the network in here, and it does not need to
# -- /app/build/_deps holds what this tree already fetched, and the parent needs the same
# two at the same tags.
BASE_SRC=/run1449/pre-1449
BASE_BUILD=/run1449/build-before
if [ ! -f $BASE_SRC/CMakeLists.txt ]; then
  echo "FAIL no base source at $BASE_SRC -- the harness unpacks it"
  exit 1
fi
echo "--- compiling the branch point for phase BEFORE ---"
cmake -S $BASE_SRC -B $BASE_BUILD -DCMAKE_PREFIX_PATH=/usr/local \
  -DCMAKE_CXX_FLAGS="-DDEBUG_SEEK_VIDEO -DDEBUG_VIA_VIDEO_INPUT" \
  -DAPP_VERSION=0.0.0-dev \
  -DFETCHCONTENT_SOURCE_DIR_NLOHMANN_JSON=/app/build/_deps/nlohmann_json-src \
  -DFETCHCONTENT_SOURCE_DIR_HTTPLIB=/app/build/_deps/httplib-src > /run1449/before_cmake.log 2>&1 || {
  tail -20 /run1449/before_cmake.log; exit 1; }
cmake --build $BASE_BUILD -- -j4 --no-print-directory > /run1449/before_build.log 2>&1 || {
  tail -30 /run1449/before_build.log; exit 1; }
BEFORE_BIN=$BASE_BUILD/opendartboard
ls -l $BEFORE_BIN | sed 's/^/    /'

# ONE stub for the whole script: the credential a pairing issues lives in the stub's
# memory, and a stub restarted between phases answers every heartbeat 401, which reads in
# the transcript as a board that never beat.
TRANSCRIPT=/run1449/transcript.jsonl
STUB_TRANSCRIPT=$TRANSCRIPT STUB_INTERVAL_SECONDS=5 STUB_SILENCE_SECONDS=60 STUB_SAMPLE_SECONDS=0 \
  python3 /app/testers/turnaus_stub.py > /run1449/stub.out 2> /run1449/stub.err &
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
$BIN --pair 483920 --turnaus $STUB_URL --allow-plaintext > /run1449/pair.out 2> /run1449/pair.err
echo "PAIR_RC=$?"

# A phase: a binary, a camera list, an environment prefix, a tag. Each detector is
# backgrounded, waited on by a sentinel it prints itself, given time to publish darts and
# then ended by its own recorded pid.
phase() {
  local tag="$1" bin="$2" cams="$3"; shift 3
  local from to pid
  echo "=== $tag ==="
  # cache/ is relative to the cwd, and every phase here must MEASURE rather than inherit
  # the phase before it. #1449 is about a fresh calibration's census; the cached path
  # reaches the same line and is not what this tester is arguing about.
  rm -rf /run1449/cache
  from=$(mark)
  env "$@" $bin --cams "$cams" --width 1280 --height 720 \
    --turnaus $STUB_URL --allow-plaintext > /run1449/$tag.out 2> /run1449/$tag.err &
  pid=$!
  echo "$tag reached its verdict after $(await /run1449/$tag.out 'Scorer running with|BOARD FAULTED' 150)s"
  sleep 30
  kill -TERM $pid 2>/dev/null; wait $pid 2>/dev/null
  to=$(mark)
  echo "${tag}_BEATS=$(beats "$from" "$to" | sort | uniq -c | tr '\n' ' ')"
  # Colour codes are in every console line; strip them once and read the plain text.
  sed 's/\x1b\[[0-9;]*m//g' /run1449/$tag.out > /run1449/$tag.txt
  beats "$from" "$to" > /run1449/$tag.beats
}

phase before "$BEFORE_BIN" "$UNANCHORED"
phase none   "$BIN"        "$UNANCHORED"
phase some   "$BIN"        "$MOCKS"
phase all    "$BIN"        "$MOCKS" OD_CAMERA_WEDGES=12,0,7

kill $STUB 2>/dev/null; wait $STUB 2>/dev/null

FAILED=0
say() { echo "$1"; [ "$2" = ok ] || FAILED=1; }
readied() { grep -qx READY /run1449/$1.beats; }

echo
echo "=== 1. BEFORE: the board this issue is about, on the binary that has the defect ==="
grep -aE 'calibration completed|calibration failed|BOARD FAULTED|Scorer running with' /run1449/before.txt | head -4 || true
if grep -qa 'Initial calibration completed successfully on 3 of 3 cameras' /run1449/before.txt; then
  say "OK   a board no camera can be read on is ADMITTED, three of three" ok
else say "FAIL the unanchored board did not calibrate, so nothing below is about it" no; fi
if readied before; then
  say "OK   and it beats READY, so the Station shows a board ready to be thrown at" ok
else say "FAIL it never beat READY, so this phase is not the state the issue describes" no; fi
# The claim, measured: every dart it publishes is the asserted 20, on any ring.
BEFORE_DARTS=$(grep -ca 'BOARD: wedge by default' /run1449/before.txt || true)
BEFORE_MEASURED=$(grep -ca 'BOARD: wedge measured' /run1449/before.txt || true)
echo "    darts published: $BEFORE_DARTS by default, $BEFORE_MEASURED measured"
grep -aoE 'BOARD: wedge by default \| ring=[a-z]+ \| segment=[0-9]+' /run1449/before.txt | sort | uniq -c | sed 's/^/    /'
if [ "$BEFORE_DARTS" -gt 0 ]; then
  say "OK   it publishes darts, and every one of them by default" ok
else say "FAIL it published no dart at all, so the asserted 20 is not shown here" no; fi
if [ "$BEFORE_MEASURED" = "0" ]; then
  say "OK   not one dart had its wedge read -- no camera on this board can be" ok
else say "FAIL $BEFORE_MEASURED darts were read, so this board is not unanchored" no; fi
NOT_TWENTY=$(grep -aoE 'BOARD: wedge by default \| ring=[a-z]+ \| segment=[0-9]+' /run1449/before.txt \
  | grep -cvE 'segment=20$' || true)
if [ "$BEFORE_DARTS" -gt 0 ] && [ "$NOT_TWENTY" = "0" ]; then
  say "OK   and every one of them is segment 20, whatever ring it landed in (#1346)" ok
else say "FAIL $NOT_TWENTY published darts were not the asserted 20" no; fi
# The half this issue is filed about: WHEN the operator is told.
if grep -qa 'ORIENTATION: .* cameras can be read for a wedge' /run1449/before.txt; then
  say "FAIL the branch point already said it at start; this issue has no subject" no
else say "OK   nothing at start said a word about it -- this is the silence #1449 names" ok; fi
FIRST_SAY=$(grep -na 'BOARD: wedge by default' /run1449/before.txt | head -1 | cut -d: -f1)
FIRST_READY=$(grep -na 'Scorer running with' /run1449/before.txt | head -1 | cut -d: -f1)
if [ -n "$FIRST_SAY" ] && [ -n "$FIRST_READY" ] && [ "$FIRST_SAY" -gt "$FIRST_READY" ]; then
  say "OK   the first mention of it is line $FIRST_SAY, after the board went READY on line $FIRST_READY -- a dart had already been published" ok
else say "FAIL could not locate the first mention against the READY line" no; fi

echo
echo "=== 2. NONE: the same board on this binary -- named, warned, and still admitted ==="
grep -aE 'ORIENTATION:|No camera on this board|calibration completed|Scorer running with' /run1449/none.txt | head -5 || true
if grep -qa 'Initial calibration completed successfully on 3 of 3 cameras' /run1449/none.txt; then
  say "OK   still admitted -- #1449 is naming and not refusing (#1363)" ok
else say "FAIL the census turned a legal board into a refused one; that is the one thing it must not do" no; fi
if readied none; then
  say "OK   still beats READY, so the rig OD_CAMERA_WEDGES was written for still works" ok
else say "FAIL a board that was READY before this change is not READY after it" no; fi
if [ "$(grep -ca 'BOARD: wedge by default' /run1449/none.txt || true)" -gt 0 ]; then
  say "OK   still scores -- scoring behaviour is #1346's and is untouched" ok
else say "FAIL it stopped publishing darts" no; fi
if grep -qa 'ORIENTATION: 0 of 3 cameras can be read for a wedge' /run1449/none.txt; then
  say "OK   and it says so AT START: 0 of 3" ok
else say "FAIL the census does not name zero on a board where no camera can be read" no; fi
if grep -qa 'No camera on this board can be read for a wedge, so every dart will publish as an asserted 20' /run1449/none.txt; then
  say "OK   at WARN, naming the consequence rather than the arithmetic (#1338's shape)" ok
else say "FAIL zero readable cameras is not a warning" no; fi
# #1389: each camera and its reason, never just a count -- and in the WARN itself, because
# an operator filtering to WARN and ERROR must see the whole sentence or none of it.
WARNLINE=$(grep -a 'No camera on this board can be read' /run1449/none.txt | head -1)
NAMED=$(echo "$WARNLINE" | grep -oE 'camera [0-9]+: ' | wc -l)
if [ "$NAMED" = "3" ]; then
  say "OK   all three cameras named in the warning itself, each with its own reason (#1389)" ok
else say "FAIL the warning names $NAMED cameras of 3; a count alone has told nobody anything" no; fi
if echo "$WARNLINE" | grep -q 'OD_CAMERA_WEDGES'; then
  say "OK   and it names the remedy, which is what start is the right moment for" ok
else say "FAIL the warning says what is wrong and not what to do about it" no; fi
echo "    the line, verbatim:"
echo "$WARNLINE" | sed 's/^/      /'

echo
echo "=== 3. SOME: the ordinary board is named and is NOT warned ==="
grep -aE 'ORIENTATION:|No camera on this board' /run1449/some.txt | head -3 || true
if grep -qa 'ORIENTATION: 1 of 3 cameras can be read for a wedge' /run1449/some.txt; then
  say "OK   the shipped mocks read 1 of 3 -- the star camera, and only it" ok
else say "FAIL the ordinary board's census is not 1 of 3" no; fi
if grep -qa 'No camera on this board can be read' /run1449/some.txt; then
  say "FAIL a board with a readable camera was warned as though it were broken" no
else say "OK   not warned: some is the ordinary case, and only NONE is the alarming one" ok; fi
if grep -qa 'camera 2: anchored by its own star-pattern measurement' /run1449/some.txt; then
  say "OK   the readable camera is named as measured, in its own words" ok
else say "FAIL the star camera is not named as anchored by measurement" no; fi
UNREAD=$(grep -a 'ORIENTATION: 1 of 3' /run1449/some.txt | head -1 | grep -oE 'camera [0-9]+: ' | wc -l)
if [ "$UNREAD" = "3" ]; then
  say "OK   and the two that cannot be read are named too, with their own reasons" ok
else say "FAIL the census names $UNREAD cameras of 3" no; fi
echo "    the line, verbatim:"
grep -a 'ORIENTATION: 1 of 3' /run1449/some.txt | head -1 | sed 's/^/      /'

echo
echo "=== 4. ALL: an operator-stated anchor is named as stated, and is not warned ==="
grep -aE 'ORIENTATION:|anchored by configuration|No camera on this board' /run1449/all.txt | head -5 || true
if grep -qa 'ORIENTATION: 3 of 3 cameras can be read for a wedge' /run1449/all.txt; then
  say "OK   with the other two stated, the whole board is readable" ok
else say "FAIL OD_CAMERA_WEDGES did not move the census" no; fi
if grep -qa 'No camera on this board can be read' /run1449/all.txt; then
  say "FAIL a fully anchored board was warned" no
else say "OK   not warned" ok; fi
if grep -qa 'camera 1: anchored by configuration, wedge 12' /run1449/all.txt \
   && grep -qa 'camera 3: anchored by configuration, wedge 7' /run1449/all.txt; then
  say "OK   a stated anchor says it is stated, and which wedge was stated (#1363)" ok
else say "FAIL a configured anchor is reported in the star camera's words" no; fi
echo "    the line, verbatim:"
grep -a 'ORIENTATION: 3 of 3' /run1449/all.txt | head -1 | sed 's/^/      /'

echo
echo "=== 5. the census reads the field the SCORER reads, on every phase ==="
# #1449's structural half: `wedge_measured` and the census are one expression now, so a
# camera reported readable at start is by construction one the scorer reads. Measured
# rather than asserted: the count in the census line must equal whether darts were read.
for t in none some all; do
  C=$(grep -aoE 'ORIENTATION: [0-9]+ of [0-9]+ cameras can be read' /run1449/$t.txt | head -1 | grep -oE '[0-9]+' | head -1)
  M=$(grep -ca 'BOARD: wedge measured' /run1449/$t.txt || true)
  D=$(grep -ca 'BOARD: wedge by default' /run1449/$t.txt || true)
  echo "    $t: census says $C readable; darts read=$M, asserted=$D"
  if [ "$C" = "0" ] && [ "$M" != "0" ]; then
    say "FAIL $t: the census said no camera could be read and $M darts were read anyway" no
  elif [ "$C" != "0" ] && [ "$D" != "0" ] && [ "$M" = "0" ]; then
    say "FAIL $t: the census said $C readable and not one dart was read" no
  else
    say "OK   $t: the census and the darts agree" ok
  fi
done

echo "CHECK_RC=$FAILED"
exit $FAILED
