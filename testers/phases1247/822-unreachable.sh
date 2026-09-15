set -u
export STUB_TRANSCRIPT=/run822/transcript.jsonl
echo "=== PHASE 1: pair while the stub is up ==="
python3 /app/testers/turnaus_stub.py > /run822/stub1.out 2> /run822/stub1.err &
STUB=$!
sleep 1
/app/build/opendartboard --pair 483920 --turnaus http://127.0.0.1:8899 --allow-plaintext \
  > /run822/pair.out 2> /run822/pair.err
echo "PAIR_RC=$?"
kill $STUB 2>/dev/null; wait $STUB 2>/dev/null
echo "stub down. nothing is listening anywhere now."

echo "=== PHASE 2: 1100 cycles at a blackholed address ==="
date +%s.%N > /run822/p2.start
OD_MAX_CYCLES=1100 /app/build/opendartboard --debug \
  --cams /app/mocks/cam_1.mp4,/app/mocks/cam_2.mp4,/app/mocks/cam_3.mp4 \
  --width 1280 --height 720 --turnaus http://192.0.2.1:8899 --allow-plaintext \
  > /run822/run.out 2> /run822/run.err
echo "PROGRAM_RC=$?"
date +%s.%N > /run822/p2.end
echo "--- what is still owed on disk ---"
wc -l /root/.config/opendartboard/owed.jsonl
cat /root/.config/opendartboard/owed.cursor 2>/dev/null; echo
ls -la /root/.config/opendartboard/

echo "=== PHASE 3: restart, stub back up, first two pushes answered 500 ==="
STUB_FAIL_FIRST=2 STUB_TRANSCRIPT=/run822/transcript-resume.jsonl python3 /app/testers/turnaus_stub.py > /run822/stub2.out 2> /run822/stub2.err &
STUB=$!
sleep 1
OD_MAX_CYCLES=250 /app/build/opendartboard --debug \
  --cams /app/mocks/cam_1.mp4,/app/mocks/cam_2.mp4,/app/mocks/cam_3.mp4 \
  --width 1280 --height 720 --turnaus http://127.0.0.1:8899 --allow-plaintext \
  > /run822/resume.out 2> /run822/resume.err
echo "RESUME_RC=$?"
kill $STUB 2>/dev/null; wait $STUB 2>/dev/null
echo "--- spool after the flush ---"
wc -l /root/.config/opendartboard/owed.jsonl
cat /root/.config/opendartboard/owed.cursor 2>/dev/null; echo

echo "=== PHASE 4: SIGTERM with the network still gone ==="
OD_MAX_CYCLES=1100 /app/build/opendartboard --debug \
  --cams /app/mocks/cam_1.mp4,/app/mocks/cam_2.mp4,/app/mocks/cam_3.mp4 \
  --width 1280 --height 720 --turnaus http://192.0.2.1:8899 --allow-plaintext \
  > /run822/term.out 2> /run822/term.err &
P=$!
sleep 40
kill -TERM $P
wait $P
echo "TERM_RC=$?"
