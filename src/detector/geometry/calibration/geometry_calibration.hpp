#pragma once

#include <opencv2/opencv.hpp>
#include <vector>

#include "ellipse_processing.hpp"
#include "wire_processing.hpp"        // Include full definition for WireData
#include "orientation_processing.hpp" // Include orientation data
#include "board_look.hpp"             // #1318: whether this camera saw a dartboard at all

using namespace std;
using namespace cv;

struct DartboardCalibration
{
    // CORE: Basic calibration data
    Point bullCenter{0, 0};  // Bull center (true dartboard center)
    Point frameCenter{0, 0}; // Frame center
    int camera_index = -1;   // Which camera this is for
    int capture_width = -1;  // Width of the captured frame
    int capture_height = -1; // Height of the captured frame
    uint64_t timestamp = 0;  // Unix timestamp of when this calibration was captured

    // ELLIPSES: Dartboard shape and rings
    ellipse_processing::EllipseBoundaryData ellipses; // Contains all ellipse data

    // WIRE DATA: Contains wire positions for segment alignment
    wire_processing::WireData wires; // Contains wire positions and segment numbers

    // ORIENTATION: Dartboard rotation and "20" segment position
    orientation_processing::OrientationData orientation; // Where is the "20" segment?

    // #1318: SIGHT - was this camera looking at a dartboard? A slot is kept for every
    // camera whether or not it was, because score_processing reads calibrations[i] by
    // the camera's position in the vector and a shorter vector would silently hand one
    // camera another camera's board. `sees_board` is what the rest of the program asks;
    // `look` is the measurement it was decided on, kept so the refusal can be argued
    // with rather than only obeyed. Both are plain data: this struct is written to the
    // calibration cache with a raw fwrite.
    bool sees_board = false;
    board_look::Evidence look;
};

// NAMESPACE WITH UTILITY FUNCTIONS
namespace geometry_calibration
{
    // Calibrate one camera. Declared since #1318, because the probe that decides which
    // cameras a start with no --cams opens calls it on one candidate at a time -- the
    // real calibration rather than a cheaper imitation of it, so that a camera accepted
    // by the probe is one that has already calibrated.
    DartboardCalibration calibrateSingleCamera(const Mat &frame, int cameraIdx, bool debugMode);

    // Calibrate multiple cameras at once
    vector<DartboardCalibration> calibrateMultipleCameras(const vector<Mat> &frames, bool debugMode = false, int targetWidth = 640, int targetHeight = 480);
}