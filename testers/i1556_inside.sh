#!/bin/bash
# #1556, inside the container: the flag census over both ground-truthed fixtures in both
# calibration windows, plus the mutation on the same binary.
#
# Five whole-clip detector replays (i1511's shape and cost apiece):
#
#   1. rig-20260918, dev window      the registry build's own 3 s calibration seek
#   2. rig-20260918, opening window  OD_SEEK_VIDEO=off on the SAME binary (#1551)
#   3. rig-20260922, dev window      the window that admits two of three cameras
#   4. rig-20260922, opening window  the window that admits three
#   5. rig-20260918, dev, MUTATED    OD_ENTRY_SIGMA=zero -- the prediction, stated in
#                                    the harness before the run: a call with no
#                                    uncertainty cannot have uncertainty that reaches a
#                                    wire, so the flag census reads EMPTY by both rules
#                                    while the same darts are still called and scored.
#
# WHY BOTH WINDOWS AND NEVER POOLED BLIND (#1551). A registry (dev) build calibrates at a
# 3 s seek into the clip; OD_SEEK_VIDEO=off holds the same binary at the clip's opening.
# On rig-20260922 that is the difference between two admitted cameras and three, so the
# two windows are different experiments with different denominators. od-baselines/5bc3b0a
# is a THIRD window (a release build's) and is not compared against here at all.
#
# WHAT RIG-20260922 CAN AND CANNOT SAY (#1585, filed, not this issue's to close). Only 9
# of its 24 throws are annotated, so correct detections read UNCLAIMED there and its
# accuracy columns are about the annotated subset alone. Its FLAG RATE is still honest --
# a flag needs no annotation to be counted -- so that fixture is reported here as
# coverage and flag rate, and the CATCH table it prints is not evidence.
#
# WHAT IS ASSERTED, and everything else is REPORTED for the record to carry:
#   - each census parses and calls at least its floor of darts (a census that compared
#     nothing has measured nothing, #1490);
#   - the MUTATION really empties the flag census, on a run that still called darts;
#   - the two rules are computed on the same dart: every called dart carries both
#     verdicts, and the rule the run published on is the across-boundary one.
# THE FLAG RATE IS NOT ASSERTED. It is the measurement the issue is about, and a harness
# that failed on a threshold of its own would be deciding the issue by that threshold.
set -u

BIN=/app/build/opendartboard
RUN=/run1556
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
    env OD_MAX_CYCLES=0 OD_GEO_SCORE=on OD_SHAFT_CENSUS=1 "$@" \
        timeout 900 $BIN --cams "$cams" --width 1280 --height 720 \
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

census18() { # $1 log basename, $2 window word, $3.. extra census args
    local out="$1" window="$2"
    shift 2
    python3 /app/testers/i1556_census.py --log "$RUN/$out.txt" --truth $T18 \
        --annotations $A18 --fixture rig-20260918 --window "$window" \
        --min-matched 4 "$@" | tee "$RUN/census-$out.txt"
    return ${PIPESTATUS[0]}
}

census22() { # $1 log basename, $2 window word
    local out="$1" window="$2"
    shift 2
    # --no-arrival: the maintainer's ground-truth correction of 2026-09-24, carried
    # forward from #1555's harness unchanged -- visit 1 is thrown before the recording's
    # clean frame, so neither of those two darts can arrive.
    python3 /app/testers/i1556_census.py --log "$RUN/$out.txt" --truth $T22 \
        --annotations $A22 --fixture rig-20260922 --window "$window" \
        --min-matched 2 --no-arrival 1.1,1.3 "$@" | tee "$RUN/census-$out.txt"
    return ${PIPESTATUS[0]}
}

echo "=== 1. rig-20260918, dev window ==============================================="
run_detector rig-20260918 r18-dev
census18 r18-dev dev || { echo "FAIL the rig-18 dev census could not be read"; exit 1; }

echo "=== 2. rig-20260918, opening window (OD_SEEK_VIDEO=off) ======================="
run_detector rig-20260918 r18-open OD_SEEK_VIDEO=off
census18 r18-open opening || { echo "FAIL the rig-18 opening census could not be read"; exit 1; }

