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
echo "PAIR_RC=$?"
/app/build/opendartboard --pair-contest 571643 --turnaus http://127.0.0.1:8899 --allow-plaintext > /run891/paircontest.out 2> /run891/paircontest.err
echo "PAIR_CONTEST_RC=$?"
echo "--- phase 2: 1100 cycles with the network gone (RFC 5737 TEST-NET-1, blackholed) ---"
OD_MAX_CYCLES=1100 /app/build/opendartboard --debug \
  --cams /app/mocks/cam_1.mp4,/app/mocks/cam_2.mp4,/app/mocks/cam_3.mp4 \
  --width 1280 --height 720 --turnaus http://192.0.2.1:8899 --allow-plaintext \
  > /run891/gone.out 2> /run891/gone.err
echo "GONE_RC=$?"
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
echo "RESUME_RC=$?"
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
grep -o "\[i803\].*" /run891/gone.out
