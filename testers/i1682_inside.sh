#!/bin/bash
# unrun-tester: the inside half of testers/i1682_run.sh, a census a person or agent runs (#1682), not a check of the tree.
# #1682 driver: the bakeoff's six fixture-windows (+ the r18 pin) with OD_TRACE per run,
# census lines in the bakeoff's own file names so testers/i1655_rows.py can compare.
# Run by testers/i1682_run.sh inside the image, the tree at /app and the run dir at /run1555.
set -u
BIN=/app/build/opendartboard; RUN=/run1555
T18=/app/mocks/rig-20260918/GROUND-TRUTH.md; T22=/app/testers/i1499_truth_rig20260922.md
A18=/app/testers/i1511_annotations/rig-20260918.csv; A22=/app/testers/i1511_annotations/rig-20260922.csv
T29=/app/mocks/rig-20260929/GROUND-TRUTH.md
run_detector() { local dir="/app/mocks/$1" out="$2"; shift 2
  cd "$RUN"; rm -rf "$RUN/cache" "$RUN/debug_frames"
  env OD_MAX_CYCLES=0 OD_GEO_SCORE=on OD_SHAFT_CENSUS=1 OD_TRACE="$RUN/$out.trace.csv" "$@" \
    timeout 2400 $BIN --cams "$dir/cam_1.mp4,$dir/cam_2.mp4,$dir/cam_3.mp4" --width 1280 --height 720 > "$RUN/$out.out" 2>&1
  echo "detector $out rc=$?"; sed 's/\x1b\[[0-9;]*m//g' "$RUN/$out.out" > "$RUN/$out.txt"; }
c18() { python3 /app/testers/i1555_census.py --log "$RUN/$1.txt" --truth $T18 --annotations $A18 --fixture rig-20260918 --window $2 --min-matched 4 > "$RUN/census-$1.txt"; }
c22() { python3 /app/testers/i1555_census.py --log "$RUN/$1.txt" --truth $T22 --annotations $A22 --fixture rig-20260922 --window $2 --min-matched 2 --no-arrival 1.1 > "$RUN/census-$1.txt"; }
c29() { python3 /app/testers/i1555_census.py --log "$RUN/$1.txt" --truth $T29 --not-annotated --fixture rig-20260929 --window $2 --min-matched 4 > "$RUN/census-$1.txt"; }
run_detector rig-20260918 r18-dev; c18 r18-dev dev
run_detector rig-20260918 r18-open OD_SEEK_VIDEO=off; c18 r18-open opening
run_detector rig-20260922 r22-dev; c22 r22-dev dev
run_detector rig-20260922 r22-open OD_SEEK_VIDEO=off; c22 r22-open opening
run_detector rig-20260918 r18-pin OD_SCORE_PATH=vote; c18 r18-pin dev
run_detector rig-20260929 r29-dev; c29 r29-dev dev
run_detector rig-20260929 r29-open OD_SEEK_VIDEO=off; c29 r29-open opening
python3 /app/testers/i1555_census.py --pool $RUN/census-r18-dev.txt $RUN/census-r18-open.txt $RUN/census-r22-dev.txt $RUN/census-r22-open.txt $RUN/census-r29-dev.txt $RUN/census-r29-open.txt > $RUN/pooled.txt
python3 /app/testers/i1555_census.py --pool-label POOLED-r18+r22 --pool $RUN/census-r18-dev.txt $RUN/census-r18-open.txt $RUN/census-r22-dev.txt $RUN/census-r22-open.txt > $RUN/pooled-r18-r22.txt
echo DONE
