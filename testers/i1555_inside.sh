#!/bin/bash
# #1555, inside the container: the side-by-side census that decides which path publishes.
#
# Five whole-clip detector replays (i1511's shape and cost apiece), then one pooled
# tally. Every run is made with OD_GEO_SCORE=on OD_SHAFT_CENSUS=1 so both paths answer
# on the same dart in the same process -- which is the only way the comparison is not a
# comparison of two runs.
#
#   1. rig-20260918, dev window      the registry build's own 3 s calibration seek
#   2. rig-20260918, opening window  OD_SEEK_VIDEO=off on the SAME binary (#1551)
#   3. rig-20260922, dev window      the window that admits two of three cameras
#   4. rig-20260922, opening window  the window that admits three
#   5. rig-20260918, dev, PINNED     OD_SCORE_PATH=vote -- the losing path, reachable
#
# WHY BOTH WINDOWS AND NEVER POOLED BLIND (#1551). A registry (dev) build calibrates at a
# 3 s seek into the clip; OD_SEEK_VIDEO=off holds the same binary at the clip's opening.
# On rig-20260922 that is the difference between two admitted cameras and three, and the
# opening window reaches a whole visit the dev window loses -- so the two windows are
# different experiments with different denominators, and a single figure over both would
# be a number about neither. od-baselines/5bc3b0a is a THIRD window (a release build's)
# and is not compared against here at all.
#
# WHAT IS ASSERTED, and everything else is REPORTED for the record to carry:
#   - each census parses and matches at least its floor (a census that compared nothing
#     has measured nothing, #1490);
#   - the PIN really restores the pre-#1555 binary: every dart of run 5 publishes by
#     path=vote, none reports a degradation, and the published column equals the vote's;
#   - the WIRING is the decision, on every run: the published score of every matched
#     dart is the column its own `path=` field names. A build whose published score is
#     not the path it says it took has a wiring bug that no accuracy figure would show.
#   - (#1587) every ACCURACY line partitions its arrivals exactly: correct, wrong
#     score, undetected, off-board scored and ambiguous sum to the denominator.
# ACCURACY IS NOT ASSERTED. It is the measurement the maintainer's rule is applied to,
# and a harness that fails on it would be deciding the issue by its own threshold.
set -u

BIN=/app/build/opendartboard
RUN=/run1555
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
    python3 /app/testers/i1555_census.py --log "$RUN/$out.txt" --truth $T18 \
        --annotations $A18 --fixture rig-20260918 --window "$window" \
        --min-matched 4 "$@" | tee "$RUN/census-$out.txt"
    return ${PIPESTATUS[0]}
}

