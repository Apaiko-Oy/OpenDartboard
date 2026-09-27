# An ASSERTING phase (#1463). Both bindings are held and the network goes away for a whole
# scoring run; the darts must be spooled as owed to the evening, and delivered into it when
# a later process can reach the server again.
#
# Its exit status is the verdict at the bottom and nothing else: 0 only when both pairings
# and both detector runs exited 0, the offline run reached its cycle budget, nothing reached
# the stub while the network was gone, the spool held at least one record and every record
# was owed to Contest 12, every spooled detection's reference was later counted (or
# absorbed) at the Casual door, nothing went out of the club's door, and the spool was
# settled afterwards. Until #1463 it ended on `grep -o "[i803]..."`, so a board that
# spooled nothing -- or spooled the evening's darts to the club -- was green.
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
echo "--- phase 1: both codes, while the pub wifi still works ---"
/app/build/opendartboard --pair 483920 --turnaus http://127.0.0.1:8899 --allow-plaintext > /run891/pair.out 2> /run891/pair.err
PAIR_RC=$?; export PAIR_RC; echo "PAIR_RC=$PAIR_RC"
/app/build/opendartboard --pair-contest 571643 --turnaus http://127.0.0.1:8899 --allow-plaintext > /run891/paircontest.out 2> /run891/paircontest.err
PAIR_CONTEST_RC=$?; export PAIR_CONTEST_RC; echo "PAIR_CONTEST_RC=$PAIR_CONTEST_RC"
echo "--- phase 2: 1100 cycles with the network gone (RFC 5737 TEST-NET-1, blackholed) ---"
OD_MAX_CYCLES=1100 /app/build/opendartboard --debug \
  --cams /app/mocks/cam_1.mp4,/app/mocks/cam_2.mp4,/app/mocks/cam_3.mp4 \
  --width 1280 --height 720 --turnaus http://192.0.2.1:8899 --allow-plaintext \
  > /run891/gone.out 2> /run891/gone.err
GONE_RC=$?; export GONE_RC; echo "GONE_RC=$GONE_RC"
# Snapshots the verdict reads: the spool as the offline run left it, and everything the
# stub had heard before the network came back.
cp /root/.config/opendartboard/owed.jsonl /run891/owed.at_gone 2>/dev/null
cp /run891/transcript.jsonl /run891/transcript.at_resume 2>/dev/null
echo "--- the spool, and what each record is owed to ---"
wc -l < /root/.config/opendartboard/owed.jsonl
python3 - <<'PY'
import json, collections
rows=[json.loads(l) for l in open('/root/.config/opendartboard/owed.jsonl')]
print('records:', len(rows))
print('bindings:', collections.Counter(r.get('binding') for r in rows))
print('contest ids:', collections.Counter(r.get('contest_id') for r in rows))
print('paths:', collections.Counter(r.get('path') for r in rows))
PY
ls -l /root/.config/opendartboard/
echo "--- phase 3: the wifi is back; a new process resumes what is owed ---"
OD_MAX_CYCLES=60 /app/build/opendartboard --debug \
  --cams /app/mocks/cam_1.mp4,/app/mocks/cam_2.mp4,/app/mocks/cam_3.mp4 \
  --width 1280 --height 720 --allow-plaintext \
  > /run891/resume.out 2> /run891/resume.err
RESUME_RC=$?; export RESUME_RC; echo "RESUME_RC=$RESUME_RC"
echo "spool after: $(wc -l < /root/.config/opendartboard/owed.jsonl) records, cursor $(cat /root/.config/opendartboard/owed.cursor 2>/dev/null)"
sleep 1
kill $STUB 2>/dev/null; wait $STUB 2>/dev/null
python3 - <<'PY'
import json, collections
rows=[json.loads(l) for l in open('/run891/transcript.jsonl')]
print(collections.Counter(r['event'] for r in rows))
for r in rows:
    if r['event'] in ('casual_counted','casual_takeout','counted','takeout'):
        print('%-16s %-5s %s' % (r['event'], r.get('sector',''), r.get('round', r.get('closed'))))
PY
grep -oE 'SCORE: [A-Z0-9]+ \| Position: \([^)]*\) \| Confidence: [0-9.]+ \| Camera: -?[0-9]+' /run891/gone.out > /run891/gone.scores
wc -l < /run891/gone.scores
grep -o "\[i803\].*" /run891/gone.out || true
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
for n in ('PAIR_RC', 'PAIR_CONTEST_RC', 'GONE_RC', 'RESUME_RC'):
    must(rc(n) == '0', '%s is 0 (%s)' % (n, rc(n)))
must('[i803] cycle budget reached' in text('gone.out'), 'the offline run reached its cycle budget')
pairing = {'paired', 'casual_paired', 'state_sample'}
must(os.path.exists('/run891/transcript.at_resume'), 'the transcript was snapshotted before the network came back')
gone_lines = open('/run891/transcript.at_resume').read().splitlines() if os.path.exists('/run891/transcript.at_resume') else []
gone_events = [json.loads(l)['event'] for l in gone_lines]
must(set(gone_events) <= pairing, 'nothing reached the stub while the network was gone (%s)' % sorted(set(gone_events) - pairing))
owed = [json.loads(l) for l in open('/run891/owed.at_gone')] if os.path.exists('/run891/owed.at_gone') else []
must(len(owed) >= 1, 'the offline run spooled what it scored (%d records)' % len(owed))
must(all(r.get('binding') == 'contest' and r.get('contest_id') == 12 for r in owed),
     'every spooled record is owed to Contest 12')
owed_refs = {json.loads(r['body']).get('reference') for r in owed if r.get('path') == '/api/v1/casual/detections'}
seen = {r.get('reference') for r in rows if r['event'] in ('casual_counted', 'casual_absorbed')}
must(bool(owed_refs) and owed_refs <= seen,
     'every spooled dart reached the Casual door after the network came back (%d of %d)' % (len(owed_refs & seen), len(owed_refs)))
must(events.count('counted') == 0, 'nothing owed to the evening went out of the club door (%d)' % events.count('counted'))
left = [l for l in open('/root/.config/opendartboard/owed.jsonl')] if os.path.exists('/root/.config/opendartboard/owed.jsonl') else []
try:
    cursor = int(open('/root/.config/opendartboard/owed.cursor').read().strip() or 0)
except OSError:
    cursor = 0
must(len(left) == 0 or cursor == len(left), 'the spool is settled afterwards (%d records, cursor %d)' % (len(left), cursor))
print('VERDICT: %s' % ('held' if not fails else 'BROKEN on %d of the above' % len(fails)))
sys.exit(1 if fails else 0)
PY
# The verdict's own status, and deliberately the last thing this script does (#1412).
exit $?
