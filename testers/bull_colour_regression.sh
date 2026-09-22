#!/bin/bash
# Run inside the Linux build environment, after cmake --build build.
# Pass the maintainer's saved empty-board images to test full calibration too.
set -euo pipefail
cd "$(dirname "$0")/.."
g++ -std=c++17 -O1 -I src -I src/utils -I src/detector/geometry/calibration \
    testers/bull_colour_regression.cpp \
    build/CMakeFiles/opendartboard.dir/src/detector/geometry/calibration/*.cpp.o \
    $(pkg-config --cflags --libs opencv4) -o build/bull_colour_regression
build/bull_colour_regression "$@"
