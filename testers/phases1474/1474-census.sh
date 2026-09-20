set -u
# #1474: whether a board really SENDS how many of its cameras a dart is scored from, and
# whether the body it sends is one Turnaus accepts.
#
# THE STATE THIS IS ABOUT. #1343 shipped the platform half on 2026-09-20: Turnaus accepts,
# stores and publishes a per-board camera census and the marking page draws it. No board
# sent it -- `postBeat` posted `{"condition":"<word>"}` and nothing else -- so the feature
# was live and INERT and every board in the field read as unknown. Nothing anywhere failed,
# which is why it needed a tester rather than a bug report.
#
# WHAT IS MEASURED IS THE WIRE, NOT A LOG LINE. Every assertion below reads the BODY that
# arrived at a Turnaus, out of the stub's transcript, because the acceptance criterion is
# what the server receives. A console line saying the right numbers over a body that never
# carried them is precisely the state this issue was filed about.
#
# THE STUB MODELS THE SERVER'S RULES, and that is load-bearing rather than tidiness.
# `App\Autoscoring\CameraReport` is ported into turnaus_stub.py to the comparison: a fourth
# member or a member that is not a whole number is a 422 -- which costs the club the
# board's CONDITION as well as its count -- and a triple no board can be in is DROPPED at
# 200. A stub that wrote down whatever arrived would pass on a body the real server refuses.
#
# FOUR PHASES, AND THE FIRST TWO ARE WHAT MAKE THE OTHERS MEAN ANYTHING:
#
#   NOCAMS   a board whose cameras never open. It cannot count, so it must send the census
#            ABSENT -- not three noughts. Zero scoring cameras and "I do not know" are
#            different claims and Turnaus draws the first as a fault; a board that answered
#            nought here would mark itself broken every time it was starting up.
#   OLD      this same binary with OD_BEAT_CAMERAS=0: the pre-#1474 body, byte for byte.
#            It is the falsifier -- the numbers below must be shown to come from this
#            change rather than from the stub -- and it is also the fleet mid-upgrade,
#            which the server contract has to go on accepting.
#   WHOLE    the shipped mocks, census on. A healthy board reports scoring EQUAL to fitted.
#   REFUSED  mocks/rig-20260918, census on. #1442 refuses a camera proposing more than
#            twenty wire boundaries and cameras 1 and 3 of that fixture propose 21 and 22,
#            so this board really has cameras it cannot be scored from. It must report
#            FEWER scoring than fitted. This is the positive control the acceptance
#            criterion names, and without it WHOLE would pass on a board that reported
#            "all of them" whatever was in front of it.
#
# WHOLE and REFUSED are each other's control in both directions: a tester holding only
# WHOLE would pass on a detector that hardcoded fitted==scoring, and one holding only
# REFUSED would pass on a detector that always under-counted.
#
# Every detector started here can end in #895's fault vigil, which never returns, so each
# is backgrounded and ended by its own recorded pid. Never by pattern.

BIN=/app/build/opendartboard
MOCKS=/app/mocks/cam_1.mp4,/app/mocks/cam_2.mp4,/app/mocks/cam_3.mp4
RIG=/app/mocks/rig-20260918/cam_1.mp4,/app/mocks/rig-20260918/cam_2.mp4,/app/mocks/rig-20260918/cam_3.mp4
NOWHERE=/run1474/no-such-camera-1.mp4,/run1474/no-such-camera-2.mp4,/run1474/no-such-camera-3.mp4
STUB_URL=http://127.0.0.1:8899
TRANSCRIPT=/run1474/transcript.jsonl

await() {
  local file="$1" needle="$2" limit="$3" i=0
  while [ $i -lt "$limit" ]; do
    grep -qaE "$needle" "$file" 2>/dev/null && { echo "$i"; return 0; }
    i=$((i + 1)); sleep 1
  done
  echo "TIMEOUT-$limit"; return 1
}

