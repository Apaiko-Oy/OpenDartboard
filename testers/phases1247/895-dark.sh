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
echo "--- three cameras that will not open; #892 measured SIGSEGV here ---"
cp /run895/still.jpg /run895/c1.jpg; cp /run895/still.jpg /run895/c2.jpg; cp /run895/still.jpg /run895/c3.jpg
/app/build/opendartboard --debug --cams /run895/c1.jpg,/run895/c2.jpg,/run895/c3.jpg \
  --width 1280 --height 720 --allow-plaintext \
  > /run895/run.out 2> /run895/run.err &
PROG=$!
sleep 80
echo "--- SIGTERM at 80s: does a faulted board leave by #825's path? ---"
kill -TERM $PROG 2>/dev/null
wait $PROG 2>/dev/null
echo "PROGRAM_RC=$?"
kill $STUB 2>/dev/null
wait $STUB 2>/dev/null
wc -l /run895/transcript.jsonl
