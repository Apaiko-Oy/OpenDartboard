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
echo "PAIR_RC=$?"
/app/build/opendartboard --pair-contest 571643 --turnaus http://127.0.0.1:8899 --allow-plaintext > /run891/paircontest.out 2> /run891/paircontest.err
echo "PAIR_CONTEST_RC=$?"
echo "--- a club board on somebody's evening: the third dart ends it ---"
OD_MAX_CYCLES=1100 /app/build/opendartboard --debug \
  --cams /app/mocks/cam_1.mp4,/app/mocks/cam_2.mp4,/app/mocks/cam_3.mp4 \
  --width 1280 --height 720 --allow-plaintext \
  > /run891/run.out 2> /run891/run.err
echo "PROGRAM_RC=$?"
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
echo "RESTART_RC=$?"
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
grep -o "\[i803\].*" /run891/run.out
