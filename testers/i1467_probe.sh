set -u
g++ -std=c++17 -O1 -I /app/src -I /app/src/utils -I /app/src/detector/geometry/calibration \
  -o /run/census /app/testers/i1467_fit_census.cpp \
  /app/src/detector/geometry/calibration/*.cpp $(pkg-config --cflags --libs opencv4) \
  > /run/build.log 2>&1 || { tail -30 /run/build.log; echo "BUILD FAILED"; exit 2; }
echo "built"
LOOKS="${LOOKS:-3}"; SPACING="${SPACING:-90}"
for c in 1 2 3; do /run/census /app/mocks/cam_$c.mp4 $((c-1)) $LOOKS $SPACING 2>/dev/null | grep -E '^I1467'; done
for c in 1 2 3; do /run/census /app/mocks/rig-20260918/cam_$c.mp4 $((c-1)) $LOOKS $SPACING 2>/dev/null | grep -E '^I1467'; done
echo DONE
