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