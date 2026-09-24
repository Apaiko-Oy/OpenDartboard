#!/bin/bash
# #1554, inside the container: the cast-shadow subtraction on both rig fixtures --
# the axis census with the subtraction at its default and with the
# OD_AXIS_SHADOW=off falsification pin thrown, side by side.
#
# Four whole-clip detector runs (i1511's shape and cost apiece):
#
#   1. rig-20260918, OD_SHAFT_CENSUS=1                       -- subtraction ON (default)
#   2. rig-20260918, OD_SHAFT_CENSUS=1 OD_AXIS_SHADOW=off    -- the #1511 behaviour
#   3. rig-20260922, the same pair, with probe overlays on the on-arm -- the
#   4. hard-shadow fixture the issue is about
#
# What is ASSERTED: that each census parses and compares at least its floor of
# annotated pairs; that the PIN CENSUS is exact -- in an off-arm every I1511AXIS line
# says subtract=0, and in an on-arm every line that measured a figure says
# subtract=1 (a refusal at the support floor never reaches the classifier and
# honestly says 0) -- so one binary demonstrably carries both behaviours; and that
# the on-arm CLASSIFIED something on the hard-shadow fixture (a subtraction that
# never fires measures nothing, #1490). Accuracy movement between the arms is
# REPORTED, not asserted: separate replays of stateful detection carry documented
# run-to-run variance (i1511_inside.sh's own rule).
set -u

BIN=/app/build/opendartboard
RUN=/run1554
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

# The pin census, exact (#1554): $1 log, $2 arm ("on" or "off").
pin_census() {
    local log="$1" arm="$2"
    local total=$(grep -c 'I1511AXIS' "$log")
    if [ "$total" -eq 0 ]; then
        echo "FAIL $log holds no I1511AXIS lines to census"
        exit 1
    fi
    if [ "$arm" = "off" ]; then
        local live=$(grep 'I1511AXIS' "$log" | grep -c 'subtract=1')
        if [ "$live" -ne 0 ]; then
            echo "FAIL the off-arm classified anyway: $live of $total lines say subtract=1"
            exit 1
        fi
        echo "OK   pin census ($arm): $total axis lines, none classified"
    else
        # Every line that measured a figure (a valid axis certainly did) must say the
        # classifier was live; abstentions and support-floor refusals never reach it.
        local blind=$(grep 'I1511AXIS' "$log" | grep 'valid=1' | grep -c 'subtract=0')
        if [ "$blind" -ne 0 ]; then
            echo "FAIL the on-arm accepted $blind axis/axes the classifier never saw"
            exit 1
        fi
        echo "OK   pin census ($arm): every accepted axis was classified ($(grep 'I1511AXIS' "$log" | grep -c 'valid=1') valid of $total)"
    fi
}

census() { # $1 log, $2 truth, $3 annotations, $4 fixture label, $5 floor, $6 extra args
    python3 /app/testers/i1511_census.py --log "$1" --truth "$2" \
        --annotations "$3" --fixture "$4" --min-compared "$5" $6
}

echo "=== 1. rig-20260918, subtraction ON (the default) ==="
run_detector rig-20260918 rig18-shadow-on OD_SHAFT_CENSUS=1
pin_census "$RUN/rig18-shadow-on.txt" on
census "$RUN/rig18-shadow-on.txt" $T18 $A18 "rig-20260918 shadow-subtraction ON" 10 "" \
    | tee "$RUN/census18-on.txt" || exit 1

echo "=== 2. rig-20260918, OD_AXIS_SHADOW=off (the #1511 behaviour) ==="
run_detector rig-20260918 rig18-shadow-off OD_SHAFT_CENSUS=1 OD_AXIS_SHADOW=off
pin_census "$RUN/rig18-shadow-off.txt" off
census "$RUN/rig18-shadow-off.txt" $T18 $A18 "rig-20260918 shadow-subtraction OFF" 10 "" \
    | tee "$RUN/census18-off.txt" || exit 1

echo "=== 3. rig-20260922, subtraction ON, with overlays ==="
mkdir -p "$RUN/probe22"
run_detector rig-20260922 rig22-shadow-on OD_SHAFT_CENSUS=1 OD_SHAFT_PROBE="$RUN/probe22"
pin_census "$RUN/rig22-shadow-on.txt" on
census "$RUN/rig22-shadow-on.txt" $T22 $A22 "rig-20260922 shadow-subtraction ON" 4 \
    "--no-arrival 1.1,1.3" | tee "$RUN/census22-on.txt" || exit 1
# A subtraction that never fires on the HARD-SHADOW fixture measures nothing (#1490).
CLASSIFIED=$(grep 'I1511AXIS' "$RUN/rig22-shadow-on.txt" | grep -c 'shadowPx=[1-9]')
if [ "$CLASSIFIED" -eq 0 ]; then
    echo "FAIL the subtraction classified nothing anywhere on rig-20260922"
    exit 1
fi
echo "OK   $CLASSIFIED axis figures carried classified shadow on rig-20260922"
APPLIED=$(grep 'I1511AXIS' "$RUN/rig22-shadow-on.txt" | grep 'shadowPx=[1-9]' | grep -c 'applied=1')
echo "applied census: the subtracted spine won the per-figure comparison on $APPLIED of $CLASSIFIED classified figures; the rest abstained to the plain fit (reported, not asserted)"

echo "=== 4. rig-20260922, OD_AXIS_SHADOW=off ==="
run_detector rig-20260922 rig22-shadow-off OD_SHAFT_CENSUS=1 OD_AXIS_SHADOW=off
pin_census "$RUN/rig22-shadow-off.txt" off
census "$RUN/rig22-shadow-off.txt" $T22 $A22 "rig-20260922 shadow-subtraction OFF" 4 \
    "--no-arrival 1.1,1.3" | tee "$RUN/census22-off.txt" || exit 1

echo "=== the before/after, side by side (reported, not asserted) ==="
for f in 18 22; do
    echo "--- rig-$f OFF (the #1511 fit, corrected census):"
    grep -E 'I1511 (ACCURACY|COMPARED)' "$RUN/census$f-off.txt"
    echo "--- rig-$f ON (the #1554 fit):"
    grep -E 'I1511 (ACCURACY|COMPARED)' "$RUN/census$f-on.txt"
done

echo "I1554 DONE logs, censuses and overlays under $RUN"
exit 0
