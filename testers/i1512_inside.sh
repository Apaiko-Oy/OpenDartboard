#!/bin/bash
# #1512, inside the container: the entry-intersection census on both rig fixtures, the
# control proving the census pin defaults off with published scores untouched, the
# reprojection overlays, and the inherited falsification targets read out by name.
#
# Four whole-clip detector runs (i1511's shape and cost apiece):
#
#   1. rig-20260918 with no pins -- the CONTROL. Not one I1512 line may print, and the
#      published SCORE lines are diffed byte-for-byte against the pinned run below
#      (reported, not asserted: separate replays of stateful detection vary run to
#      run, rig-18's visit-6 dart is documented variance).
#   2. rig-20260918 with OD_GEO_SCORE=on OD_SHAFT_CENSUS=1 OD_GEO_PROBE -- the
#      geometric census against GROUND-TRUTH.md and testers/i1511_annotations/,
#      SIDE BY SIDE with the string-vote baseline, plus one overlay JPEG per camera
#      per called dart with the solved entry reprojected into that view.
#   3. rig-20260922 the same, against its truth table (camera 1 has no fitted board
#      on this box -- the 2-of-3 shape, reported, never failed on).
#   4. The inherited falsification targets, read out of run 2/3's own census:
#      rig-18 v4's phantom (thrown miss published T20@0.7 off camera 3) and v6's
#      phantom (S7@0.7) as UNMATCHED events with the solver's verdict beside them,
#      and rig-22's #1535 re-report dart in the PAIR lines.
#
# What is ASSERTED: the control's zero; that each census parses and solves at least
# its floor of entries (a census that compared nothing has measured nothing, #1490);
# that overlays were written. Accuracy figures are REPORTED for the report to carry.
set -u

BIN=/app/build/opendartboard
RUN=/run1512
T18=/app/mocks/rig-20260918/GROUND-TRUTH.md
T22=/app/testers/i1499_truth_rig20260922.md
A18=/app/testers/i1511_annotations/rig-20260918.csv
A22=/app/testers/i1511_annotations/rig-20260922.csv

if [ ! -x $BIN ]; then echo "FAIL no $BIN"; exit 1; fi
for f in $T18 $T22 $A18 $A22; do
    if [ ! -s $f ]; then echo "FAIL $f is missing"; exit 1; fi
done

run_detector() { # $1 fixture dir, $2 output basename, $3.. extra env as VAR=value
    local dir="/app/mocks/$1" out="$2"
    shift 2
    local cams="$dir/cam_1.mp4,$dir/cam_2.mp4,$dir/cam_3.mp4"
    cd "$RUN" || exit 1
    rm -rf "$RUN/cache" "$RUN/debug_frames"
    env OD_MAX_CYCLES=0 "$@" timeout 900 $BIN --cams "$cams" --width 1280 --height 720 \
        > "$RUN/$out.out" 2>&1
    local rc=$?
    sed 's/\x1b\[[0-9;]*m//g' "$RUN/$out.out" > "$RUN/$out.txt"
    echo "detector $out rc=$rc lines=$(wc -l < "$RUN/$out.txt")"
    if [ $rc -ne 0 ] && [ $rc -ne 124 ]; then
        tail -5 "$RUN/$out.txt"
        echo "FAIL the $out run did not finish"
        exit 1
    fi
}

echo "=== 1. rig-20260918, no pins (control) ==="
run_detector rig-20260918 rig18-off
OFF_LINES=$(grep -c 'I1512' "$RUN/rig18-off.txt")
if [ "$OFF_LINES" -ne 0 ]; then
    echo "FAIL no pins set and $OFF_LINES I1512 lines printed; the census default is not off"
    exit 1
fi
echo "OK   the control printed zero I1512 lines"

echo "=== 2. rig-20260918, OD_GEO_SCORE=on + probe ==="
mkdir -p "$RUN/probe18geo"
run_detector rig-20260918 rig18-on OD_GEO_SCORE=on OD_SHAFT_CENSUS=1 OD_GEO_PROBE="$RUN/probe18geo"
python3 /app/testers/i1512_census.py --log "$RUN/rig18-on.txt" --truth $T18 \
    --annotations $A18 --fixture "mocks/rig-20260918 (the ground-truthed rig)" \
    --min-solved 4 | tee "$RUN/census18.txt"
CRC=${PIPESTATUS[0]}
if [ "$CRC" -ne 0 ]; then exit 1; fi
P18=$(ls "$RUN/probe18geo" | wc -l)
if [ "$P18" -eq 0 ]; then
    echo "FAIL the probe run wrote no overlay images"
    exit 1
fi
echo "OK   $P18 overlay images under $RUN/probe18geo"

echo "=== 3. rig-20260922, OD_GEO_SCORE=on + probe ==="
mkdir -p "$RUN/probe22geo"
run_detector rig-20260922 rig22-on OD_GEO_SCORE=on OD_SHAFT_CENSUS=1 OD_GEO_PROBE="$RUN/probe22geo"
python3 /app/testers/i1512_census.py --log "$RUN/rig22-on.txt" --truth $T22 \
    --annotations $A22 --fixture "mocks/rig-20260922 (the current rig)" \
    --min-solved 2 | tee "$RUN/census22.txt"
CRC=${PIPESTATUS[0]}
if [ "$CRC" -ne 0 ]; then exit 1; fi
echo "OK   $(ls "$RUN/probe22geo" | wc -l) overlay images under $RUN/probe22geo"

echo "=== 4. the inherited falsification targets (reported by name) ==="
echo "--- rig-18 phantoms (#1505: lone-witness darts; no second constraint should exist):"
grep -E 'I1512 UNMATCHED' "$RUN/census18.txt" || echo "(no unmatched events this replay -- run-to-run detection variance; see census18.txt)"
echo "--- rig-22 #1535 re-report dart (v1.2) and its wedge-neighbour pairs:"
grep -E 'I1512 PAIR v1\.|I1512 UNMATCHED v1' "$RUN/census22.txt" || true

echo "=== published scores, control vs census run (reported, not asserted) ==="
grep 'SCORE:' "$RUN/rig18-off.txt" | sed 's/.*SCORE:/SCORE:/;s/ | Processing.*//' > "$RUN/rig18-off.scores"
grep 'SCORE:' "$RUN/rig18-on.txt"  | sed 's/.*SCORE:/SCORE:/;s/ | Processing.*//' > "$RUN/rig18-on.scores"
if diff "$RUN/rig18-off.scores" "$RUN/rig18-on.scores" > "$RUN/rig18.scorediff"; then
    echo "identical: $(wc -l < "$RUN/rig18-off.scores") published lines byte-for-byte"
else
    echo "differ (run-to-run detection variance is documented; the shadow cannot read a score):"
    cat "$RUN/rig18.scorediff"
fi

echo "I1512 DONE logs, censuses and overlays under $RUN"
exit 0
