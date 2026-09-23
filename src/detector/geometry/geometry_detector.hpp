#pragma once

#include <opencv2/opencv.hpp>
#include <functional>
#include <vector>
#include "../detector_interface.hpp"
#include "calibration/geometry_calibration.hpp"
#include "calibration/geometry_agreement.hpp"
#include "calibration/board_model.hpp"
#include "detection/motion_processing.hpp"
#include "streamer.hpp"

using namespace cv;
using namespace std;

class GeometryDetector : public DetectorInterface, public CalibrationProvenance
{
public:
    GeometryDetector(bool debug_mode, int target_width, int target_height, int target_fps);
    virtual ~GeometryDetector() = default;

    virtual bool initialize(const vector<camera::Frame> &calibration_frames, double capture_fps) override;
    virtual bool isInitialized() const override { return initialized; }

    // Process method that handles motion + detection
    virtual DetectorResult process(const vector<camera::Frame> &frames) override;

    // #899: look again at the cameras that are back and say whether the calibration this
    // detector holds is still true of them. It does not replace `calibrations`, by
    // design -- geometry_agreement.hpp holds the argument.
    virtual GeometryReview reviewGeometry(const vector<camera::Frame> &frames) override;

    // #1338: the census this detector decided, for whoever has to print it.
    virtual string scoringWith() const override { return scoring_with; }

    // #1445: where another look comes from, for a camera the averaged frame refused.
    // Held, not called, until `initialize` has been through its first pass -- and never
    // called at all once the geometry is sealed. See `lookAgainAtRefusedCameras`.
    virtual void offerFurtherLooks(function<vector<camera::Frame>()> look) override
    {
        further_look = std::move(look);
    }

    // #1388: empty while `calibrations` is still what `initialize` sealed, or the two
    // lines that differ. See detector_interface.hpp for why anything asks.
    virtual string geometryBreach() const override;
    virtual void offerCameraIdentities(const vector<string> &identities) override
    { camera_identities = identities; }

protected:
    // #1363: apply OD_CAMERA_WEDGES -- the operator's stated orientation anchors -- to
    // the calibrations this start holds. Called on both the fresh and the cached path.
    void applyConfiguredAnchors();

    /**
     * #1445: look again at the cameras this start's averaged frame refused, and fill a
     * slot that is still empty.
     *
     * Called from `initialize`, on the measuring path only, between
     * `calibrateMultipleCameras` and the gate that counts what it produced -- which is to
     * say BEFORE `sealed_geometry` is taken. That position is the whole argument that
     * this is not the adoption ADR-0080 §2 refuses, and it is checked rather than
     * asserted: the function refuses to run at all once the seal exists.
     *
     * IT ONLY EVER FILLS AN EMPTY SLOT. A camera whose `sees_board` is true is not looked
     * at, is not re-read and cannot be overwritten, so the geometry this board ends up
     * sealing contains every measurement the first pass made, unchanged, plus measurements
     * for cameras that had none. A first calibration for a camera that never calibrated is
     * a different act from re-calibrating one that did, and this is where that sentence is
     * enforced rather than promised.
     */
    void lookAgainAtRefusedCameras();

    bool initialized;
    bool calibrated;
    bool debug_mode;
    int target_width;
    int target_height;
    int target_fps;
    vector<DartboardCalibration> calibrations;
    vector<board_model::Model> physical_models;
    vector<string> camera_identities;
    string sealed_physical_geometry;
    vector<Mat> background_frames;

    // #1338: what `initialize` concluded, in the words it concluded it in. Written once,
    // in `initialize`, read by Scorer when it announces the loop -- because that line
    // used to count the camera SOURCES it was given and print the answer as though it
    // were the number of cameras scoring, four lines under a line saying 1 of 3.
    string scoring_with;

    // #1388: `geometry_agreement::fingerprint(calibrations)` as it was at the end of
    // `initialize`, on whichever path initialize took -- fresh or cached. Written once
    // and never again, which is the whole of its value: it is a copy of the geometry
    // made at the one moment the board is allowed to decide what its geometry is, held
    // beside the geometry itself so the two can be asked whether they still agree.
    //
    // Empty means `initialize` has not finished. A detector that never sealed cannot be
    // in breach, because it has not been given anything to depart from.
    string sealed_geometry;

    // #1445: the offer from whoever owns the cameras, or nothing. A detector handed no
    // offer -- every unit test, and any caller that calibrates from a still -- looks once
    // and behaves exactly as it did before this issue.
    function<vector<camera::Frame>()> further_look;

    // #1339: each camera's board, in that camera's slot, as the motion stage wants it.
    // Derived from `calibrations` rather than stored beside it, so there is one answer
    // to where a board is and the motion stage cannot drift from the scoring stage.
    vector<motion_processing::BoardExtent> board_extents;

    // Debugging streamers for visual output
#ifdef DEBUG_VIA_VIDEO_INPUT
    unique_ptr<streamer> raw_streamer; // Raw camera feeds
#endif
};