# ---- preconditions, ASSERTED rather than relied on (#1295 lost two runs to exactly this)
#
# #1449's guard, inherited whole and for its reason: a stale binary does not fail loudly
# here, it fails as the ISSUE -- a transcript with no camera census in it is what the
# defect looks like. Two questions, because they catch different mistakes: the mtime
# catches a tree edited after its last build, the string catches a binary built from some
# OTHER tree, which an mtime can never see.
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
  echo "     and every phase would report a beat with no census -- which is precisely what"
  echo "     the defect being measured looks like. Rebuild and re-run."
  exit 1
fi
if ! strings $BIN | grep -q 'this board states its cameras on every beat'; then
  echo "FAIL $BIN does not carry #1474's beat census at all."
  echo "     It was built from a tree without this change in it. Nothing below would be"
  echo "     measuring this branch. Rebuild and re-run."
  exit 1
fi
if ! strings $BIN | grep -q 'OD_BEAT_CAMERAS'; then
  echo "FAIL $BIN does not carry the OD_BEAT_CAMERAS falsifier, so phase OLD would run"
  echo "     the SAME code as the phases it is supposed to be the control for."
  exit 1
fi
# The fixture the positive control is built from. It is the only shipped footage holding a
# camera the wire stage refuses, so its absence is not a board that counts differently --
# it is a tester with no positive control, which is a tester about nothing.
if [ ! -s /app/mocks/rig-20260918/cam_3.mp4 ] || [ ! -s /app/mocks/rig-20260918/cam_1.mp4 ]; then
  echo "FAIL mocks/rig-20260918/cam_1.mp4 or cam_3.mp4 is missing -- they are the refused"
  echo "     cameras phase REFUSED is built from, and without them there is no board with"
  echo "     fewer scoring cameras than fitted and nothing here is a positive control."
  exit 1
fi
if [ ! -s /app/mocks/cam_1.mp4 ]; then
  echo "FAIL mocks/cam_1.mp4 is missing -- phase WHOLE has no healthy board to measure."
  exit 1
fi
# And the stub really carries the server's rules, or every 200 below is the stub being
# permissive rather than the body being right.
if ! grep -q 'def camera_report' /app/testers/turnaus_stub.py; then
  echo "FAIL testers/turnaus_stub.py does not model CameraReport, so a 200 here would say"
  echo "     nothing about whether Turnaus would accept the body."
  exit 1
fi
echo "--- $BIN carries the beat census and the falsifier, and is newer than /app/src ---"

# ONE stub for the whole script: the credential a pairing issues lives in the stub's
# memory, and a stub restarted between phases answers every heartbeat 401, which reads in
# the transcript as a board that never beat.
STUB_TRANSCRIPT=$TRANSCRIPT STUB_INTERVAL_SECONDS=5 STUB_SILENCE_SECONDS=60 STUB_SAMPLE_SECONDS=0 \
  python3 /app/testers/turnaus_stub.py > /run1474/stub.out 2> /run1474/stub.err &
STUB=$!
sleep 1
if ! kill -0 $STUB 2>/dev/null; then
  echo "FAIL the stub did not start; nothing below could measure a beat"
  sed 's/^/    /' /run1474/stub.err
  exit 1
fi

# The phases share one transcript, so each is a slice of it, marked at both ends -- #1338's
# rule, and it was bought there: a slice open at the top end reads a later phase's beat as
# this phase's.
mark() { wc -l < "$TRANSCRIPT" 2>/dev/null || echo 0; }

# Every beat in a slice, one per line: "<condition> <why> <raw-keys> <fitted> <scoring> <dark>".
# A refused beat is included, and says so, because a census that cost the board its
# condition is the failure mode this whole message must not have.
beats() {
  python3 -c "
import json, sys
start, end = int(sys.argv[2]), int(sys.argv[3])
for n, line in enumerate(open(sys.argv[1])):
    if n < start or n >= end:
        continue
    try: r = json.loads(line)
    except ValueError: continue
    if r.get('event') not in ('beat', 'beat_refused'):
        continue
    raw = r.get('cameras_raw')
    keys = ','.join(sorted(raw)) if isinstance(raw, dict) else '-'
    rep = r.get('cameras') or {}
    print(r.get('event'), r.get('condition', ''), r.get('cameras_why', '?'), keys,
          rep.get('fitted', '-'), rep.get('scoring', '-'), rep.get('dark', '-'))
" "$TRANSCRIPT" "$1" "$2"
}

