set -u
# #1458, inside the container: the reprojection line, and no-worse on the shipped mocks.
#
# THE DEFECT. perspective_processing.cpp computed the PnP reprojection error only under
# enableDebug, and its one consumer was a putText on a debug JPEG -- so no transcript said
# how well a camera's fit fitted, and a geometry change could not be graded.
#
# THE DECISION (maintainer, 2026-09-23), and where each point is held:
#   1  mean AND worst, per camera, not RMS; the mean is the number the JPEG draws. The
#      line carries both; the JPEG draws the same variable the line logs (source, not here).
#   2  one INFO line per camera on success -- section A, read from a run without --debug.
#   3  the correspondence count on the same line (imagePoints.size(), the mean's
#      denominator, not the 120-ray census) -- section A reads it, section C holds it.
#   4  baseline all three fixtures, assert no-worse on the mocks only. On the mocks
#      nothing retries and nothing is refused, so the first-pass fit is deterministic
#      (section B measures that rather than assuming it) and section C asserts with NO
#      tolerance. The two rig fixtures are run and their lines PRINTED as `I1458 RECORD`,
#      and asserted on nothing: a tolerance for them is a later slice, chosen from
#      evidence. Their baseline is written below beside the mocks'.
#
# THE BASELINE, recorded on the first run of this tester (issue-1458-w191 over fork main
# e527687, dev build, calibration-only, OD_MAX_CYCLES=1):
#                  camera 1                camera 2                camera 3   (mean / worst px)
#   mocks          23.421739 / 29.360687   23.938600 / 33.342636   23.843451 / 31.499226
#   rig-20260918   32.989323 / 52.896242   32.591181 / 49.115719   33.238353 / 48.399773
#   rig-20260922   32.567565 / 47.014513   32.181304 / 41.335308   33.526866 / 52.433329
# every one over 20 points: one ring (the outer doubles) at each of the twenty wires. The
# rig-20260922 dev start takes further looks, and a look that passes says the line too
# (#1457's rule for a passing look's fit line), so its camera 1 says it seven times (looks
# 25..31) and camera 2 twice (looks 9, 10); the row above is each camera's SEALED look (28,
# 9, and camera 3's averaged frame). Every figure is about a camera's own twenty
# correspondences; a mean of ~23-33 px on boards of 200-320 px radius is what the pose
# fitted today scores, and whether that is the model or the fit is not this slice's call.
#
# "No worse" is mean <= baseline mean AND worst <= baseline worst AND the same number of
# points: a lower mean over fewer correspondences is the flattering fit point 3 is about.
#
# The script ends on `exit`, never on an `echo`: #1463, #1479.

BIN=/app/build/opendartboard
RUN=/run1458
FAIL=0
note() { if [ "$1" -eq 0 ]; then echo "ok   $2"; else echo "FAIL $2"; FAIL=$((FAIL + 1)); fi; }

if [ ! -x $BIN ]; then
  echo "FAIL no $BIN -- a fresh worktree has none. Build it: testers/run_all.sh 1458"
  exit 1
fi
NEWEST=$(find /app/src /app/CMakeLists.txt -newer $BIN 2>/dev/null | head -5)
if [ -n "$NEWEST" ]; then
  echo "FAIL $BIN is OLDER than the source it is supposed to be measuring:"
  echo "$NEWEST"
  exit 1
fi

# camera mean worst points, per camera, as the detector wrote them. A baseline edit is a
# decision about the fit and belongs in the commit that makes the fit better.
BASELINE="1 23.421739 29.360687 20
2 23.938600 33.342636 20
3 23.843451 31.499226 20"

calibrate_once() { # $1 fixture dir, $2 out file
  local dir="$1" out="$2"
  rm -rf $RUN/cache $RUN/debug_frames
  ( cd $RUN && env OD_MAX_CYCLES=1 timeout 1200 $BIN \
      --cams "$dir/cam_1.mp4,$dir/cam_2.mp4,$dir/cam_3.mp4" \
      --width 1280 --height 720 > "$out.raw" 2>&1 )
  local rc=$?
  sed 's/\x1b\[[0-9;]*m//g' "$out.raw" > "$out"
  rm -f "$out.raw"
  return $rc
}

# "Camera N reprojection: M px mean, W px worst, over P points" -> "N M W P", in log order.
lines_of() {
  sed -n 's/.*Camera \([0-9][0-9]*\) reprojection: \([0-9.]*\) px mean, \([0-9.]*\) px worst, over \([0-9][0-9]*\) points.*/\1 \2 \3 \4/p' "$1"
}

echo "---- the runs ----"
calibrate_once /app/mocks $RUN/mocks.1.log; RC1=$?
calibrate_once /app/mocks $RUN/mocks.2.log; RC2=$?
calibrate_once /app/mocks/rig-20260918 $RUN/r18.log; RC18=$?
calibrate_once /app/mocks/rig-20260922 $RUN/r22.log; RC22=$?
[ $RC1 = 0 ] && [ $RC2 = 0 ] && [ $RC18 = 0 ] && [ $RC22 = 0 ]
note $? "every calibration run ended rc 0 (mocks $RC1 $RC2, rig-20260918 $RC18, rig-20260922 $RC22)"
grep -q "DEBUG_SEEK_VIDEO: video 1 seeked forward" $RUN/mocks.1.log
note $? "the binary carries the dev seek, so the frame the baseline was recorded on is the one measured"

lines_of $RUN/mocks.1.log > $RUN/mocks.1.fit
lines_of $RUN/mocks.2.log > $RUN/mocks.2.fit
while read -r c m w p; do echo "I1458 FIT mocks camera $c mean=$m worst=$w points=$p"; done < $RUN/mocks.1.fit
for f in r18 r22; do
  lines_of $RUN/$f.log | while read -r c m w p; do echo "I1458 RECORD $f camera $c mean=$m worst=$w points=$p"; done
done

echo
echo "---- A. one INFO line per camera, at default level ----"
[ "$(cut -d' ' -f1 $RUN/mocks.1.fit | tr '\n' ' ')" = "1 2 3 " ]
note $? "the mocks run, without --debug, says the line once for each of cameras 1, 2 and 3 ($(wc -l < $RUN/mocks.1.fit) line(s))"
grep -q "\[INFO\].*Camera 1 reprojection: " $RUN/mocks.1.log
note $? "and it says it at INFO"

echo
echo "---- B. the mocks fit is deterministic, so no tolerance is needed ----"
cmp -s $RUN/mocks.1.fit $RUN/mocks.2.fit && [ -s $RUN/mocks.1.fit ]
note $? "two runs of the mocks write the same three lines to the last digit"

echo
echo "---- C. no worse than the baseline on the mocks ----"
for c in 1 2 3; do
  now="$(awk -v c=$c '$1 == c' $RUN/mocks.1.fit)"
  was="$(awk -v c=$c '$1 == c' <<< "$BASELINE")"
  awk -v now="$now" -v was="$was" 'BEGIN {
      split(now, n, " "); split(was, b, " ")
      exit !(now != "" && was != "" && n[2] + 0 <= b[2] + 0 && n[3] + 0 <= b[3] + 0 && n[4] == b[4]) }'
  note $? "camera $c: now ($now) against the baseline ($was) as camera mean worst points -- mean and worst no higher, points the same"
done

echo
if [ "$FAIL" -gt 0 ]; then echo "RESULT: $FAIL failure(s)"; exit 1; fi
echo "RESULT: every camera says its reprojection mean, worst and point count at INFO, and the mocks fit is no worse than its recorded baseline"
exit 0
