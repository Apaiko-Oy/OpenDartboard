#!/bin/bash
# #1511, inside the container: the shaft-axis census on both rig fixtures, the control
# proving the census pins default off, the probe overlays, and the gate falsifier on
# real footage.
#
# Four whole-clip detector runs (i1510p2's shape and cost apiece):
#
#   1. rig-20260918 with no pins -- the CONTROL. Not one I1511AXIS line may print:
#      the observation is computed on every window, but a probe nobody asked for is a
#      probe somebody will one day parse by accident.
#   2. rig-20260918 with OD_SHAFT_CENSUS=1 OD_SHAFT_PROBE -- the accuracy AND
#      coverage/refusal census against testers/i1511_annotations/rig-20260918.csv,
#      plus one annotated overlay JPEG per camera per advanced window.
#   3. rig-20260922 the same, against its annotation subset (visits 1-3; the README
#      records the scope bound and why camera 1 is absent on this box).
#   4. rig-20260918 with OD_AXIS_GATE=off as well -- the falsification pin on REAL
#      footage. REPORTED, not asserted: the synthetic check (unit_check.sh 1511) is
#      where the mutation is load-bearing; this run says what the gates actually
#      refuse on fixture figures, with the numbers beside it.
#
# What is ASSERTED: the control's zero; that each census run parses and compares at
# least the floor of annotated pairs (a census that compared nothing has measured
# nothing, #1490); that overlays were written. Published SCORE sequences are diffed
# and REPORTED only -- separate replays of stateful detection vary run to run
# (rig-18's visit-6 dart is documented variance), so equality would be a coin-flip red.
set -u

BIN=/app/build/opendartboard
RUN=/run1511
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
OFF_LINES=$(grep -c 'I1511AXIS' "$RUN/rig18-off.txt")
if [ "$OFF_LINES" -ne 0 ]; then
    echo "FAIL no pins set and $OFF_LINES I1511AXIS lines printed; the census default is not off"
    exit 1
fi
echo "OK   the control printed zero I1511AXIS lines"

echo "=== 2. rig-20260918, OD_SHAFT_CENSUS=1 + probe ==="
mkdir -p "$RUN/probe18"
run_detector rig-20260918 rig18-on OD_SHAFT_CENSUS=1 OD_SHAFT_PROBE="$RUN/probe18"
python3 /app/testers/i1511_census.py --log "$RUN/rig18-on.txt" --truth $T18 \
    --annotations $A18 --fixture "mocks/rig-20260918 (the ground-truthed rig)" \
    --min-compared 10 || exit 1
P18=$(ls "$RUN/probe18" | wc -l)
if [ "$P18" -eq 0 ]; then
    echo "FAIL the probe run wrote no overlay images"
    exit 1
fi
echo "OK   $P18 overlay images under $RUN/probe18"

echo "=== 3. rig-20260922, OD_SHAFT_CENSUS=1 + probe ==="
mkdir -p "$RUN/probe22"
run_detector rig-20260922 rig22-on OD_SHAFT_CENSUS=1 OD_SHAFT_PROBE="$RUN/probe22"
python3 /app/testers/i1511_census.py --log "$RUN/rig22-on.txt" --truth $T22 \
    --annotations $A22 --fixture "mocks/rig-20260922 (the current rig)" \
    --min-compared 4 || exit 1
echo "OK   $(ls "$RUN/probe22" | wc -l) overlay images under $RUN/probe22"

echo "=== 4. rig-20260918, gates off (reported, not asserted) ==="
run_detector rig-20260918 rig18-ungated OD_SHAFT_CENSUS=1 OD_AXIS_GATE=off
GATED_REFUSED=$(grep 'I1511AXIS' "$RUN/rig18-on.txt" | grep -c 'valid=0 .*refusal=\(not \|support floor\|direction \)')
UNGATED_VALID=$(grep 'I1511AXIS' "$RUN/rig18-ungated.txt" | grep -c 'valid=1')
GATED_VALID=$(grep 'I1511AXIS' "$RUN/rig18-on.txt" | grep -c 'valid=1')
echo "gate census: gated run valid=$GATED_VALID gate-refused=$GATED_REFUSED; ungated run valid=$UNGATED_VALID"
echo "(separate replays; the load-bearing mutation is measured figure-for-figure by unit_check.sh 1511)"

echo "=== published scores, control vs census run (reported, not asserted) ==="
grep 'SCORE:' "$RUN/rig18-off.txt" | sed 's/.*SCORE:/SCORE:/;s/ | Processing.*//' > "$RUN/rig18-off.scores"
grep 'SCORE:' "$RUN/rig18-on.txt"  | sed 's/.*SCORE:/SCORE:/;s/ | Processing.*//' > "$RUN/rig18-on.scores"
if diff "$RUN/rig18-off.scores" "$RUN/rig18-on.scores" > "$RUN/rig18.scorediff"; then
    echo "identical: $(wc -l < "$RUN/rig18-off.scores") published lines byte-for-byte"
else
    echo "differ (run-to-run detection variance is documented; the census cannot read a score):"
    cat "$RUN/rig18.scorediff"
fi

echo "I1511 DONE logs, censuses and overlays under $RUN"
exit 0
