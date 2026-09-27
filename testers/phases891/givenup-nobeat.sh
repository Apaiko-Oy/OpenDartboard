# An ASSERTING phase (#1463). givenup.sh on a deployment that does not serve the Casual
# heartbeat, so the board cannot learn of the give-up from a beat: it has to learn it from
# the next push (a dart or a takeout) the Contest refuses.
#
# Its exit status is the verdict at the bottom and nothing else: 0 only when both pairings
# and both detector runs exited 0, the scoring run reached its cycle budget, no beat was
# served, the stub gave the evening up, the board ended the binding on a refused PUSH
# (`HTTP 401)`, not `on a heartbeat`), darts after that went out of the club's door, the
# credential file afterwards holds the club token and no `contest`, and the restarted
# process did not announce the Contest. Until #1463 it ended on `grep -o "[i803]..."`.
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
export STUB_GIVE_UP_AFTER=3
export STUB_CASUAL_BEAT=0
python3 /app/testers/turnaus_stub.py > /run891/stub.out 2> /run891/stub.err &
STUB=$!
sleep 1
/app/build/opendartboard --pair 483920 --turnaus http://127.0.0.1:8899 --allow-plaintext > /run891/pair.out 2> /run891/pair.err
PAIR_RC=$?; export PAIR_RC; echo "PAIR_RC=$PAIR_RC"
/app/build/opendartboard --pair-contest 571643 --turnaus http://127.0.0.1:8899 --allow-plaintext > /run891/paircontest.out 2> /run891/paircontest.err
PAIR_CONTEST_RC=$?; export PAIR_CONTEST_RC; echo "PAIR_CONTEST_RC=$PAIR_CONTEST_RC"
echo "--- a club board on somebody's evening: the third dart ends it ---"
OD_MAX_CYCLES=1100 /app/build/opendartboard --debug \
  --cams /app/mocks/cam_1.mp4,/app/mocks/cam_2.mp4,/app/mocks/cam_3.mp4 \
  --width 1280 --height 720 --allow-plaintext \
  > /run891/run.out 2> /run891/run.err
PROGRAM_RC=$?; export PROGRAM_RC; echo "PROGRAM_RC=$PROGRAM_RC"
echo "--- the credential file afterwards ---"
python3 -c "
import json
d=json.load(open('/root/.config/opendartboard/credentials.json'))
print('keys now:', sorted(d.keys()))
print('organisation binding still present:', bool(d.get('token')))
"
echo "--- and a restart, to prove the evening is not rejoined ---"
OD_MAX_CYCLES=40 /app/build/opendartboard --debug \
  --cams /app/mocks/cam_1.mp4,/app/mocks/cam_2.mp4,/app/mocks/cam_3.mp4 \
  --width 1280 --height 720 --allow-plaintext > /run891/restart.out 2> /run891/restart.err
RESTART_RC=$?; export RESTART_RC; echo "RESTART_RC=$RESTART_RC"
sleep 1
kill $STUB 2>/dev/null; wait $STUB 2>/dev/null
python3 - <<'PY'
import json, collections
rows=[json.loads(l) for l in open('/run891/transcript.jsonl')]
print(collections.Counter(r['event'] for r in rows))
for r in rows:
    if r['event'] not in ('state_sample',):
        print('%-20s %-5s %-18s %s' % (r['event'], r.get('sector',''), r.get('path',''), r.get('round', r.get('closed',''))))
PY
grep -oE 'SCORE: [A-Z0-9]+ \| Position: \([^)]*\) \| Confidence: [0-9.]+ \| Camera: -?[0-9]+' /run891/run.out > /run891/run.scores
wc -l < /run891/run.scores
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
logs = text('run.out', 'run.err')
must('[i803] cycle budget reached' in logs, 'the scoring run reached its cycle budget')
must('casual_beat' not in events, 'no heartbeat was served (%d unserved)' % events.count('casual_beat_unserved'))
must('casual_given_up' in events, 'the stub gave the evening up')
must('the binding to Casual Contest 12 has ended (HTTP 401)' in logs,
     'the board ended the binding on a refused dart, not on a beat')
refused = [r for r in rows if r['event'] == 'casual_refused']
# A dart or a takeout: both are pushes, and either can be the first to meet the ended evening.
must(bool(refused) and all(r.get('path') in ('/api/v1/casual/detections', '/api/v1/casual/takeouts') for r in refused),
     'the refusal it learned from was a push, not a beat (%s)' % sorted({r.get('path') for r in refused}))
after = events[events.index('casual_given_up') + 1:] if 'casual_given_up' in events else []
must(after.count('casual_counted') == 0, 'nothing was counted into the evening after it was given up (%d)' % after.count('casual_counted'))
must(after.count('counted') >= 1, 'darts after the give-up went out of the club door (%d)' % after.count('counted'))
must(bool(creds.get('token')) and 'contest' not in creds,
     'the credential file keeps the club and forgets the evening (%s)' % sorted(creds))
must('bound to Casual Contest' not in text('restart.out', 'restart.err'), 'the restarted process did not rejoin the evening')
print('VERDICT: %s' % ('held' if not fails else 'BROKEN on %d of the above' % len(fails)))
sys.exit(1 if fails else 0)
PY
# The verdict's own status, and deliberately the last thing this script does (#1412).
exit $?
