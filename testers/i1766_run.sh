#!/bin/bash
# #1766's harness: the two-line census -- the pure check on planted boards, then one
# whole-clip replay of rig-20260929 (the fixture whose physical cameras 1 and 3 are the
# live log's 2 and 3) with the census over every two-line solve it makes.
#
#   testers/run_all.sh 1766        build the tree and run this
#   testers/i1766_run.sh           run it against this checkout (OD_SKIP_BUILD=1 reuses build/)
#
# Two sections:
#   1. unit_check.sh 1766: the sigma is the crossing (A^-1 diag(s^2) A^-T to 2%), the live
#      1.08-sigma shape reads SOLVED under crossingSigmas 1.0 and flagged under 1.25, the
#      sentence carries whose sigma it is, the control verdict on two lines.
#   2. the replay (OD_GEO_SCORE=on OD_SHAFT_CENSUS=1, i1555's pins and clock) and
#      i1766_census.py over it, asserting only what a census can: at least 3 two-line
#      solves parsed (rig-20260929 dev makes 6), every one with redundancy 0.000 on both
#      lines, and the recomputed major axis within 5% of the claimed one on every row.
#      What the vote read and what OD_SOLVE_CONTROL=on would have done are REPORTED.
#
# MEASURED 2026-10-10 on the 4-core box beside a running bakeoff: see run_all.sh's line.
# The script ends on `exit`, never on an `echo`: #1463.
set -u
. "$(dirname "${BASH_SOURCE[0]}")/tester_paths.sh"

RUN="$OD_RUNS_BASE/1766"
rm -rf "$RUN" 2>/dev/null
mkdir -p "$RUN"
START=$(date +%s)

echo "=== 1. the pure check on planted boards ======================================="
bash "$(dirname "${BASH_SOURCE[0]}")/unit_check.sh" 1766 2>&1 | tee "$RUN/unit.txt"
RC1=${PIPESTATUS[0]}
if [ $RC1 -ne 0 ]; then
  echo "FAIL the pure check did not pass (rc=$RC1)"
  echo "RUN=1766 rc=$RC1 seconds=$(( $(date +%s) - START )) load_at_end=$(cut -d' ' -f1 /proc/loadavg)"
  exit $RC1
fi

echo "=== 2. rig-20260929, dev window, the census over its two-line solves =========="
export OD_MOTION_CLOCK="${OD_MOTION_CLOCK:-capture}"
od_run "1766" --cpus=2 --network none -e HOME=/root -e OD_MOTION_CLOCK \
  -v "$OD_TREE_ROOT":/app -v "$RUN":/run1766 -w /run1766 \
  "$OD_IMAGE" bash -c '
    set -u
    BIN=/app/build/opendartboard
    if [ ! -x $BIN ]; then echo "FAIL no $BIN"; exit 1; fi
    dir=/app/mocks/rig-20260929
    env OD_MAX_CYCLES=0 OD_GEO_SCORE=on OD_SHAFT_CENSUS=1 \
        timeout 900 $BIN --cams "$dir/cam_1.mp4,$dir/cam_2.mp4,$dir/cam_3.mp4" --width 1280 --height 720 \
        > /run1766/r29-dev.out 2>&1
    rc=$?
    sed "s/\x1b\[[0-9;]*m//g" /run1766/r29-dev.out > /run1766/r29-dev.txt
    echo "detector r29-dev rc=$rc lines=$(wc -l < /run1766/r29-dev.txt)"
    if [ $rc -ne 0 ] && [ $rc -ne 124 ]; then tail -5 /run1766/r29-dev.txt; echo "FAIL the replay did not finish"; exit 1; fi
    python3 /app/testers/i1766_census.py --log /run1766/r29-dev.txt --min-two-line 3 | tee /run1766/census.txt
    [ ${PIPESTATUS[0]} -eq 0 ] || { echo "FAIL the census could not be read"; exit 1; }
    python3 - /run1766/census.txt <<"EOF"
import re, sys
rows = [l for l in open(sys.argv[1]) if l.startswith("I1766 TWO-LINE ") and "window cams" not in l]
bad = 0
for l in rows:
    f = l.split()
    resid = float(f[11].rstrip("%"))
    red = f[-1]
    used = [x for x in red.split("/") if x != "-"]
    if abs(resid) > 5.0:
        print("FAIL window %s: the claimed major axis is %.1f%% off the two lines own arithmetic" % (f[2], resid)); bad += 1
    if len(used) != 2 or any(float(x) > 1e-6 for x in used):
        print("FAIL window %s: a two-line solve whose redundancy numbers are not 0/0: r=%s" % (f[2], red)); bad += 1
print("I1766 CHECKED %d two-line row(s): every claimed sigma is its two lines and their crossing, every redundancy 0.000" % len(rows) if bad == 0 else "I1766 CHECKED %d row(s), %d bad" % (len(rows), bad))
sys.exit(1 if bad else 0)
EOF
  ' 2>&1 | tee "$RUN/out.txt"
RC=${PIPESTATUS[0]}
END=$(date +%s)
echo "RUN=1766 rc=$RC seconds=$((END - START)) load_at_end=$(cut -d' ' -f1 /proc/loadavg)"
exit $RC
