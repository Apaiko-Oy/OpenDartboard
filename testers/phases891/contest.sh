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
echo "PAIR_RC=$?"
echo "--- phase 2: somebody opens a Casual Contest and hands the board its six digits ---"
/app/build/opendartboard --pair-contest 571643 --turnaus http://127.0.0.1:8899 --allow-plaintext \
  > /run891/paircontest.out 2> /run891/paircontest.err
echo "PAIR_CONTEST_RC=$?"
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
echo "PROGRAM_RC=$?"
echo "--- phase 4: a SECOND process, to prove the binding survived a restart ---"
OD_MAX_CYCLES=40 /app/build/opendartboard --debug \
  --cams /app/mocks/cam_1.mp4,/app/mocks/cam_2.mp4,/app/mocks/cam_3.mp4 \
  --width 1280 --height 720 --allow-plaintext \
  > /run891/restart.out 2> /run891/restart.err
echo "RESTART_RC=$?"
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
grep -o "\[i803\].*" /run891/run.out
