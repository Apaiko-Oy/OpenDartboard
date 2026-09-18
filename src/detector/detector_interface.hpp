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

// #899: what a detector says when it is asked to look again at a board it has already
// calibrated, and say whether it is the same board in the same place.
//
// It is deliberately not "recalibrate". The detector is not asked to replace the geometry
// it scores with -- see geometry_agreement.hpp for why replacing it would be the same
// silent failure in a new costume -- it is asked for a verdict on the geometry it holds.
struct GeometryReview
{
    enum class Verdict
    {
        Unchanged,  // the cameras that can be compared agree with the calibration held
        Moved,      // at least one of them does not, so something was touched
        Unreadable, // nothing could be compared: no camera that was scoring is back yet
    };

    Verdict verdict = Verdict::Unreadable;

    // One sentence naming the camera and the numbers it was decided on, in the shape
    // #1321 established for a refusal: every figure beside the threshold it failed.
    string account = "no detector was asked";
};

// Abstract interface for any dart detection method
class DetectorInterface
{
public:
    virtual ~DetectorInterface() = default;

    /**
     * #899: look again, and say whether the board is where it was.
     *
     * The default refuses rather than agrees, and that direction is the whole point: a
     * detector that has not been taught to compare its geometry cannot be allowed to
     * wave a board back into scoring by saying nothing. `Unreadable` is what a caller
     * treats as "do not score", so a detector that inherits this default is a detector
     * whose board stops at a camera failure exactly as it did before #899.
     */
    virtual GeometryReview reviewGeometry(const vector<camera::Frame> & /*frames*/)
    {
        return GeometryReview{GeometryReview::Verdict::Unreadable,
                              "this detector cannot say whether the board moved"};
    }

    // Initialize the detector. A detector is given the frames it calibrates on and the
    // rate they arrive at; it is not given the device they came from.
    virtual bool initialize(const vector<camera::Frame> &calibration_frames, double capture_fps) = 0;

    // Whether the detector is ready
    virtual bool isInitialized() const = 0;

    // Process frames and return detection results
    virtual DetectorResult process(const vector<camera::Frame> &frames) = 0;
};