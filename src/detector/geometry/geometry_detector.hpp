#pragma once

#include <opencv2/opencv.hpp>
#include <vector>
#include "../detector_interface.hpp"
#include "calibration/geometry_calibration.hpp"
#include "calibration/geometry_agreement.hpp"
#include "detection/motion_processing.hpp"
#include "streamer.hpp"

using namespace cv;
using namespace std;

class GeometryDetector : public DetectorInterface
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

protected:
    bool initialized;
    bool calibrated;
    bool debug_mode;
    int target_width;
    int target_height;
    int target_fps;
    vector<DartboardCalibration> calibrations;
    vector<Mat> background_frames;

    // #1338: what `initialize` concluded, in the words it concluded it in. Written once,
    // in `initialize`, read by Scorer when it announces the loop -- because that line
    // used to count the camera SOURCES it was given and print the answer as though it
    // were the number of cameras scoring, four lines under a line saying 1 of 3.
    string scoring_with;

    // Debugging streamers for visual output
#ifdef DEBUG_VIA_VIDEO_INPUT
    unique_ptr<streamer> raw_streamer; // Raw camera feeds
#endif
};