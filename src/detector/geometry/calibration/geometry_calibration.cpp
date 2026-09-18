#include <iostream>
#include "logging.hpp"
#include <cmath>
#include <algorithm>
#include <numeric>

#include "utils.hpp"
#include "board_sight.hpp"
#include "geometry_calibration.hpp"
#include "color_processing.hpp"
#include "roi_processing.hpp"
#include "mask_processing.hpp"
#include "contour_processing.hpp"
#include "bull_processing.hpp"
#include "ellipse_processing.hpp"
#include "wire_processing.hpp"
#include "orientation_processing.hpp"
#include "dartboard_visualization.hpp"
#include "perspective_processing.hpp"

using namespace cv;
using namespace std;

namespace geometry_calibration
{
    // This function orchestrates the entire calibration pipeline for one camera
    DartboardCalibration calibrateSingleCamera(const Mat &frame, int cameraIdx, bool debugMode)
    {
        log_info("Calibrating camera " + log_string(cameraIdx + 1));

        if (frame.empty())
        {
            log_error("Empty frame from camera " + log_string(cameraIdx + 1));
            DartboardCalibration blank; // Return empty calibration
            blank.camera_index = cameraIdx;
            return blank;
        }

        DartboardCalibration calibration;
        calibration.camera_index = cameraIdx;
        calibration.capture_width = frame.cols;
        calibration.capture_height = frame.rows;
        calibration.timestamp = static_cast<uint64_t>(time(nullptr)); // Current Unix timestamp

        Mat orginalFrame = frame.clone();
        Point frameCenter = math::calculateFrameCenter(frame);
        calibration.frameCenter = frameCenter;

        // [===STEP 1:===] Create ROI using the clean ROI processing module
        roi_processing::ROIParams roiParams;
        Mat roiFrame = roi_processing::processROI(orginalFrame, debugMode, cameraIdx, roiParams);

        // [===STEP 2:===] Detect red-green colors using the CLEAN color detection module
        color_processing::ColorParams colorParams;
        Mat redGreenFrame = color_processing::processColors(roiFrame, cameraIdx, debugMode, colorParams);

        // [===STEP 3:===] contour DETECTION using the new contour processing module
        // Note: This step is currently commented out as it is not used in the new pipeline
        // Uncomment if contour processing is needed in the future, for now leave it here for reference
        // contour_processing::ContourParams contourParams;
        // vector<vector<Point>> contours = contour_processing::processContours(redGreenFrame, orginalFrame, cameraIdx, debugMode, contourParams);

        // [===STEP 4:===] BULL dectection using the new bull processing module
        bull_processing::BullParams bullParams;
        bull_processing::BullSighting bull = bull_processing::processBull(redGreenFrame, frameCenter, cameraIdx, debugMode, bullParams);
        const Point bullCenter = bull.center;
        calibration.bullCenter = bullCenter;

        // #1320: a centre nothing can vouch for is not a centre to calibrate from. Every
        // stage below this one takes the bull as given -- the doubles mask is built
        // around it and the rays are traced out of it -- so a wrong centre does not fail
        // here. It fails 300 pixels away, as a boundary-point count that nearly worked.
        // The camera fails here instead, at the level and in the shape #1321 established.
        if (!bull.found)
        {
            // #1318 asks its question two stages below here, and a camera that is not
            // looking at a dartboard now fails before it gets there: a frame that is a
            // quarter dartboard red has no bull in it either. "No candidate can be a
            // bull" is that camera's symptom rather than its illness, so the look is
            // taken here too, from the red/green frame the doubles mask is derived
            // from, and the ERROR carries whichever sentence says more. A dark board
            // reads as RingNotTraced, which says nothing this line has not, so it is
            // left off -- #1321's rule that one refused camera is one ERROR.
            Mat redGreenGray;
            cvtColor(redGreenFrame, redGreenGray, COLOR_BGR2GRAY);
            calibration.look.frame_pixels = static_cast<int>(redGreenFrame.total());
            calibration.look.red_green_pixels = countNonZero(redGreenGray);
            calibration.look.traced_doubles = false;
            calibration.look.outer_points = 0;
            calibration.look.inner_points = 0;

            const board_look::Refused looked = board_look::verdict(calibration.look);
            const string look = (looked == board_look::Refused::None || looked == board_look::Refused::RingNotTraced)
                                    ? string("")
                                    : " This camera " + board_look::refusal(calibration.look) + ".";

            log_error("Camera " + log_string(cameraIdx + 1) +
                      " did not calibrate: the bull could not be found, so there is no centre to "
                      "build the doubles mask around or to trace the rays from -- " +
                      bull.failure + "." + look);
            board_sight::recordFault("camera " + to_string(cameraIdx + 1) +
                                     " did not calibrate: the bull could not be found -- " + bull.failure);
            return calibration; // ellipses.hasValidDoubles stays false, so the board fails
        }

        // What the winner was chosen on, where a reader sees it without --debug and
        // without opening a debug image. #1320's speck won on circularity alone, and the
        // only place that was ever written down was a JPEG nobody opens until afterwards.
        log_info("Camera " + log_string(cameraIdx + 1) + " bull at (" + log_string(bullCenter.x) + "," +
                 log_string(bullCenter.y) + "), chosen on " + log_string_src(bull.basis));

        // [===STEP 5:===] Create binary mask for contour processing
        mask_processing::MaskParams maskParams;
        mask_processing::MaskBundle masks = mask_processing::processMask(redGreenFrame, bullCenter, cameraIdx, debugMode, maskParams);

        // [===STEP 6:===] ELLIPSE DETECTION for dartboard shape
        ellipse_processing::EllipseParams ellipseParams;
        // #1330: the stage hands back its geometry and, if it failed, the words for it.
        // Only the geometry is kept on the calibration, because the calibration is what
        // is fwritten to the cache; the reason is read four statements below and printed.
        const ellipse_processing::EllipseReport ellipseReport = ellipse_processing::processEllipse(orginalFrame, masks, bullCenter, frameCenter, cameraIdx, debugMode, ellipseParams);
        const ellipse_processing::EllipseBoundaryData &ellipseData = ellipseReport.ellipses;
        calibration.ellipses = ellipseData;

        // [===STEP 6.5:===] #1318: IS THIS A DARTBOARD? Asked here, after the last step
        // that looks at the picture on its own terms and before the three that assume a
        // dartboard is in it. Steps 8, 8.5 and 9 fit wires, a perspective and an
        // orientation to whatever they are handed; run on a picture of a room they
        // produce numbers, not errors, and the numbers become a calibration the board
        // scores with. board_look.hpp holds the measurement and the argument.
        calibration.look.frame_pixels = masks.doublesMask.empty()
                                            ? 0
                                            : (int)masks.doublesMask.total();
        calibration.look.red_green_pixels = masks.doublesMask.empty()
                                                ? 0
                                                : countNonZero(masks.doublesMask);
        calibration.look.traced_doubles = ellipseData.hasValidDoubles;
        calibration.look.outer_points = ellipseData.validOuterPoints;
        calibration.look.inner_points = ellipseData.validInnerPoints;
        log_debug("Camera " + log_string(cameraIdx + 1) + " sight: " + log_string_src(board_look::measured(calibration.look)));

        const board_look::Refused refused = board_look::verdict(calibration.look);

        // #1321: the one place a failed calibration is reported, and the only one that
        // knows which camera this is. Everything below refuses on the same flag, so a
        // reader who is told three times learns nothing the first telling did not say;
        // those refusals are DEBUG now and this line carries the count they never did.
        //
        // #1318 extends the sentence rather than adding a second one. A camera that is
        // not pointed at a dartboard fails here too -- the ring it has no ring to fit --
        // and "9 of 120 rays gave a boundary point" is that camera's symptom, not its
        // illness. Where the look can say which it is, it is said in this ERROR, because
        // one refused camera is one ERROR and a webcam and an unlit board want different
        // things done about them.
        if (!ellipseData.hasValidDoubles)
        {
            const string reason = ellipseReport.doublesFailure.empty()
                                      ? string("the stage did not say why")
                                      : ellipseReport.doublesFailure;
            const string look = refused == board_look::Refused::RingNotTraced
                                    ? string("")
                                    : " This camera " + board_look::refusal(calibration.look) + ".";
            log_error("Camera " + log_string(cameraIdx + 1) +
                      " did not calibrate: the doubles ring could not be fitted, so wire "
                      "detection and perspective correction cannot run -- " +
                      reason + "." + log_string_src(look));
            board_sight::recordFault("camera " + to_string(cameraIdx + 1) +
                                     " did not calibrate: the doubles ring could not be fitted -- " + reason);
        }
        else if (refused != board_look::Refused::None)
        {
            // The ring WAS fitted and the camera still is not looking at a dartboard:
            // the one case #1321's line above cannot reach, and the one this issue was
            // filed about. Same shape, same rule -- the index, and the count said
            // against the threshold that refused it.
            const string why = board_look::refusal(calibration.look);
            log_error("Camera " + log_string(cameraIdx + 1) + " " + log_string_src(why) + ".");
            board_sight::recordFault("camera " + to_string(cameraIdx + 1) + " " + why);
        }

        if (refused != board_look::Refused::None)
        {
            // The camera is refused; the board is not. calibrateMultipleCameras keeps
            // its slot so no other camera is scored through its perspective, and the
            // cameras that did see the board carry on without it.
            calibration.sees_board = false;
            return calibration;
        }
        calibration.sees_board = true;

        // [===STEP 8:===] Extract actual wire positions for segment alignment
        wire_processing::WireDetectionConfig wireConfig;
        // When useHoughLinesDetection = false, it will use ensemble method

        wire_processing::WireData wireData = wire_processing::processWires(orginalFrame, redGreenFrame, calibration, debugMode, wireConfig);
        calibration.wires = wireData;

        // #1317: a partial detection does not calibrate. This is the second place a camera
        // is refused and it is deliberately the same shape as the first one above --
        // #1321's convention, one ERROR naming the camera, the stage and the count against
        // the threshold that refused it, plus the fault the vigil reads -- because a
        // reader should not have to know which stage failed to recognise the sentence.
        //
        // Before this, nine wires reported twenty, the three guards downstream were
        // tautologies, and the run said `PnP calibration successful` over eleven slots of
        // whatever was in memory. A board in that state scores darts against a geometry
        // it invented, which is worse than one that will not start.
        if (!wireData.isValid)
        {
            log_error("Camera " + log_string(cameraIdx + 1) +
                      " did not calibrate: the wire stage found " + log_string(wireData.wiresDetected) +
                      " wire boundaries and all " + log_string(wire_processing::kWiresRequired) +
                      " are needed to tell one wedge from the next, so this camera cannot be scored with.");
            board_sight::recordFault("camera " + to_string(cameraIdx + 1) +
                                     " did not calibrate: the wire stage found " + to_string(wireData.wiresDetected) +
                                     " of the " + to_string(wire_processing::kWiresRequired) +
                                     " wire boundaries a board has");
            calibration.sees_board = false;
            return calibration;
        }

        //[===STEP 8.5:===] PERSPECTIVE CORRECTION - Apply perspective correction to the mask
        perspective_processing::DartboardSpec perspectiveSpec;
        Mat rectifiedImage = perspective_processing::processPerspective(orginalFrame, calibration, debugMode, perspectiveSpec);

        // [===STEP 9:===] ORIENTATION DETECTION - Find where "20" segment is located
        // Use rectified image instead of original for better OCR
        orientation_processing::OrientationParams orientationParams;
        orientation_processing::OrientationData orientationData = orientation_processing::processOrientation(orginalFrame, redGreenFrame, calibration, debugMode, orientationParams);
        calibration.orientation = orientationData;

        return calibration;
    }