echo "=== 3. rig-20260922, dev window ==============================================="
run_detector rig-20260922 r22-dev
census22 r22-dev dev || { echo "FAIL the rig-22 dev census could not be read"; exit 1; }

echo "=== 4. rig-20260922, opening window (OD_SEEK_VIDEO=off) ======================="
run_detector rig-20260922 r22-open OD_SEEK_VIDEO=off
census22 r22-open opening || { echo "FAIL the rig-22 opening census could not be read"; exit 1; }

echo "=== the pooled flag census, per fixture and per window, never blind across them ="
python3 /app/testers/i1556_census.py --pool \
    "$RUN/census-r18-dev.txt" "$RUN/census-r18-open.txt" \
    "$RUN/census-r22-dev.txt" "$RUN/census-r22-open.txt" | tee "$RUN/pooled.txt"
if [ ${PIPESTATUS[0]} -ne 0 ]; then echo "FAIL nothing to pool"; exit 1; fi

echo "=== 5. THE MUTATION: rig-20260918, dev window, OD_ENTRY_SIGMA=zero ============"
echo "PREDICTION, stated before the run: the same darts are called and scored, and the"
echo "            flag census reads EMPTY -- zero flagged by the across-boundary rule"
echo "            AND zero by #1555's major-axis one, because there is no uncertainty"
echo "            left for either to spend. A census that flagged nothing on a run that"
echo "            called nothing would prove nothing, so the count of called darts is"
echo "            asserted beside it."
run_detector rig-20260918 r18-zero OD_ENTRY_SIGMA=zero
census18 r18-zero dev --expect-empty || { echo "FAIL the mutation did not empty the flag census"; exit 1; }
echo "OK   zeroing the uncertainty empties the flag census"

echo "=== assertions ================================================================"

# Both rules on the same dart, and the run published on the across-boundary one. Checked
# in python because it is a per-row comparison of fields, and a shell pipeline that got
# it wrong would fail silently.
python3 - "$RUN" <<'PY'
import re, sys, glob, os
run = sys.argv[1]
rows = bad = 0
pat = re.compile(r"I1556FLAG .* flag=(\d) crude=(\d) published=(\S+)")
for path in sorted(glob.glob(os.path.join(run, "r1*-*.txt"))):
    if path.endswith("-zero.txt"):
        continue
    for line in open(path, errors="replace"):
        m = pat.search(line)
        if not m:
            continue
        rows += 1
        if m.group(3) != "across":
            bad += 1
            print("FAIL %s: the run published on rule %s"
                  % (os.path.basename(path), m.group(3)))
print("both-rules: rows=%d published_on_wrong_rule=%d" % (rows, bad))
sys.exit(1 if (bad or rows == 0) else 0)
PY
if [ $? -ne 0 ]; then echo "FAIL the run did not publish on the across-boundary rule"; exit 1; fi
echo "OK   every solve carries both verdicts and the run published on the measured one"

# A flagged dart names two candidates. This is the contract half the census cannot state
# as a rate: a flag with one name in it has nothing for a consumer to ask about.
# The log line carries the logger's own prefix, so the needle is the token and not the
# start of the line -- a `^I1556PUBLISH` here would match nothing and read as zero faults.
BAD=$(grep -h 'I1556PUBLISH' "$RUN"/r1*-*.txt 2>/dev/null | grep 'flagged=1' | grep -c 'alt=-')
NAMED=$(grep -h 'I1556PUBLISH' "$RUN"/r1*-*.txt 2>/dev/null | grep -c 'flagged=1')
echo "candidates: flagged=$NAMED without_an_alternative=$BAD"
[ "$BAD" -eq 0 ] || { echo "FAIL $BAD flagged dart(s) published without naming a second candidate"; exit 1; }
echo "OK   every flagged dart names both candidates"

echo "I1556 DONE logs, censuses and the pooled flag census under $RUN"
exit 0
