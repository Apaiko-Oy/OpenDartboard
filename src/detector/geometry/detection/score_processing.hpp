#pragma once

#include <opencv2/opencv.hpp>
#include "dart_processing.hpp"
#include "../calibration/geometry_calibration.hpp"

using namespace cv;
using namespace std;

namespace score_processing
{
    // #1186: where a dart is on the board, in the board's own frame rather than in one
    // camera's pixels. radius is 0 at the bull centre and 1 at the outer edge of the
    // double ring; angle is degrees clockwise from the vertical through the middle of
    // the 20, so the 20 spans [-9, 9), the 1 spans [9, 27) and so on round the board.
    // Both come from the rulers the calibration already holds for the camera the vote
    // chose: the six ring ellipses as a radial ruler with known marks in millimetres, and
    // the twenty wires as an angular ruler with marks 18 degrees apart. Neither is a
    // metric pose; a wire-to-wire fraction in image angle is a rectification, not a
    // reconstruction.
    struct BoardPosition
    {
        bool has_radius = false; // the tip is on the board and the radial ruler answered
        bool has_angle = false;  // the wedge the dart is in is known
        float radius = -1.0f;    // 0 .. 1 on a scoring dart; >1 is off the board
        float angle = -1.0f;     // [0, 360)
    };

    // #1186: one camera's reading of one tip, as a decision rather than as a string. The
    // score string is composed from ring and segment here, and nothing downstream parses
    // it back. wedge_measured is false where the orientation stage had no answer for this
    // camera and the wedge is the one the scorer asserts by default (upstream's "no
    // orientation data, defaulting to 20"); the board angle then says where in *that*
    // wedge the tip is, which is the position the score implies rather than one measured
    // against the number ring.
    struct PointScore
    {
        string score = "MISS";       // S20, D5, T17, BULL, OUTER, MISS - unchanged vocabulary
        string ring;                 // single, double, triple, bull, outer; empty for a miss
        int segment = -1;            // 1..20; -1 where the ring has no segment or the dart missed
        bool wedge_measured = false; // orientation known for this camera
        BoardPosition board;
    };

    // Score result for a single dart
    struct ScoreResult
    {
        string score = "MISS";                        // Dart score (S20, D5, T17, BULL, etc.)
        Point2f dartboard_position = Point2f(-1, -1); // Position on dartboard coordinate system
        Point2f pixel_position = Point2f(-1, -1);     // Original pixel position
        Point2f center_position = Point2f(-1, -1);    // Dart center position
        float confidence = 0.0f;                      // Scoring confidence
        int camera_index = -1;                        // Which camera detected this
        bool valid = false;                           // Is this a valid score result
        // #1186: the chosen camera's reading, in the board's frame. Absent on END and on
        // the MISS the vote publishes when no camera scored.
        string ring;
        int segment = -1;
        BoardPosition board;
    };

    // #1186: score one tip against one camera's calibration. The string the vote counts
    // is PointScore::score; the rest is the same decision stated as fields.
    PointScore scorePoint(Point2f pixel, const DartboardCalibration &calib);

    // Process dart scoring from tip detection results
    ScoreResult processScore(
        const vector<Mat> &background_frames,
        const dart_processing::DartStateResult &dart_result,
        const vector<DartboardCalibration> &calib,
        bool debug_mode = false);

} // namespace score_processing