echo "--- pairing, once, against the stub ---"
$BIN --pair 483920 --turnaus $STUB_URL --allow-plaintext > /run1474/pair.out 2> /run1474/pair.err
PAIR_RC=$?
echo "PAIR_RC=$PAIR_RC"
if [ $PAIR_RC -ne 0 ]; then
  echo "FAIL the board did not pair, so no phase below can beat at all"
  sed 's/^/    /' /run1474/pair.err | tail -10
  kill $STUB 2>/dev/null
  exit 1
fi

# A phase: a tag, a camera list, how long to let it beat, and an environment prefix. Each
# detector is backgrounded, waited on by a sentinel it prints itself, and then ended by its
# own recorded pid. cache/ is cleared every time: this issue is about what a FRESH start
# counts, and a phase inheriting the previous phase's geometry would be measuring that one.
phase() {
  local tag="$1" cams="$2" settle="$3"; shift 3
  local from to pid
  echo "=== $tag ==="
  rm -rf /run1474/cache
  from=$(mark)
  env "$@" $BIN --cams "$cams" --width 1280 --height 720 \
    --turnaus $STUB_URL --allow-plaintext > /run1474/$tag.out 2> /run1474/$tag.err &
  pid=$!
  echo "$tag reached its verdict after $(await /run1474/$tag.out 'Scorer running with|BOARD FAULTED' 150)s"
  sleep "$settle"
  kill -TERM $pid 2>/dev/null; wait $pid 2>/dev/null
  to=$(mark)
  # Colour codes are in every console line; strip them once and read the plain text.
  sed 's/\x1b\[[0-9;]*m//g' /run1474/$tag.out > /run1474/$tag.txt
  beats "$from" "$to" > /run1474/$tag.beats
  echo "    beats in this slice: $(wc -l < /run1474/$tag.beats)"
  sed 's/^/      /' /run1474/$tag.beats | head -6
}

phase nocams  "$NOWHERE" 20
phase old     "$MOCKS"   30 OD_BEAT_CAMERAS=0
phase whole   "$MOCKS"   30
phase refused "$RIG"     30

kill $STUB 2>/dev/null; wait $STUB 2>/dev/null

FAILED=0
say() { echo "$1"; [ "$2" = ok ] || FAILED=1; }

# ---- readers over a phase's beats -------------------------------------------------------
n_beats()    { wc -l < /run1474/$1.beats; }
n_refused()  { grep -c '^beat_refused ' /run1474/$1.beats || true; }
# The LAST stated beat of a phase: the board's settled answer, after calibration has
# finished. An early beat is honestly absent and asserting on it would be asserting on a
# race.
stated()     { grep '^beat ' /run1474/$1.beats | awk '$3 == "stated"' | tail -1; }
n_stated()   { grep '^beat ' /run1474/$1.beats | awk '$3 == "stated"' | wc -l; }
n_absent()   { grep '^beat ' /run1474/$1.beats | awk '$3 == "absent"' | wc -l; }
field()      { stated "$1" | awk -v c="$2" '{print $c}'; }
keys()       { field "$1" 4; }
fitted()     { field "$1" 5; }
scoring()    { field "$1" 6; }
dark()       { field "$1" 7; }

