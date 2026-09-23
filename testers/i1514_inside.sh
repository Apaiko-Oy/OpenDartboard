set -u
# #1514: why detection on mocks/rig-20260922 stalled after the first visit --
# and, since #1518, the check that it stays repaired.
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
# So no camera could ever read CLEAN, `goes_clean` never reached the quorum of
# 2, no takeout was reconciled, no END was published, and after three advances
# the board wedged at DART_3 -- every later window refused "0 up, 0 clean". The
# motion half was healthy the whole clip: ~30 events formed, settled and ENDed.
#
# #1518 repaired the reference -- adoption of the settled scene at every
# reconciled CLEAN, and a takeout read from the DIRECTION of change (the
# reversion vote; the census is on readsAsReversion in dart_processing.hpp) --
# so the two figures this census was filed on are no longer allowed to be zero:
# a run of this fixture where NO window reads CLEAN, or NO END publishes, is
# the wedge back, and it fails below by name instead of reading as a report
# somebody must interpret. Everything else stays #1322's rule: no other number
# the detector produced is asserted, and the read-failures above are unchanged.
#
# The needle's mutation proof: OD_CLEAN_REFERENCE=calibration on the same
# binary restores the pre-#1518 rule, and this census goes red on both figures
# (measured; #1518's report quotes the run).
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
echo "    The last window census lines, so the final state is quoted rather than believed:"
grep -a 'WINDOW CENSUS' $RUN/i1514.txt | tail -3 | sed 's/^/    /'
echo

# #1518: the two figures the stall was measured by may no longer be zero. See the
# header for why this is a check now and what its mutation proof is.
if [ "$CLEAN_WINDOWS" -eq 0 ]; then
  echo "FAIL no window in $WINDOWS saw any camera read CLEAN: the CLEAN reference cannot"
  echo "     recover from this fixture's parked dart again -- #1514's wedge is back."
  exit 1
fi
if [ "$ENDS" -eq 0 ]; then
  echo "FAIL no END was published over the whole clip: no takeout reconciled -- #1514's"
  echo "     wedge is back."
  exit 1
fi
echo "CHECK_RC=0  (this harness fails on a run it could not READ, and on the two figures"
echo "             #1518 repaired: any-camera-CLEAN windows and published ENDs are nonzero)"
exit 0
