set -u
# #1514: why detection on mocks/rig-20260922 stalls after the first visit.
#
# THE FINDING THIS REPRODUCES, so it is a command rather than an hour of log
# reading. The fixture's calibration frames hold a dart PARKED in the board
# (visible in frame 0 of every camera, near the 4/13 wedge boundary); it is
# pulled before the first throw, so from that moment every settled window's
# cumulative diff against the calibration background carries the parked dart's
# silhouette:
#
#   empty-board floor        cam 1  5,767-5,968 px  (2.67-2.76% of its board)
#   (settled empty windows,  cam 2  1,175-1,254 px  (0.55-0.58%)
#   whole clip)              cam 3  1,021-1,175 px  (0.56-0.64%)
#   min over ALL windows     cam 1  4,900 px -- window #1, the pull itself,
#                            before the silhouette's shadow fully developed
#   CLEAN ceiling (0.10%)    cam 1 215 px, cam 2 215 px, cam 3 182 px
#
# So no camera can ever read CLEAN, `goes_clean` never reaches the quorum of 2,
# no takeout is reconciled, no END is published, and after three advances the
# board wedges at DART_3 -- every later window is refused "0 up, 0 clean". The
# motion half is healthy the whole clip: ~30 events form, settle and END.
#
# IT IS A MEASUREMENT AND NOT A CHECK (#1322, i1484's rule): nothing below
# asserts anything about a number the detector produced. It fails only on a run
# it could not READ -- no binary, no fixture, a run that never opened a window.
#
# The script ends on `exit`, never on an `echo`: #1463, #1479.

BIN=/app/build/opendartboard
RIG_DIR=/app/mocks/rig-20260922
RIG=$RIG_DIR/cam_1.mp4,$RIG_DIR/cam_2.mp4,$RIG_DIR/cam_3.mp4
RUN=/run1514

if [ ! -x $BIN ]; then
  echo "FAIL no $BIN -- a fresh worktree has none. Build it: make build (or testers/run_all.sh)."
  exit 1
fi
NEWEST=$(find /app/src /app/CMakeLists.txt -newer $BIN 2>/dev/null | head -5)
if [ -n "$NEWEST" ]; then
  echo "FAIL $BIN is OLDER than the source it is supposed to be measuring:"
  echo "$NEWEST" | sed 's/^/       /'
  exit 1
fi
if [ ! -s $RIG_DIR/cam_1.mp4 ] || [ ! -s $RIG_DIR/cam_2.mp4 ] || [ ! -s $RIG_DIR/cam_3.mp4 ]; then
  echo "FAIL the rig-20260922 fixture is not whole ($RIG_DIR); there is nothing to measure."
  exit 1
fi

echo "############ running the detector on rig-20260922, whole clip, window census on ############"
rm -rf $RUN/cache $RUN/debug_frames
# No cycle budget: the clip ends the run (END OF FOOTAGE), so the census below
# covers the whole fixture. OD_WINDOW_CENSUS=1 prints one line per completed
# dart window with each camera's cumulative board px, which is the figure the
# CLEAN vote reads.
env OD_WINDOW_CENSUS=1 timeout 900 \
  $BIN --cams "$RIG" --width 1280 --height 720 > $RUN/i1514.out 2>&1
RC=$?
sed 's/\x1b\[[0-9;]*m//g' $RUN/i1514.out > $RUN/i1514.txt
echo "    detector rc=$RC lines=$(wc -l < $RUN/i1514.txt)"
if [ $RC -ne 0 ]; then
  echo "FAIL the detector exited $RC; 0 is the status of a run that played the footage out."
  exit 1
fi
if ! grep -aq 'END OF FOOTAGE' $RUN/i1514.txt; then
  echo "FAIL the run did not reach the end of the footage, so the census below would be of"
  echo "     an unknown share of the fixture."
  exit 1
fi

WINDOWS=$(grep -ac 'WINDOW CENSUS' $RUN/i1514.txt)
if [ "$WINDOWS" -eq 0 ]; then
  echo "FAIL the run completed no dart window at all, which is a different failure than the"
  echo "     one this harness measures (#1514 is a run whose windows all refuse to go CLEAN)."
  exit 1
fi

echo
echo "########################################################################"
echo "#  the stall census: what every completed window voted, and the floor"
echo "#  the CLEAN test can never get under"
echo "########################################################################"
echo
# The three figures per line: windows completed, how many had any camera read
# CLEAN, how many published an END (a reconciled takeout).
CLEAN_WINDOWS=$(grep -a 'WINDOW CENSUS' $RUN/i1514.txt | grep -acEv '\(([0-9]+) up, 0 clean')
ENDS=$(grep -ac 'SCORE: END' $RUN/i1514.txt)
SCORES=$(grep -ac 'SCORE: ' $RUN/i1514.txt)
echo "    windows completed:                 $WINDOWS"
echo "    windows where any camera saw CLEAN: $CLEAN_WINDOWS"
echo "    ENDs published:                    $ENDS"
echo "    SCORE lines (darts + ENDs):        $SCORES"
echo
echo "    per-camera cumulative board px across every completed window"
echo "    (min = the floor the CLEAN ceiling of 0.10% of the board is measured against):"
grep -a 'WINDOW CENSUS' $RUN/i1514.txt | awk '
{
    for (c = 1; c <= 3; c++) {
        # each window line carries "camN <STATE> board=<changed>/<total>"
        pat = "cam" c " [A-Z_0-9]+ board="
        if (match($0, pat)) {
            rest = substr($0, RSTART + RLENGTH)
            split(rest, halves, "/")
            changed = halves[1] + 0
            split(halves[2], t, " ")
            total[c] = t[1] + 0
            if (!(c in min) || changed < min[c]) min[c] = changed
            if (!(c in max) || changed > max[c]) max[c] = changed
        }
    }
}
END {
    for (c = 1; c <= 3; c++) {
        if (!(c in min)) { print "    cam" c ": no board figure in any window"; continue }
        ceiling = int(total[c] * 0.001)
        printf "    cam%d: min %d px, max %d px of %d (min = %.2f%% of the board; the CLEAN ceiling is %d px)\n",
               c, min[c], max[c], total[c], 100.0 * min[c] / total[c], ceiling
    }
}'
echo
echo "    The last window census lines, so the wedged state is quoted rather than believed:"
grep -a 'WINDOW CENSUS' $RUN/i1514.txt | tail -3 | sed 's/^/    /'
echo
echo "CHECK_RC=0  (this harness fails on a run it could not READ, never on what the run said)"
exit 0