echo
echo "=== 0. every phase beat at all, and NO beat was ever refused ==="
# The first thing to establish, because every assertion below reads beats. And the second
# is the one that would be catastrophic in the field: a 422 costs the club the board's
# CONDITION as well as its census, so a Station's screen would degrade to the keypad in
# front of a working board on the day this shipped.
for t in nocams old whole refused; do
  echo "    $t: $(n_beats $t) beats, $(n_refused $t) refused, $(n_stated $t) stating a census"
  if [ "$(n_beats $t)" -lt 1 ]; then
    say "FAIL $t: no beat reached the stub, so nothing in this phase is measured" no
  elif [ "$(n_refused $t)" != "0" ]; then
    say "FAIL $t: $(n_refused $t) beat(s) REFUSED -- the census cost this board its condition" no
  else
    say "OK   $t: beat, and every beat was accepted" ok
  fi
done

echo
echo "=== 1. OLD: the falsifier -- this binary beating the pre-#1474 body ==="
# The control that makes every number below a statement about this change. Same binary,
# same footage, same stub: only OD_BEAT_CAMERAS=0 differs. If this phase carried a census
# too, the numbers in WHOLE and REFUSED would be the stub's invention rather than the
# board's.
if [ "$(n_stated old)" = "0" ]; then
  say "OK   with OD_BEAT_CAMERAS=0 not one beat carried a camera census" ok
else say "FAIL the falsifier still sent a census on $(n_stated old) beat(s), so it falsifies nothing" no; fi
if [ "$(n_absent old)" -ge 1 ]; then
  say "OK   and the server accepted it: an older board mid-upgrade goes on beating" ok
else say "FAIL no accepted beat with the census absent, which is the fleet's own body" no; fi

echo
echo "=== 2. NOCAMS: a board that cannot count sends ABSENT, never three noughts ==="
# Zero scoring cameras and "I do not know" are different claims and Turnaus draws the
# first as a board in total failure. A board that answered nought while it was starting up
# would mark itself broken on every start.
echo "    $(n_absent nocams) beats absent, $(n_stated nocams) stating a census"
if grep -qa 'the cameras did not open' /run1474/nocams.txt; then
  say "OK   the cameras really did not open, so this board genuinely cannot count" ok
else say "FAIL this phase's board opened cameras, so it is not the board being measured" no; fi
if [ "$(n_stated nocams)" = "0" ] && [ "$(n_absent nocams)" -ge 1 ]; then
  say "OK   it states no census at all rather than fitted=0 or scoring=0" ok
else say "FAIL it stated a census ($(stated nocams)) on a board with no cameras" no; fi

echo
echo "=== 3. WHOLE: a healthy board reports scoring EQUAL to fitted ==="
grep -aE 'SCORING:|Initial calibration completed|this board states its cameras' /run1474/whole.txt | head -3 || true
echo "    last stated beat: $(stated whole)"
if [ -z "$(stated whole)" ]; then
  say "FAIL the healthy board never stated a census -- #1474 is not done" no
else
  say "OK   the healthy board states a census on the beat" ok
  if [ "$(keys whole)" = "dark,fitted,scoring" ]; then
    say "OK   and the body carries exactly fitted, scoring and dark -- the 45.15.0 shape" ok
  else say "FAIL the body carried '$(keys whole)'; a fourth member is a 422 on the real server" no; fi
  if [ "$(fitted whole)" = "3" ]; then
    say "OK   fitted is 3 -- the cameras it was given, stated rather than inferred" ok
  else say "FAIL fitted is '$(fitted whole)' on a board handed three cameras" no; fi
  if [ -n "$(scoring whole)" ] && [ "$(scoring whole)" = "$(fitted whole)" ]; then
    say "OK   scoring equals fitted: nothing is missing and the board says so out loud" ok
  else say "FAIL a healthy board reported $(scoring whole) of $(fitted whole) scoring" no; fi
  if [ "$(dark whole)" = "0" ]; then
    say "OK   and none of them is dark" ok
  else say "FAIL $(dark whole) cameras reported dark on footage that all answered" no; fi
fi

