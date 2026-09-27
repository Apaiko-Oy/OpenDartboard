set -u
# #1654, inside the container: the wire comb on every look of every camera, fixture and
# window. Builds testers/i1654_comb_census.cpp against this tree's calibration sources
# and runs it on each clip at its own camera index (which decides the dev seek), at the
# board's own look spacing and default budget, read out of geometry_detector.cpp.
# The script ends on `exit`, never on an `echo`: #1463, #1479.
RUN=/run1654
GD=/app/src/detector/geometry/geometry_detector.cpp
LOOKS="$(grep -oE 'constexpr int kFurtherLooksPastAStandingDart = [0-9]+' $GD | grep -oE '[0-9]+$')"
SPACING="$(grep -oE 'constexpr int kFramesBetweenLooks = [0-9]+' $GD | grep -oE '[0-9]+$')"
[ -n "$LOOKS" ] && [ -n "$SPACING" ] || { echo "FAIL could not read the look budget/spacing"; exit 2; }
g++ -std=c++17 -O1 -I /app/src -I /app/src/utils -I /app/src/detector/geometry/calibration \
  -o $RUN/comb_census /app/testers/i1654_comb_census.cpp \
  /app/src/detector/geometry/calibration/*.cpp $(pkg-config --cflags --libs opencv4) \
  > $RUN/build_census.log 2>&1 || { echo "FAIL could not build the comb census"; tail -20 $RUN/build_census.log; exit 2; }
echo "=== $LOOKS looks, $SPACING capture cycles apart, after the 30-frame average ==="
for fx in ${OD_I1654_FIXTURES:-rig-20260922 rig-20260918}; do
  for c in 1 2 3; do
    $RUN/comb_census /app/mocks/$fx/cam_$c.mp4 $((c - 1)) "$LOOKS" "$SPACING" 30 2>/dev/null \
      | grep '^I1654' | tee $RUN/$fx.cam$c.txt
  done
done
exit 0
