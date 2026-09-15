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
echo "PAIR_CONTEST_RC=$?"
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
echo "PROGRAM_RC=$?"
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