echo
echo "=== 4. REFUSED: a board with a refused camera reports FEWER scoring than fitted ==="
# The acceptance criterion's other half. #1442 refuses a camera proposing more than twenty
# wire boundaries; cameras 1 and 3 of this fixture propose 21 and 22. Those cameras are
# FITTED and they are not SCORING, and the census has to say so rather than counting them.
grep -aE 'did not calibrate: the wire stage found|SCORING:|BOARD FAULTED' /run1474/refused.txt | head -4 || true
echo "    last stated beat: $(stated refused)"
if grep -qa 'wire boundaries where a board has 20' /run1474/refused.txt; then
  say "OK   a camera really was refused at calibration on this fixture (#1442)" ok
else say "FAIL no camera was refused here, so this phase is not a positive control at all" no; fi
if [ -z "$(stated refused)" ]; then
  say "FAIL the degraded board stated no census -- which is the state somebody most needs it in" no
else
  say "OK   the degraded board states a census too, whatever it says it can see" ok
  if [ "$(fitted refused)" = "3" ]; then
    say "OK   fitted is still 3: a refused camera is one the board HAS" ok
  else say "FAIL fitted is '$(fitted refused)'; a refused camera must not leave the denominator" no; fi
  if [ -n "$(scoring refused)" ] && [ "$(scoring refused)" -lt "$(fitted refused)" ]; then
    say "OK   scoring ($(scoring refused)) is FEWER than fitted ($(fitted refused)) -- the refused cameras are not counted" ok
  else say "FAIL the board reported $(scoring refused) of $(fitted refused): a refused camera was counted as scoring" no; fi
fi

echo
echo "=== 5. and the two boards do not read the same, which is the whole point ==="
# #1343's own rule, inherited: an assertion that a degraded board publishes a count would
# pass on a detector that published the same count for every board. So the two must differ,
# measured against each other rather than against a literal.
echo "    whole:   fitted=$(fitted whole) scoring=$(scoring whole) dark=$(dark whole)"
echo "    refused: fitted=$(fitted refused) scoring=$(scoring refused) dark=$(dark refused)"
if [ -n "$(scoring whole)" ] && [ -n "$(scoring refused)" ] && [ "$(scoring whole)" != "$(scoring refused)" ]; then
  say "OK   a whole board and a degraded one report different numbers of scoring cameras" ok
else say "FAIL both boards reported the same count, so the number says nothing about the board" no; fi

echo
echo "=== 6. the arithmetic Turnaus asks of the triple holds on every stated beat ==="
# scoring + dark <= fitted, and fitted within 1..16. A triple outside that is DROPPED by
# the server at 200 -- so a board sending one would beat happily for ever while its page
# read unknown and nothing anywhere failed, which is #1474 one level down.
BAD=$(cat /run1474/*.beats | awk '$3 == "stated" { if ($5 < 1 || $5 > 16 || $6 + $7 > $5) print }' | wc -l)
DROPPED=$(cat /run1474/*.beats | grep -c 'arithmetic no board can be in' || true)
echo "    stated beats whose three numbers disagree: $BAD; dropped by the server: $DROPPED"
if [ "$BAD" = "0" ] && [ "$DROPPED" = "0" ]; then
  say "OK   every census this board stated is one the server stored rather than dropped" ok
else say "FAIL $BAD impossible triples stated and $DROPPED dropped by the server" no; fi

echo
echo "=== 7. nothing about the MACHINE went onto the wire with the numbers ==="
# `cameras` is a CLOSED object on the server: a fourth member is a 422. This is the sweep
# that catches a later widening -- the machine's serial, a maker's name, a credential --
# before it takes a club's board off its own page.
STRAY=$(cat /run1474/*.beats | awk '$4 != "-" && $4 != "dark,fitted,scoring" { print $4 }' | sort -u)
if [ -z "$STRAY" ]; then
  say "OK   every census on the wire carried exactly the three members and no fourth" ok
else say "FAIL a census carried members the server refuses: $STRAY" no; fi

echo "CHECK_RC=$FAILED"
exit $FAILED
