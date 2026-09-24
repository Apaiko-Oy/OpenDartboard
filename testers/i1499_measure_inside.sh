#!/bin/bash
# #1499, inside the container: compile the band census and the #1485 radius census and
# run both over every camera of the three fixtures, so the fit residual and the three
# band positions are measured in one pass. Measurement only: it asserts nothing.
#
# unrun-tester: a measurement harness, not a check -- it says so above and exits 0
# whatever the numbers say, so a suite run of it could measure nothing. It is kept as the
# way to re-take #1499's RAW/PIPE/FIT band read (and #1485's radius census beside it) in
# one pass over every camera of the three fixtures when a treble-band question comes back;
# the repair those measurements decided is guarded in the gate by 1485-rings, and
# ellipse_processing.cpp/.hpp carry the measured constants with the band census's name
# beside them. Marked by #1534.
set -u

OUT=/run1499
mkdir -p "$OUT"

build() { # $1 source, $2 binary
    g++ -std=c++17 -O1 -I /app/src -I /app/src/utils -I /app/src/detector/geometry/calibration \
        -o "$2" "$1" \
        /app/src/detector/geometry/calibration/*.cpp \
        /app/src/detector/geometry/detection/score_processing.cpp \
        /app/src/detector/geometry/detection/dart_processing.cpp \
        /app/src/detector/geometry/detection/motion_processing.cpp \
        $(pkg-config --cflags --libs opencv4) > "$2.build.log" 2>&1
}

echo "=== building ==="
if ! build /app/testers/i1499_band_census.cpp "$OUT/band"; then
    tail -30 "$OUT/band.build.log"
    echo "FAIL band census did not build"
    exit 2
fi
if ! build /app/testers/i1485_radius_census.cpp "$OUT/radius"; then
    tail -30 "$OUT/radius.build.log"
    echo "FAIL radius census did not build"
    exit 2
fi

for dir in rig-20260918 rig-20260922 ""; do
    tag="${dir:-mocks}"
    base="/app/mocks${dir:+/$dir}"
    mkdir -p "$OUT/dbg_$tag"
    cd "$OUT/dbg_$tag"
    for c in 1 2 3; do
        clip="$base/cam_$c.mp4"
        echo "--- $tag cam_$c ---"
        "$OUT/band" "$clip" $((c - 1)) "$OUT/${tag}_cam${c}.jpg" 2>/dev/null > "$OUT/${tag}_cam${c}.rays"
        grep '^I1499 ' "$OUT/${tag}_cam${c}.rays"
        "$OUT/radius" "$clip" $((c - 1)) 1 90 2>/dev/null | grep '^I1485 '
    done
    cd /
done
exit 0