    // Multi-camera calibration orchestration
    vector<DartboardCalibration> calibrateMultipleCameras(const vector<Mat> &frames, bool debugMode, int targetWidth, int targetHeight)
    {
        vector<DartboardCalibration> calibrations;

        log_debug("DARTBOARD CALIBRATION STARTED");

        if (frames.empty())
        {
            log_error("No frames provided for calibration");
            return calibrations;
        }

        // Check if all frames have the same size and calibrate
        for (size_t cam_idx = 0; cam_idx < frames.size(); cam_idx++)
        {
            if (frames[cam_idx].empty())
            {
                // #1338: a camera that produced no frame is not a camera that was
                // looked at and refused. It is said here in the words of the remedy --
                // nothing was seen, so nothing about aim or lighting can be concluded --
                // and its slot carries `Refused::NoFrame` into the census below.
                log_warning("Camera " + log_string(cam_idx + 1) +
                            " produced no frame to calibrate on, so it was never looked at; "
                            "that is a cable, a hub or the bandwidth it shares, not its aim");
                // #1318: the slot is kept. score_processing reads calibrations[i] by the
                // camera's position in this vector, so a `continue` that shortened it
                // handed every camera after this one the calibration of its neighbour --
                // a board scored through the wrong perspective, with nothing said.
                DartboardCalibration blank;
                blank.camera_index = (int)cam_idx;
                calibrations.push_back(blank);
                continue;
            }

            //  Just call the static function
            DartboardCalibration calibration = calibrateSingleCamera(frames[cam_idx], cam_idx, debugMode);
            calibrations.push_back(calibration);

            if (debugMode)
            {
                // Create a debug visualization showing the calibration
                cv::Mat visFrame = dartboard_visualization::drawCalibrationOverlay(frames[cam_idx], calibration, true);
                odfs::ensureDirectory("debug_frames/geometry_calibration");
                imwrite("debug_frames/geometry_calibration/calibration_camera_" + to_string(cam_idx) + ".jpg", visFrame);
            }
        }

        // #1318: which cameras are looking at the dartboard, said once, as a sentence.
        // A board with two good cameras and a webcam in the middle of the list used to
        // report `BOARD FAULTED: this board is running and cannot see`, which was true of
        // nothing anybody could act on. Now the count is here and the reason is on the
        // refused camera's own line above.
        //
        // #1338: and the refused are split in two, because the two remedies are at
        // opposite ends of the room. A camera that ANSWERED and was refused is pointed
        // wrong or lit wrong, and the sentence on its own line above says which check it
        // failed. A camera that produced no frame was never looked at at all -- its slot
        // carries `board_look::Refused::NoFrame` by construction, because a blank
        // Evidence has `frame_pixels == 0` -- and the thing to go and look at is the
        // cable, the hub or the bandwidth it is sharing (#1319). Lumping the two under
        // one word `refused` sent the maintainer to check the aim of two cameras that
        // were not plugged in.
        {
            string seeing, blind, silent;
            int count = 0;
            for (const auto &calibration : calibrations)
            {
                const string index = to_string(calibration.camera_index + 1);
                if (calibration.sees_board)
                {
                    count++;
                    seeing += (seeing.empty() ? "" : ",") + index;
                }
                else if (board_look::verdict(calibration.look) == board_look::Refused::NoFrame)
                {
                    silent += (silent.empty() ? "" : ",") + index;
                }
                else
                {
                    blind += (blind.empty() ? "" : ",") + index;
                }
            }
            string line = "CAMERAS: " + to_string(count) + " of " + to_string((int)calibrations.size()) +
                          " are looking at the dartboard";
            if (count > 0)
                line += " (" + seeing + ")";
            if (!blind.empty())
                line += "; refused: " + blind;
            if (!silent.empty())
                line += "; produced no frame: " + silent;
            if (count == 0)
                log_error(line);
            else if (!blind.empty() || !silent.empty())
                log_warning(line);
            else
                log_info(line);
        }

        // once all cameras are calibrated, we need to find the star camera
        // and then use it to determine the perspective correction for the other 2 cameras

        if (debugMode && !calibrations.empty())
        {
            log_debug("Creating combined calibration visualization");
            Mat combinedViz = debug::createCombinedCalibrationVisualization(frames.size());

            if (!combinedViz.empty())
            {
                odfs::ensureDirectory("debug_frames");
                string filename = "debug_frames/calibration_summary_all_cameras.jpg";
                imwrite(filename, combinedViz);
                log_debug("Saved combined calibration visualization: " + filename);
            }
        }

        log_debug("DARTBOARD CALIBRATION COMPLETED");
        return calibrations;
    }

} // namespace geometry_calibration