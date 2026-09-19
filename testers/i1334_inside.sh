set -u
# #1334, inside the container: a board with no network at all.
#
# The container is --network none, so loopback is the only interface there is. The stub
# stands in for the club's Turnaus while the board is paired, and is then killed -- which
# is the board being carried to a venue whose wifi does not work. Nothing after that line
# can reach anything.
#
# It exits non-zero on its own assertions. The numbers are floors, not fingerprints: this
# tester is a control for a unit-file change and must not go red because a detector
# somewhere above it scored one dart fewer.
RUN=/run1334
CFG=/root/.config/opendartboard
FAILED=0
fail() { echo "FAIL $*"; FAILED=1; }
ok() { echo "ok   $*"; }

# The premise, asserted rather than decorated. This read /proc/net/dev from the first run:
# `ip -o addr show` printed nothing at all in $OD_IMAGE, which has no ip(8), and a line
# that says nothing is worse here than no line -- it reads as "no interfaces" whatever the
# container was given.
echo "--- the interfaces this board has ---"
IFACES=$(awk 'NR > 2 { sub(/:$/, "", $1); print $1 }' /proc/net/dev | sort | tr '\n' ' ')
echo "     $IFACES"
[ "$(echo "$IFACES" | xargs)" = "lo" ] \
  && ok "there really is no network: loopback and nothing else" \
  || fail "this container has more than loopback ($IFACES), so nothing below is about a board with no network"

echo
echo "--- phase 1: paired at the club, while there is something to pair with ---"
export STUB_TRANSCRIPT=$RUN/transcript.jsonl
export STUB_INTERVAL_SECONDS=15
export STUB_SILENCE_SECONDS=60
export STUB_SAMPLE_SECONDS=1
python3 /app/testers/turnaus_stub.py > "$RUN/stub.out" 2> "$RUN/stub.err" &
STUB=$!
sleep 1
/app/build/opendartboard --pair 483920 --turnaus http://127.0.0.1:8899 --allow-plaintext \
  > "$RUN/pair.out" 2> "$RUN/pair.err"
PAIR_RC=$?
echo "PAIR_RC=$PAIR_RC"
kill $STUB 2> /dev/null
wait $STUB 2> /dev/null
[ "$PAIR_RC" = 0 ] || fail "the board could not be paired, so nothing below it is about a paired board"

# The needle has to be proved absent before its absence means anything (#708 on the other
# side): there must be no spool yet, or a full one later says nothing.
BEFORE=0
[ -f "$CFG/owed.jsonl" ] && BEFORE=$(wc -l < "$CFG/owed.jsonl")
echo "spool before the networkless run: $BEFORE records"
[ "$BEFORE" = 0 ] || fail "the spool was not empty before the run that is supposed to fill it"

echo
echo "--- phase 2: 900 cycles with nothing to push to and no interface to reach it on ---"
# No --turnaus: it uses the address it was paired to, where nothing is listening any more.
OD_MAX_CYCLES=900 /app/build/opendartboard --debug \
  --cams /app/mocks/cam_1.mp4,/app/mocks/cam_2.mp4,/app/mocks/cam_3.mp4 \
  --width 1280 --height 720 --allow-plaintext \
  > "$RUN/gone.out" 2> "$RUN/gone.err"
GONE_RC=$?
echo "PROGRAM_RC=$GONE_RC"
cat "$RUN/gone.out" "$RUN/gone.err" > "$RUN/gone.all"

# 1. It still starts, and stays started. A board that exits here is a board the restart
#    stanza this issue is about would be looping on.
[ "$GONE_RC" = 0 ] \
  && ok "it still starts and runs to the end of its cycles with no network (rc=0)" \
  || fail "the detector exited $GONE_RC with no network"

# 2. It still opens its cameras. "Capturing frames for calibration" is logged only after
#    capture->open() has returned true, and "Failed to initialize cameras" is the other
#    branch of the same if.
if grep -q "Capturing frames for calibration" "$RUN/gone.all" \
   && ! grep -q "Failed to initialize cameras" "$RUN/gone.all"; then
  ok "it still opens its cameras with no network"
else
  fail "the cameras did not open"
  grep -iE "camera|calibrat" "$RUN/gone.all" | head -5 | sed 's/^/     | /'
fi

# 3. It still sees darts. Without this the spool below could be empty for an innocent
#    reason and the test would still look like it measured something.
SCORES=$(grep -cE "^SCORE: |SCORE: [A-Z0-9]+ \| Position:" "$RUN/gone.all" || true)
if [ "${SCORES:-0}" -ge 1 ]; then
  ok "it still scores: $SCORES SCORE lines off the mocks"
else
  fail "no dart was scored with no network, so 'it still spools' has nothing to be about"
fi

# 4. It still spools.
AFTER=0
[ -f "$CFG/owed.jsonl" ] && AFTER=$(wc -l < "$CFG/owed.jsonl")
echo "spool after the networkless run: $AFTER records"
if [ "$AFTER" -gt "$BEFORE" ]; then
  ok "it still spools: $AFTER owed push(es) written while nothing was reachable"
else
  fail "nothing was spooled, so a networkless board silently lost what it saw"
fi
ls -l "$CFG" 2> /dev/null | sed 's/^/     | /'

echo
echo "--- phase 3: the venue's wifi works after all; the spool is what it was for ---"
# The positive control for phase 2's fourth assertion. A spool that grows and can never be
# drained is not the behaviour being preserved, and a spool that grows because the client
# always spools would look identical from phase 2 alone.
python3 /app/testers/turnaus_stub.py > "$RUN/stub2.out" 2> "$RUN/stub2.err" &
STUB2=$!
sleep 1
OD_MAX_CYCLES=60 /app/build/opendartboard --debug \
  --cams /app/mocks/cam_1.mp4,/app/mocks/cam_2.mp4,/app/mocks/cam_3.mp4 \
  --width 1280 --height 720 --allow-plaintext \
  > "$RUN/resume.out" 2> "$RUN/resume.err"
echo "RESUME_RC=$?"
sleep 1
kill $STUB2 2> /dev/null
wait $STUB2 2> /dev/null
cat "$RUN/resume.out" "$RUN/resume.err" > "$RUN/resume.all"
RESUMED=$(grep -o "resumed [0-9]* owed push" "$RUN/resume.all" | head -1 || true)
if [ -n "$RESUMED" ]; then
  ok "the spool drained once there was somewhere to push: \"$RESUMED(es) from the spool\""
else
  fail "the spool was never resumed, so what phase 2 wrote is not what a board recovers from"
  grep -i "TURNAUS:" "$RUN/resume.all" | head -5 | sed 's/^/     | /'
fi

echo
[ "$FAILED" = 0 ] && echo "i1334_inside: a board with no network starts, sees, and spools"
exit "$FAILED"