census22() { # $1 log basename, $2 window word
    local out="$1" window="$2"
    shift 2
    # --no-arrival: the maintainer's ground-truth correction of 2026-09-24. Visit 1 is
    # thrown before the recording's clean frame -- the 8 is #1514's parked dart, on the
    # board from frame one -- so it cannot arrive, its absence is a fact about the
    # footage, and an event matched to its annotation is itself suspect and stays out of
    # every denominator here. Visit 1's third dart is the miss: no annotation, nothing to
    # name (#1585).
    python3 /app/testers/i1555_census.py --log "$RUN/$out.txt" --truth $T22 \
        --annotations $A22 --fixture rig-20260922 --window "$window" \
        --min-matched 2 --no-arrival 1.1 "$@" | tee "$RUN/census-$out.txt"
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

echo "=== 5. the pin: rig-20260918, dev window, OD_SCORE_PATH=vote =================="
run_detector rig-20260918 r18-pin OD_SCORE_PATH=vote
census18 r18-pin dev || { echo "FAIL the pinned census could not be read"; exit 1; }

echo "=== the pooled tally, per fixture and per window, never blind across them ====="
python3 /app/testers/i1555_census.py --pool \
    "$RUN/census-r18-dev.txt" "$RUN/census-r18-open.txt" \
    "$RUN/census-r22-dev.txt" "$RUN/census-r22-open.txt" | tee "$RUN/pooled.txt"
if [ ${PIPESTATUS[0]} -ne 0 ]; then echo "FAIL nothing to pool"; exit 1; fi

echo "=== assertions ================================================================"

# The pin. Every matched dart of run 5 must publish by the vote, with no degradation
# reported -- a vote-only board has not fallen back, it never asked.
PIN_ROWS=$(grep -c '^I1555 PAIR ' "$RUN/census-r18-pin.txt")
PIN_NOT_VOTE=$(grep '^I1555 PAIR ' "$RUN/census-r18-pin.txt" | grep -vc 'path=vote')
PIN_DEGRADED=$(grep '^I1555 PAIR ' "$RUN/census-r18-pin.txt" | grep -c 'degraded=1')
PIN_TALLY=$(grep '^I1555 TALLY ' "$RUN/census-r18-pin.txt" | head -1)
PIN_VOTE=$(echo "$PIN_TALLY" | sed -n 's/.*vote_exact=\([0-9]*\).*/\1/p')
PIN_PUB=$(echo "$PIN_TALLY" | sed -n 's/.*published_exact=\([0-9]*\).*/\1/p')
echo "pin: rows=$PIN_ROWS not_vote=$PIN_NOT_VOTE degraded=$PIN_DEGRADED vote_exact=$PIN_VOTE published_exact=$PIN_PUB"
[ "$PIN_ROWS" -gt 0 ] || { echo "FAIL the pinned run matched no darts, so the pin proved nothing"; exit 1; }
[ "$PIN_NOT_VOTE" -eq 0 ] || { echo "FAIL $PIN_NOT_VOTE dart(s) of the pinned run did not publish by the vote"; exit 1; }
[ "$PIN_DEGRADED" -eq 0 ] || { echo "FAIL the pinned run reported $PIN_DEGRADED degradation(s); it never asked the solver"; exit 1; }
[ "$PIN_VOTE" = "$PIN_PUB" ] || { echo "FAIL the pinned run published $PIN_PUB correct where the vote read $PIN_VOTE"; exit 1; }
echo "OK   the pin restores the string vote on this binary, $PIN_ROWS matched dart(s)"

# The wiring is the decision, on every run: the published score of each matched dart is
# the column its own path= field names. Checked in python because it is a per-row
# comparison of three fields, and a shell pipeline that got it wrong would fail silently.
python3 - "$RUN" <<'PY'
import re, sys, glob, os
run = sys.argv[1]
bad = rows = 0
pat = re.compile(r"vote=(\S+) \S+ \| geo=(\S+) (\S+) \| geometry-first=\S+ \| "
                 r"published=(\S+) \S+ path=(\S+)")
for path in sorted(glob.glob(os.path.join(run, "census-*.txt"))):
    for line in open(path, errors="replace"):
        if not line.startswith("I1555 PAIR "):
            continue
        m = pat.search(line)
        if not m:
            continue
        vote, geo, gverdict, published, which = m.groups()
        rows += 1
        want = geo if which == "geometry" else vote
        if published != want:
            bad += 1
            print("FAIL %s: path=%s published %s where that path read %s"
                  % (os.path.basename(path), which, published, want))
print("wiring: rows=%d mismatched=%d" % (rows, bad))
sys.exit(1 if (bad or rows == 0) else 0)
PY
if [ $? -ne 0 ]; then echo "FAIL the published score is not the path the run says it took"; exit 1; fi
echo "OK   every published score is the column its own path= names"

# The wiring is the whole column, not just row by row, and this is the assertion that
# stops the next branch moving it in silence. On a build that wired the geometry the
# PUBLISHED tally must equal GEOMETRY-FIRST exactly; on one that did not it must equal
# VOTE exactly. Either is a verdict somebody took; a build sitting between them has
# changed the publish rule without changing the census, which is the regression this
# tester exists for. Read off the run itself -- whether any dart published by the
# geometry -- so the harness needs to know nothing about the constant in the header.
for name in r18-dev r18-open r22-dev r22-open; do
    C="$RUN/census-$name.txt"
    [ -s "$C" ] || { echo "FAIL $C is missing"; exit 1; }
    T=$(grep '^I1555 TALLY ' "$C" | head -1)
    V=$(echo "$T" | sed -n 's/.*vote_exact=\([0-9]*\).*/\1/p')
    F=$(echo "$T" | sed -n 's/.*first_exact=\([0-9]*\).*/\1/p')
    P=$(echo "$T" | sed -n 's/.*published_exact=\([0-9]*\).*/\1/p')
    G=$(grep -c '^I1555 PAIR .*path=geometry' "$C")
    if [ "$G" -gt 0 ]; then WANT="$F"; WORD="geometry-first"; else WANT="$V"; WORD="the vote"; fi
    echo "column $name: geometry_rows=$G vote_exact=$V first_exact=$F published_exact=$P expecting=$WORD"
    [ "$P" = "$WANT" ] || {
        echo "FAIL $name published $P correct where $WORD reads $WANT -- the published"
        echo "     column is neither path, so the publish rule has moved away from the census"
        exit 1
    }
done
echo "OK   the published column is exactly one of the two paths on every fixture and window"

# #1587: the ACCURACY line is over EVERY arrival, and its buckets must partition that
# denominator exactly -- a dart counted twice or dropped would move the one figure the
# accuracy effort is judged by without any column above noticing. This is about the
# instrument adding up, not about the figure: the figure itself is still not asserted.
for name in r18-dev r18-open r22-dev r22-open; do
    A=$(grep '^I1555 ACCURACY-TALLY ' "$RUN/census-$name.txt" | head -1)
    [ -n "$A" ] || { echo "FAIL census-$name has no ACCURACY-TALLY line"; exit 1; }
    python3 - "$name" "$A" <<'PY' || { echo "FAIL the $name ACCURACY line does not add up"; exit 1; }
import sys
name, line = sys.argv[1], sys.argv[2]
f = dict(t.split("=", 1) for t in line.split() if "=" in t)
parts = sum(int(f[k]) for k in ("correct", "wrong_score", "undetected",
                                "offboard_scored", "ambiguous"))
print("accuracy %s: arrivals=%s buckets=%d" % (name, f["arrivals"], parts))
sys.exit(0 if parts == int(f["arrivals"]) > 0 else 1)
PY
done
echo "OK   every ACCURACY line partitions its arrivals exactly"
grep -h '^I1555 ACCURACY ' "$RUN/census-r18-dev.txt" "$RUN/census-r18-open.txt" \
    "$RUN/census-r22-dev.txt" "$RUN/census-r22-open.txt" "$RUN/pooled.txt"

echo "I1555 DONE logs, censuses and the pooled tally under $RUN"
exit 0
