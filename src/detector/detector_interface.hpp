#pragma once
#include <functional>
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
// Optional supplementary interface. Keeps the existing detector plugin vtable intact.
// Identifiers are provenance only; capture and device ownership stay in Scorer.
class CalibrationProvenance
{
public:
    virtual ~CalibrationProvenance() = default;
    virtual void offerCameraIdentities(const vector<string> &identities) = 0;
};

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

    /**
     * #1388: empty while this detector is scoring with the geometry it was given, or one
     * sentence naming what changed under it.
     *
     * ADR-0080 makes a `Moved` verdict survivable, so a board now goes back to scoring
     * after a disagreement instead of exiting. The half of #899 that must survive that
     * unchanged is the other one: nothing adopts fresh geometry mid-run, in any path.
     * Before #1388 that was true because the only way back into scoring was a `Moved`
     * that killed the process; afterwards it is true because nothing writes the held
     * calibrations after `initialize`, which is a property of the code rather than a
     * fact anything reads.
     *
     * So the detector is asked, every cycle, and the caller stops the board on an answer.
     * The default is empty rather than a refusal -- the opposite direction from
     * `reviewGeometry` above, and for the opposite reason: `reviewGeometry`'s default
     * governs whether a board may START scoring again, where silence must not be taken
     * for consent; this one governs whether a board that IS scoring must stop, where a
     * detector that holds no geometry of its own has nothing to have adopted.
     */
    virtual string geometryBreach() const { return ""; }

    /**
     * #1338: how many cameras this detector is actually scoring with, of how many it was
     * given, in one phrase -- "1 of 3 cameras (2 produced no frame)".
     *
     * The detector is the only thing that knows. Scorer used to announce the loop with
     * `camera_sources.size()`, the number of sources it was asked to OPEN, which on the
     * rig this issue was measured on printed `Scorer running with 3 cameras` four lines
     * below `Initial calibration completed successfully on 1 of 3 cameras`. Two numbers,
     * two meanings, no way for a reader to tell which one the board was scoring on.
     *
     * Empty means the detector has not been asked to calibrate, or cannot say; the caller
     * falls back to counting sources and says so.
     */
    virtual string scoringWith() const { return ""; }

    // Initialize the detector. A detector is given the frames it calibrates on and the
    // rate they arrive at; it is not given the device they came from.
    virtual bool initialize(const vector<camera::Frame> &calibration_frames, double capture_fps) = 0;

    /**
     * #1445: a way to be handed ANOTHER look, for a camera this detector could not
     * calibrate on the first one.
     *
     * The line above is the contract and this does not weaken it. A detector is still
     * not given the device: it is given a function that answers with frames, of exactly
     * the type `initialize` is already handed, and it can do nothing with it but ask for
     * another picture. It cannot open, close, re-configure or even name a camera.
     *
     * WHY ANY OF THIS IS NEEDED. Calibration happens once, on one averaged frame per
     * camera, and a camera refused on that frame abstains for the life of the run
     * (#1318, and the cost is written out in geometry_calibration.cpp). #1442 made the
     * wire count two-sided and the cost landed at once: `mocks/rig-20260918/cam_3`
     * proposes twenty-one on its averaged frame and exactly twenty on many of the single
     * frames composing that average. One bad picture is not an evening, and the only
     * thing missing was a second picture.
     *
     * WHAT IT IS NOT. It is not a recalibration, and the distinction is ADR-0080 §2's.
     * Whatever a detector does with this, it does BEFORE it has sealed a geometry -- so
     * there is nothing for it to depart from and nothing for it to adopt. A board that
     * is scoring is asked with `reviewGeometry`, which is a different question with a
     * different answer and does not touch `calibrations`.
     *
     * The default is to ignore the offer: a detector that calibrates on one frame or on
     * none is complete without it, and `initialize` is still the only thing that decides.
     */
    virtual void offerFurtherLooks(function<vector<camera::Frame>()> /*look*/) {}


    // Whether the detector is ready
    virtual bool isInitialized() const = 0;

    // Process frames and return detection results
    virtual DetectorResult process(const vector<camera::Frame> &frames) = 0;
};