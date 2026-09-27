# An ASSERTING phase (#1463). A board paired to its club takes an evening's six digits, and
# the darts it scores go into that Contest; a second process still holds the evening.
#
# Its exit status is the verdict at the bottom and nothing else: 0 only when both pairings
# and both detector runs exited 0, the scoring run reached its cycle budget, darts were
# counted into the Contest and none went out of the club's door, the evening was never
# given up or refused, both bindings are still in the credential file after the restart,
# and the restarted process announced the Contest. Until #1463 it ended on
# `grep -o "[i803]..."`, so a board that sent every dart to the wrong door was green.
#
# Nothing else reads what it leaves in /run891: no tester, harness or document consumes its
# transcript (grepped for phases891, run891 and the 891 runs directory, #1463), which is why
# it asserts rather than records. The transcript is still printed above the verdict for a
# reader who wants to see why.
set -u
export STUB_TRANSCRIPT=/run891/transcript.jsonl
export STUB_INTERVAL_SECONDS=15
export STUB_SILENCE_SECONDS=60
export STUB_SAMPLE_SECONDS=0
python3 /app/testers/turnaus_stub.py > /run891/stub.out 2> /run891/stub.err &
STUB=$!
sleep 1
echo "--- phase 1: the club pairs the board, months ago ---"
/app/build/opendartboard --pair 483920 --turnaus http://127.0.0.1:8899 --allow-plaintext \
  > /run891/pair.out 2> /run891/pair.err
PAIR_RC=$?; export PAIR_RC; echo "PAIR_RC=$PAIR_RC"
echo "--- phase 2: somebody opens a Casual Contest and hands the board its six digits ---"
/app/build/opendartboard --pair-contest 571643 --turnaus http://127.0.0.1:8899 --allow-plaintext \
  > /run891/paircontest.out 2> /run891/paircontest.err
PAIR_CONTEST_RC=$?; export PAIR_CONTEST_RC; echo "PAIR_CONTEST_RC=$PAIR_CONTEST_RC"
ls -l /root/.config/opendartboard/credentials.json
python3 -c "
import json
d=json.load(open('/root/.config/opendartboard/credentials.json'))
print('keys:', sorted(d.keys()))
print('contest keys:', sorted(d['contest'].keys()))
print('contest id:', d['contest']['casual_contest_id'], 'board:', d['contest']['board_id'])
print('organisation binding still present:', bool(d.get('token')))
"
echo "--- phase 3: 1100 cycles, the darts go into the Contest ---"
OD_MAX_CYCLES=1100 /app/build/opendartboard --debug \
  --cams /app/mocks/cam_1.mp4,/app/mocks/cam_2.mp4,/app/mocks/cam_3.mp4 \
  --width 1280 --height 720 --allow-plaintext \
  > /run891/run.out 2> /run891/run.err
PROGRAM_RC=$?; export PROGRAM_RC; echo "PROGRAM_RC=$PROGRAM_RC"
echo "--- phase 4: a SECOND process, to prove the binding survived a restart ---"
OD_MAX_CYCLES=40 /app/build/opendartboard --debug \
  --cams /app/mocks/cam_1.mp4,/app/mocks/cam_2.mp4,/app/mocks/cam_3.mp4 \
  --width 1280 --height 720 --allow-plaintext \
  > /run891/restart.out 2> /run891/restart.err
RESTART_RC=$?; export RESTART_RC; echo "RESTART_RC=$RESTART_RC"
sleep 2
kill $STUB 2>/dev/null; wait $STUB 2>/dev/null
echo "--- what arrived, at which door ---"
python3 - <<'PY'
import json, collections
rows=[json.loads(l) for l in open('/run891/transcript.jsonl')]
print(collections.Counter(r['event'] for r in rows))
for r in rows:
    if r['event'] in ('casual_counted','casual_takeout','counted','takeout','absorbed'):
        print('%-16s %-5s %-28s %s' % (r['event'], r.get('sector',''), r.get('reference',''),
                                       r.get('round', r.get('closed'))))
PY
echo "--- the control ---"
grep -o "SCORE: .*" /run891/run.out > /run891/run.scores
wc -l < /run891/run.scores
cat /run891/run.scores
grep -o "\[i803\].*" /run891/run.out || true
echo "--- the verdict ---"
python3 - <<'PY'
import json, os, sys
fails = []
def must(ok, what):
    print(('HELD   ' if ok else 'BROKEN ') + what)
    if not ok:
        fails.append(what)
def rc(name):
    return os.environ.get(name, 'unset')
def text(*names):
    out = ''
    for n in names:
        try:
            out += open('/run891/' + n, errors='replace').read()
        except OSError:
            pass
    return out
rows = [json.loads(l) for l in open('/run891/transcript.jsonl')]
events = [r['event'] for r in rows]
creds = json.load(open('/root/.config/opendartboard/credentials.json'))
for n in ('PAIR_RC', 'PAIR_CONTEST_RC', 'PROGRAM_RC', 'RESTART_RC'):
    must(rc(n) == '0', '%s is 0 (%s)' % (n, rc(n)))
must('[i803] cycle budget reached' in text('run.out'), 'the scoring run reached its cycle budget')
must(events.count('casual_counted') >= 1, 'darts were counted into the Contest (%d)' % events.count('casual_counted'))
must(events.count('counted') == 0, 'no dart went out of the club door while the Contest was held (%d)' % events.count('counted'))
must('casual_given_up' not in events and 'casual_refused' not in events, 'the evening was never given up or refused')
must(bool(creds.get('token')) and isinstance(creds.get('contest'), dict),
     'both bindings are in the credential file after the restart (%s)' % sorted(creds))
must('bound to Casual Contest 12' in text('restart.out', 'restart.err'), 'the restarted process announced the Contest')
print('VERDICT: %s' % ('held' if not fails else 'BROKEN on %d of the above' % len(fails)))
sys.exit(1 if fails else 0)
PY
# The verdict's own status, and deliberately the last thing this script does (#1412).
exit $?
