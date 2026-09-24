#!/bin/bash
# Run inside the Linux build environment, after cmake --build build.
# Pass the maintainer's saved empty-board images to test full calibration too.
#
# unrun-tester: the hand-run half. The synthetic assertions in bull_colour_regression.cpp
# are in the gate (run_all.sh bull-colour, via unit_check.sh; registered by #1534) -- what
# this driver adds is the optional image mode above, and the saved empty-board images it
# takes live on the maintainer's box, not in this repository. It also compiles with the
# HOST's g++ against build/'s objects where every registered tester runs in $OD_IMAGE, so
# a row for it could only repeat the bull-colour row less portably. Kept, not deleted, so
# the image invocation is not lost.
set -euo pipefail
cd "$(dirname "$0")/.."
g++ -std=c++17 -O1 -I src -I src/utils -I src/detector/geometry/calibration \
    testers/bull_colour_regression.cpp \
    build/CMakeFiles/opendartboard.dir/src/detector/geometry/calibration/*.cpp.o \
    $(pkg-config --cflags --libs opencv4) -o build/bull_colour_regression
build/bull_colour_regression "$@"
