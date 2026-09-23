#!/bin/bash
# #1499, inside the container: the accuracy census over mocks/rig-20260922, the fixture
# the i1484 harness predates. Whole clip, no cycle cap, judged with the same
# i1484_confidence_census.py against a table transcription of the fixture's own
# GROUND-TRUTH.md line (testers/i1499_truth_rig20260922.md; nothing under mocks/ is
# edited, and a copy at /run1499/truth-rig-20260922.md wins so a run can override it).
set -u

BIN=/app/build/opendartboard
DIR=/app/mocks/rig-20260922
CAMS=$DIR/cam_1.mp4,$DIR/cam_2.mp4,$DIR/cam_3.mp4
TRUTH=/run1499/truth-rig-20260922.md
[ -s "$TRUTH" ] || TRUTH=/app/testers/i1499_truth_rig20260922.md
RUN=/run1499

if [ ! -x $BIN ]; then echo "FAIL no $BIN"; exit 1; fi
if [ ! -s $TRUTH ]; then echo "FAIL no $TRUTH"; exit 1; fi

g++ -std=c++17 -O1 -o $RUN/clip_length /app/testers/i1484_clip_length.cpp \
    $(pkg-config --cflags --libs opencv4) 2>/dev/null
$RUN/clip_length $DIR/cam_1.mp4 $DIR/cam_2.mp4 $DIR/cam_3.mp4 > $RUN/clip-lengths-22.txt

cd $RUN
rm -rf $RUN/cache $RUN/debug_frames
env OD_MAX_CYCLES=0 timeout 900 $BIN --cams "$CAMS" --width 1280 --height 720 > $RUN/rig22.out 2>&1
rc=$?
sed 's/\x1b\[[0-9;]*m//g' $RUN/rig22.out > $RUN/rig22.txt
echo "detector rc=$rc lines=$(wc -l < $RUN/rig22.txt)"

python3 /app/testers/i1484_confidence_census.py --log $RUN/rig22.txt \
    --fixture "mocks/rig-20260922 (the current rig)" --clips "$CAMS" \
    --lengths $RUN/clip-lengths-22.txt --truth $TRUTH
exit $?
