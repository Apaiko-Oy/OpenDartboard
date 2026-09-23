#!/bin/bash
# #1510 Phase 2, inside the container: the paint-containment census on both rig
# fixtures, and the guard's default proved off on the same binary.
#
# Three whole-clip detector runs (i1499's shape and cost, no cycle cap):
#
#   1. rig-20260922 with OD_MODEL_SCORE unset -- the CONTROL. The run must print not
#      one I1510P2 line, because the guard's default is the slice's whole promise:
#      the binary's published scores do not move, and a probe nobody asked for is a
#      probe somebody will one day parse by accident.
#   2. rig-20260922 with OD_MODEL_SCORE=on -- the census run for the deployment rig.
#   3. rig-20260918 with OD_MODEL_SCORE=on -- the census run for the ground-truthed rig.
#
# i1510p2_census.py then prints, per dart per camera, what the existing per-ring
# machinery said, what the fitted board model said about the same tip, and what was
# thrown. This harness asserts its INSTRUMENT (#1490's rule: a census that compared
# nothing has measured nothing) and the control's zero; it asserts nothing about
# whether the model agrees, which is the census's finding and not its precondition.
#
# The published SCORE sequences of runs 1 and 2 are diffed and REPORTED, never
# asserted: they are separate runs of stateful detection, and rig-18's visit-6 dart is
# a documented run-to-run variance (GROUND-TRUTH.md), so an equality assertion here
# would be a coin-flip red. The structural claim -- nothing reads the model's answer --
# is held by the pure check (unit_check.sh 1510p2), not by racing two replays.
set -u

BIN=/app/build/opendartboard
RUN=/run1510p2
T22=/app/testers/i1499_truth_rig20260922.md
T18=/app/mocks/rig-20260918/GROUND-TRUTH.md

if [ ! -x $BIN ]; then echo "FAIL no $BIN"; exit 1; fi
if [ ! -s $T22 ] || [ ! -s $T18 ]; then echo "FAIL a truth table is missing"; exit 1; fi

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

echo "=== 1. rig-20260922, guard at its default (control) ==="
run_detector rig-20260922 rig22-off
OFF_LINES=$(grep -c 'I1510P2 MODEL' "$RUN/rig22-off.txt")
if [ "$OFF_LINES" -ne 0 ]; then
    echo "FAIL OD_MODEL_SCORE unset printed $OFF_LINES I1510P2 lines; the default is not off"
    exit 1
fi
echo "OK   the control printed zero I1510P2 lines"

echo "=== 2. rig-20260922, OD_MODEL_SCORE=on ==="
run_detector rig-20260922 rig22-on OD_MODEL_SCORE=on
python3 /app/testers/i1510p2_census.py --log "$RUN/rig22-on.txt" \
    --truth $T22 --fixture "mocks/rig-20260922 (the current rig)" || exit 1

echo "=== 3. rig-20260918, OD_MODEL_SCORE=on ==="
run_detector rig-20260918 rig18-on OD_MODEL_SCORE=on
python3 /app/testers/i1510p2_census.py --log "$RUN/rig18-on.txt" \
    --truth $T18 --fixture "mocks/rig-20260918 (the ground-truthed rig)" || exit 1

echo "=== published scores, control vs shadowed run (reported, not asserted) ==="
grep 'SCORE:' "$RUN/rig22-off.txt" | sed 's/.*SCORE:/SCORE:/;s/ | Processing.*//' > "$RUN/rig22-off.scores"
grep 'SCORE:' "$RUN/rig22-on.txt"  | sed 's/.*SCORE:/SCORE:/;s/ | Processing.*//' > "$RUN/rig22-on.scores"
if diff "$RUN/rig22-off.scores" "$RUN/rig22-on.scores" > "$RUN/rig22.scorediff"; then
    echo "identical: $(wc -l < "$RUN/rig22-off.scores") published lines byte-for-byte"
else
    echo "differ (run-to-run detection variance is documented; the guard cannot read a score):"
    cat "$RUN/rig22.scorediff"
fi

# The instrument, asserted: the census must have compared something on every camera.
for f in rig22-on rig18-on; do
    for c in 1 2 3; do
        if ! grep -q "I1510P2 MODEL cam=$c " "$RUN/$f.txt"; then
            echo "FAIL no shadow line from camera $c in $f -- the census never heard it"
            exit 1
        fi
    done
done
echo "OK   every camera of both fixtures produced shadow lines"
echo "I1510P2 DONE logs and censuses under $RUN"
exit 0
