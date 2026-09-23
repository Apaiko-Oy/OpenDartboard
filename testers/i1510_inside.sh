#!/bin/bash
# #1510, inside the container: the one-board fit measured on the evidence fixtures.
#
# Builds i1510_overlay_census against the whole calibration stack (i1499's build shape)
# and runs it over every camera of both rig fixtures plus the upstream Unicorn mocks as
# a CONTROL (never as evidence, #1478). The frame per fixture is deliberate:
#
#   rig-20260922  frame 270 -- the issue's own clean frame at 9 s. #1514 found this
#                 recording STARTS with a dart parked in the board, pulled about a
#                 second in, so frame 0 is not an empty board. The averaged input frame
#                 is dumped beside the overlay so "clean" is checked by eye, not assumed.
#   rig-20260918  frame 90  -- 3 s, where the dev build's own calibration seeks.
#   mocks         frame 90  -- same, control only.
#
# Measurement only: it asserts nothing and exits 0 unless a build or a run fails.
set -u

OUT=/run1510
mkdir -p "$OUT"

echo "=== building ==="
g++ -std=c++17 -O1 -I /app/src -I /app/src/utils -I /app/src/detector/geometry/calibration \
    -o "$OUT/census" /app/testers/i1510_overlay_census.cpp \
    /app/src/detector/geometry/calibration/*.cpp \
    /app/src/detector/geometry/detection/score_processing.cpp \
    /app/src/detector/geometry/detection/dart_processing.cpp \
    /app/src/detector/geometry/detection/motion_processing.cpp \
    $(pkg-config --cflags --libs opencv4) > "$OUT/census.build.log" 2>&1
if [ $? -ne 0 ]; then
    tail -30 "$OUT/census.build.log"
    echo "FAIL the overlay census did not build"
    exit 2
fi

run_fixture() { # $1 fixture dir under mocks ('' for the upstream mocks), $2 frame
    local dir="$1" frame="$2"
    local tag="${dir:-mocks}"
    local base="/app/mocks${dir:+/$dir}"
    mkdir -p "$OUT/dbg_$tag"
    cd "$OUT/dbg_$tag" || exit 2
    for c in 1 2 3; do
        echo "--- $tag cam_$c frame $frame ---"
        "$OUT/census" "$base/cam_$c.mp4" $((c - 1)) "$frame" \
            "$OUT/${tag}_cam${c}_overlay.jpg" "$OUT/${tag}_cam${c}_input.jpg" \
            2> "$OUT/${tag}_cam${c}.log" | grep '^I1510'
        rc=$?
        if [ $rc -ne 0 ]; then
            tail -5 "$OUT/${tag}_cam${c}.log"
            echo "FAIL census rc=$rc on $tag cam_$c"
            exit 2
        fi
    done
    cd / || exit 2
}

run_fixture rig-20260922 270
run_fixture rig-20260918 90
run_fixture "" 90
echo "I1510 DONE overlays and inputs under $OUT"
exit 0
