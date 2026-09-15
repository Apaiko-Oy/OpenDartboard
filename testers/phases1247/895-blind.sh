set -u
export STUB_TRANSCRIPT=/run895/transcript.jsonl
export STUB_INTERVAL_SECONDS=5
export STUB_SILENCE_SECONDS=60
export STUB_SAMPLE_SECONDS=1
python3 /app/testers/turnaus_stub.py > /run895/stub.out 2> /run895/stub.err &
STUB=$!
sleep 1
/app/build/opendartboard --pair 483920 --turnaus http://127.0.0.1:8899 --allow-plaintext \
  > /run895/pair.out 2> /run895/pair.err
echo "PAIR_RC=$?"
echo "--- a board that opens, calibrates, scores, and loses every camera at cycle 400 ---"
OD_MAX_CYCLES=900 OD_BLIND_AFTER=400 /app/build/opendartboard --debug \
  --cams /app/mocks/cam_1.mp4,/app/mocks/cam_2.mp4,/app/mocks/cam_3.mp4 \
  --width 1280 --height 720 --allow-plaintext \
  > /run895/run.out 2> /run895/run.err
echo "PROGRAM_RC=$?"
sleep 3
kill $STUB 2>/dev/null
wait $STUB 2>/dev/null
wc -l /run895/transcript.jsonl
