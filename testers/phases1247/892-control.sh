# A RECORDING phase (#1412). It asserts nothing. It runs the detector through a
# situation and leaves stdout, stderr and the stub's transcript behind for a reader to
# judge, so its green tick in run_all.sh means "it ran to the end" and never "it held".
# phases1247/1188-subscribers.sh is the one phase in this directory that really does
# compute a verdict, and since #1412 it exits on it.
#
# What its exit status carries is its last line and no more: `wc -l` on the stub's
# transcript, which fails if no transcript was written at all. So a run whose stub never
# started is red; everything else about the run is for a reader.
set -u
export STUB_TRANSCRIPT=/run892/transcript.jsonl
export STUB_INTERVAL_SECONDS=15
export STUB_SILENCE_SECONDS=60
export STUB_SAMPLE_SECONDS=1
python3 /app/testers/turnaus_stub.py > /run892/stub.out 2> /run892/stub.err &
STUB=$!
sleep 1
echo "--- pairing ---"
/app/build/opendartboard --pair 483920 --turnaus http://127.0.0.1:8899 --allow-plaintext \
  > /run892/pair.out 2> /run892/pair.err
echo "PAIR_RC=$?"
echo "--- scoring run, 1100 cycles, beats every 15s ---"
OD_MAX_CYCLES=1100 /app/build/opendartboard --debug \
  --cams /app/mocks/cam_1.mp4,/app/mocks/cam_2.mp4,/app/mocks/cam_3.mp4 \
  --width 1280 --height 720 --allow-plaintext \
  > /run892/run.out 2> /run892/run.err
echo "PROGRAM_RC=$?"
echo "--- 95 seconds of the stub watching a board that has stopped ---"
sleep 95
kill $STUB 2>/dev/null
wait $STUB 2>/dev/null
echo "--- transcript ---"
wc -l /run892/transcript.jsonl
