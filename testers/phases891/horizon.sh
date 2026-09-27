# An ASSERTING phase (#1463). A board bound only to an evening restarts over a spool planted
# by hand: two records past the fifteen-minute horizon, one owed to last night's Contest and
# one fresh record owed to this one. Then the second beat gives the evening up.
#
# Its exit status is the verdict at the bottom and nothing else: 0 only when the Contest
# pairing and the detector exited 0, exactly the one fresh record for this evening (01M1DDDD)
# was delivered and none of the other three arrived anywhere, the board said it resumed 1
# and abandoned 2 as stale and 1 as owed to another Contest, the stub gave the evening up,
# the board said it has no other binding, and the credential file afterwards holds nothing.
# Until #1463 its last command was the transcript printout, which is red only on a missing
# or torn transcript.
#
# Nothing else reads what it leaves in /run891: no tester, harness or document consumes its
# transcript (grepped for phases891, run891 and the 891 runs directory, #1463), which is why
# it asserts rather than records. The transcript is still printed above the verdict for a
# reader who wants to see why.
set -u
export STUB_TRANSCRIPT=/run891/transcript.jsonl
export STUB_INTERVAL_SECONDS=5
export STUB_SILENCE_SECONDS=60
export STUB_SAMPLE_SECONDS=0
export STUB_GIVE_UP_AFTER_BEATS=2
python3 /app/testers/turnaus_stub.py > /run891/stub.out 2> /run891/stub.err &
STUB=$!
sleep 1
echo "--- a board in somebody's garage: ONE binding, and it is the evening's ---"
/app/build/opendartboard --pair-contest 571643 --turnaus http://127.0.0.1:8899 --allow-plaintext > /run891/paircontest.out 2> /run891/paircontest.err
PAIR_CONTEST_RC=$?; export PAIR_CONTEST_RC; echo "PAIR_CONTEST_RC=$PAIR_CONTEST_RC"
python3 -c "
import json
d=json.load(open('/root/.config/opendartboard/credentials.json'))
print('keys:', sorted(d.keys()), 'organisation token present:', bool(d.get('token')))
"
echo "--- a spool planted by hand: nothing here waits fifteen minutes for the horizon ---"
python3 - <<'PY'
import json, time
now = int(time.time()*1000)
rows = [
  # Twenty minutes old, owed to the club. The horizon: past it a dart is counted into
  # whoever is throwing now, so it is abandoned.
  {"path":"/api/v1/autoscorer/detections","body":json.dumps({"reference":"01M1AAAAAAAAAAAAAAAAAAAAAA","sector":"S20","bounced_out":False}),"key":"01M1AAAAAAAAAAAAAAAAAAAAAA","binding":"organisation","contest_id":0,"spooled_ms":now-20*60*1000},
  # Twenty minutes old and owed to THIS evening. Same horizon, same verdict.
  {"path":"/api/v1/casual/detections","body":json.dumps({"reference":"01M1BBBBBBBBBBBBBBBBBBBBBB","sector":"S20","bounced_out":False}),"key":"01M1BBBBBBBBBBBBBBBBBBBBBB","binding":"contest","contest_id":12,"spooled_ms":now-20*60*1000},
  # Fresh, and owed to LAST NIGHT'S Contest. Inside the horizon and still undeliverable.
  {"path":"/api/v1/casual/detections","body":json.dumps({"reference":"01M1CCCCCCCCCCCCCCCCCCCCCC","sector":"T20","bounced_out":False}),"key":"01M1CCCCCCCCCCCCCCCCCCCCCC","binding":"contest","contest_id":99,"spooled_ms":now-30*1000},
  # Fresh, this evening's. The one that should arrive.
  {"path":"/api/v1/casual/detections","body":json.dumps({"reference":"01M1DDDDDDDDDDDDDDDDDDDDDD","sector":"D20","bounced_out":False}),"key":"01M1DDDDDDDDDDDDDDDDDDDDDD","binding":"contest","contest_id":12,"spooled_ms":now-30*1000},
]
with open('/root/.config/opendartboard/owed.jsonl','w') as fh:
    for r in rows: fh.write(json.dumps(r)+"\n")
print('planted', len(rows), 'records')
PY
echo "--- 65 cycles: fewer than the first dart, and the second beat ends the evening ---"
OD_MAX_CYCLES=65 /app/build/opendartboard --debug \
  --cams /app/mocks/cam_1.mp4,/app/mocks/cam_2.mp4,/app/mocks/cam_3.mp4 \
  --width 1280 --height 720 --allow-plaintext \
  > /run891/run.out 2> /run891/run.err
PROGRAM_RC=$?; export PROGRAM_RC; echo "PROGRAM_RC=$PROGRAM_RC"
python3 -c "
import json
d=json.load(open('/root/.config/opendartboard/credentials.json'))
print('keys after the evening ended:', sorted(d.keys()))
"
sleep 1
kill $STUB 2>/dev/null; wait $STUB 2>/dev/null
python3 - <<'PY'
import json, collections
rows=[json.loads(l) for l in open('/run891/transcript.jsonl')]
print(collections.Counter(r['event'] for r in rows))
for r in rows:
    print('%-20s %-6s %s' % (r['event'], r.get('sector',''), r.get('reference','')))
PY
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
for n in ('PAIR_CONTEST_RC', 'PROGRAM_RC'):
    must(rc(n) == '0', '%s is 0 (%s)' % (n, rc(n)))
logs = text('run.out', 'run.err')
arrived = [r.get('reference') for r in rows if r['event'] in ('casual_counted', 'casual_absorbed', 'counted', 'absorbed')]
must(arrived.count('01M1DDDDDDDDDDDDDDDDDDDDDD') == 1, "this evening's fresh record was delivered once")
for ref, why in (('01M1AAAAAAAAAAAAAAAAAAAAAA', 'the stale club record'),
                 ('01M1BBBBBBBBBBBBBBBBBBBBBB', "this evening's stale record"),
                 ('01M1CCCCCCCCCCCCCCCCCCCCCC', "last night's record")):
    must(ref not in arrived, '%s never arrived' % why)
must('resumed 1 owed push(es) from the spool; 2 abandoned as older than the round they belonged to; '
     '1 abandoned as owed to a Contest this board is no longer bound to' in logs,
     'the board said it resumed 1, abandoned 2 as stale and 1 as another evening\'s')
must('casual_given_up' in events, 'the stub gave the evening up')
must('this board holds no other binding' in logs, 'the board said it holds no other binding')
must('contest' not in creds and not creds.get('token'), 'the credential file holds no binding afterwards (%s)' % sorted(creds))
print('VERDICT: %s' % ('held' if not fails else 'BROKEN on %d of the above' % len(fails)))
sys.exit(1 if fails else 0)
PY
# The verdict's own status, and deliberately the last thing this script does (#1412).
exit $?
