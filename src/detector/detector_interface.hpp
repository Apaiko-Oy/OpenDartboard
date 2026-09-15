#pragma once
#include <string>
#include <vector>
#include <opencv2/opencv.hpp>
#include "../utils/frame.hpp"

using namespace cv;
using namespace std;

// Result structure with all detection data
struct DetectorResult
{
    bool dart_detected = false;
    string score = "";
    Point2f position{-1, -1}; // Dart position for overlay/preview
    float confidence = 0.0f;
    int camera_index = -1;
    uint64_t timestamp = 0;

    // #1186: where the dart is on the board, in the board's own frame, from the same
    // decision that produced `score`. `position` above is in the pixels of `camera_index`
    // and stays for the debug views. Absent - and published as null - on END, on a MISS,
    // and on anything that is not a dart.
    string ring;               // single, double, triple, bull, outer; empty when absent
    int segment = -1;          // 1..20; -1 when absent
    bool board_radius_known = false;
    bool board_angle_known = false;
    float board_radius = -1.0f; // 0 at the bull, 1 at the outer edge of the double ring
    float board_angle = -1.0f;  // degrees clockwise from the vertical through the 20, [0, 360)

    // Metadata for debugging/analysis
    bool motion_detected = false;
    int processing_time_ms = 0;

    // Easy boolean check
    operator bool() const { return dart_detected; }
};

// Abstract interface for any dart detection method
class DetectorInterface
{
public:
    virtual ~DetectorInterface() = default;

    // Initialize the detector. A detector is given the frames it calibrates on and the
    // rate they arrive at; it is not given the device they came from.
    virtual bool initialize(const vector<camera::Frame> &calibration_frames, double capture_fps) = 0;

    // Whether the detector is ready
    virtual bool isInitialized() const = 0;

    // Process frames and return detection results
    virtual DetectorResult process(const vector<camera::Frame> &frames) = 0;
};