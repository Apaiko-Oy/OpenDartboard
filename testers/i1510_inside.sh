#!/bin/bash
set -eu
export OD_1510_REQUIRE_ACCEPTED="${OD_1510_REQUIRE_ACCEPTED:-1}"
SRC=/app/src/detector/geometry/calibration
OBJECTS=$(find /app/build/CMakeFiles/opendartboard.dir/src/detector/geometry/calibration -name '*.o')
if [ -z "$OBJECTS" ] || [ ! -s /app/build/opendartboard ]; then
  echo 'FAIL build this checkout first; no calibration objects'; exit 2
fi
if [ -n "$(find /app/src /app/CMakeLists.txt -newer /app/build/opendartboard -print -quit)" ]; then
  echo 'FAIL build is older than its source'; exit 2
fi
for tool in model_check probe; do
  g++ -std=c++17 -O2 -I/app/src -I/app/src/utils /app/testers/i1510_${tool}.cpp \
    $OBJECTS $(pkg-config --cflags --libs opencv4) -o /out/$tool
done
/out/model_check /out/synthetic
# Mutation proof: a reporter that omits the held-out gate must fail its negative control.
sed 's/out.quality.heldOutP95Mm>seed.profile.boundaryToleranceMm/false/' \
  "$SRC/board_model.cpp" > /out/board_model_mutant.cpp
INCLUDES=$(sed -n 's/^CXX_INCLUDES = //p' /app/build/CMakeFiles/opendartboard.dir/flags.make)
g++ -std=c++17 -O2 $INCLUDES -I/app/src -I/app/src/utils -I"$SRC" \
  $(pkg-config --cflags opencv4) -c /out/board_model_mutant.cpp -o /out/board_model_mutant.o
OTHERS=$(printf '%s\n' $OBJECTS | grep -v '/board_model.cpp.o$')
g++ -std=c++17 -O2 -I/app/src /app/testers/i1510_model_check.cpp \
  /out/board_model_mutant.o $OTHERS $(pkg-config --cflags --libs opencv4) -o /out/mutant
if /out/mutant /out/mutant-cache > /out/mutant.txt 2>&1; then
  echo 'FAIL removing the held-out gate did not fail a check'; exit 1
fi
grep -q '^FAIL held-out observations' /out/mutant.txt || {
  cat /out/mutant.txt; echo 'FAIL mutant failed for an unrelated reason'; exit 1;
}
echo 'OK removing held-out acceptance is caught'
echo "RIG ACCEPTANCE: all three models must pass unless explicitly in measurement mode"
/out/probe /out/rig 3000 /app/mocks/rig-20260918/cam_1.mp4 \
  /app/mocks/rig-20260918/cam_2.mp4 /app/mocks/rig-20260918/cam_3.mp4
# The user selected these last rig clips. No upstream mock is substituted.
# See docs/physical-board-model.md for measured acceptance and remaining limitations.
echo 'OK synthetic controls and selected rig acceptance'
exit 0
