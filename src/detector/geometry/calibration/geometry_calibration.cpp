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
    // #1378, and both are stated here rather than inline so that a test can move them and
    // watch the answer move. `kRegionRimPx` is three pixels because the colour stage's own
    // 7x7 blur and dilation already spread a boundary by about that much, so a rim thinner
    // than it would be measuring the blur rather than the board.
    static constexpr int kRegionRimPx = 3;
    static constexpr double kRegionRimColourShare = 0.01;

    // This function orchestrates the entire calibration pipeline for one camera
    // WHAT A REPEAT LOOK SAYS, AND WHERE (#1457).
    //
    // THE RULE IS: a further look (#1445) does not RE-say what has already been said. It
    // is not "a repeat look is silent", and the difference is the one decision in this
    // change that could have gone the other way.
    //
    // Its REFUSAL has been said. The pass that refused this camera said it once, aloud,
    // naming the camera, the stage and the count against the threshold (#1321) -- and the
    // picture is the only thing that changed between that call and this one. Its
    // NARRATION has been said too: `LOOK AGAIN` announced, before the first look, which
    // cameras are being looked at again and how many times, so "Calibrating camera 1"
    // twelve more times is the same mistake one level quieter. Both go to DEBUG, word for
    // word, through the macros below.
    //
    // A CAUTION HAS NOT BEEN SAID, and that is why `log_warning` is not routed through
    // them. #1378's rim check is a measurement of THIS frame, and what it warns about --
    // a doubles ring fitted out of what survived the cut, every dart on this camera
    // measured against a board smaller than the board -- is a property of the geometry
    // this board will SEAL if this look is the one that calibrates. Whether it is cannot
    // be known here: the warning is emitted three stages above the verdict. Quieting it
    // would be the one thing worse than noise, which is deleting the only evidence a live
    // condition ever had; a camera rescued on look 4 would seal a cut region and say so
    // nowhere. So a camera refused on every look still leaves up to twelve of these, and
    // that residue is named rather than hidden: the honest repair for it is for the retry
    // to say which look it ADOPTED and what that look measured, which is the question
    // #1456 was filed to decide and is not this change's to answer.
    //
    // A look that finds a board is still announced out loud by its caller, and so is a
    // budget that runs out; both are news, and neither is said here.
    //
    // ONE REFUSAL, SAID AT THE LEVEL THE CALLER SAYS IT IS WORTH. It reads the enclosing
    // function's own `voice` parameter, which is `RefusalIs::News` for every caller but
    // #1445's further looks -- the header holds the whole argument -- and the sentence is
    // identical either way. It is #undef'd at the end of that function, so there is
    // nowhere else it can be read from and no second `voice` it could pick up.
    //
    // WHY A MACRO AND NOT A FUNCTION. Every log line's module, the [GEOMETRY_CALIBRATION]
    // that testers/phases1318, phases1321, phases1392 and phases1317 all grep for, is
    // parsed out of __PRETTY_FUNCTION__ where the logging macro expands (utils/logging.hpp
    // -- LOG_ERROR passes OD_FUNCTION_SIGNATURE). A refusal announced from inside a helper
    // function or a lambda would therefore file itself under that helper's name and every
    // one of those assertions would stop matching, silently, on a line that still reads
    // right in a terminal. Expanded here it is calibrateSingleCamera's own module, which
    // is what it has always been.
#define log_refusal(message)                  \
    do                                        \
    {                                         \
        if (voice == RefusalIs::News)         \
        {                                     \
            log_error(message);               \
        }                                     \
        else                                  \
        {                                     \
            log_debug(message);               \
        }                                     \
    } while (0)

// The same rule for the lines that are not refusals: what this look measured about a
// camera it is repeating a look at. Same reason for a macro, same `voice`, same #undef.
#define log_narration(message)                \
    do                                        \
    {                                         \
        if (voice == RefusalIs::News)         \
        {                                     \
            log_info(message);                \
        }                                     \
        else                                  \
        {                                     \
            log_debug(message);               \
        }                                     \
    } while (0)

    DartboardCalibration calibrateSingleCamera(const Mat &frame, int cameraIdx, bool debugMode, RefusalIs voice)
    {
        // A further look is not a new attempt to be announced: the LOOK AGAIN line has
        // already said which cameras are being looked at again and how many times. ERROR
        // is where the cost was, but "Calibrating camera 1" twelve more times is the same
        // mistake one level quieter.
        log_narration("Calibrating camera " + log_string(cameraIdx + 1) +
                      (voice == RefusalIs::News ? "" : " again"));

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

        // [===STEP 1:===] FIND THE BOARD, on the whole picture, before anything is framed.
        //
        // ADR-0079. Until #1331 this stage was an ROI: a frame-centred ellipse of four
        // hand-fitted constants, drawn before anything had looked at the picture, with the
        // colour stage running inside it and the board finally MEASURED two stages below
        // that. So the one thing in the pipeline that can say where a board is was handed
        // a frame with the evidence already cut off, and a board at the region's edge was
        // measured as a clipped board -- its radius and its centre both wrong, and wrong
        // in the direction that makes it look more centred than it is.
        //
        // It is a smaller change than it sounds because the ROI has exactly one consumer:
        // `roiFrame` is read by the colour stage and by nothing else, and STEP 6's ellipse
        // fitting and STEP 8's wire extraction already read `orginalFrame`. Confirmed on
        // this tree before it was moved.
        //
        // So: colours on the full frame, the board measured from that with
        // `bull_processing::measureBoard` -- #1320's choice of region and #1340's measure
        // of it, the same function `processBull` calls, not a second opinion -- and only
        // then a region, drawn around what was found.
        color_processing::ColorParams colorParams;
        Mat fullFrameColours = color_processing::processColors(orginalFrame, cameraIdx, false, colorParams);

        bull_processing::BullParams bullParams;
        const bull_processing::BoardSighting board =
            bull_processing::measureBoard(fullFrameColours, frameCenter, bullParams);

        // The evidence a refusal below is argued from, taken here because these numbers
        // exist from this point on whether or not the camera gets any further.
        //
        // ADR-0079 §2 is the second of them and it is asked of EVERYTHING the colour stage
        // kept, not of the board region alone. The board region is the largest outermost
        // one, and when a board is cut by the frame the doubles ring breaks and the largest
        // survivor is the INNER part of the board, which touches no edge: mocks/cam_1.mp4
        // shifted 430 px right -- a third of the board off the picture -- measures as a
        // tidy board of radius 176 px sitting 56 px clear of the nearest edge and
        // calibrates, where the union of the kept colour is hard against x=1279. The union
        // is what "the whole board is visible" means, and it is a gap in pixels rather than
        // a share of anything, so there is no number for a rig to sit just outside of.
        //
        // Both rigs, measured on the full frame: mocks/cam_*.mp4 clear the frame edge by
        // 121, 148 and 119 px and mocks/rig-20260918 by 75, 39 and 55 px. The same mock
        // shifted right in 50 px steps reads 122 px at +150, 62 px at +230 and 0 px from
        // +300, which is where its board really does start leaving the picture.
        {
            Mat fullColourGray;
            cvtColor(fullFrameColours, fullColourGray, COLOR_BGR2GRAY);
            calibration.look.frame_pixels = static_cast<int>(fullFrameColours.total());
            calibration.look.frame_cols = fullFrameColours.cols;
            calibration.look.frame_rows = fullFrameColours.rows;
            calibration.look.red_green_pixels = countNonZero(fullColourGray);

            // #1392: what those pixels are a share OF, set once, here, because this is
            // where the only thing in the pipeline that measures a board has just run and
            // because every later site -- the bull branch, STEP 6.5 -- replaces the
            // numerator with a mask taken at the same pixel scale. A board that was not
            // found leaves it at zero: a camera with no board in its picture is refused
            // by the stage that could not find one, in that stage's own words, and has no
            // circle to be a share of.
            calibration.look.board_span_px = board.found ? board.radius : 0.0;

            const Rect kept = boundingRect(fullColourGray);
            calibration.look.board_edge_gap = min(min(kept.x, kept.y),
                                                  min(fullColourGray.cols - (kept.x + kept.width),
                                                      fullColourGray.rows - (kept.y + kept.height)));
            // Only where there is a board at all: an empty mask has an empty bounding box,
            // whose gap is zero, and a camera looking at a grey wall is not a camera whose
            // board is cut off -- it has no board, which the branch below says instead.
            calibration.look.board_clipped = board.found && calibration.look.board_edge_gap <= 0;
        }

        // What the full frame says, which is the only picture in this pipeline that has not
        // been cut down by anything. The edge gap is ADR-0079 §2's whole question and it
        // is printed whether or not it refuses, so a rig that is close to the edge can be
        // seen to be close to the edge before it falls off it.
        if (board.found)
        {
            log_debug("Camera " + log_string(cameraIdx + 1) + " board found on the FULL frame: radius " +
                      log_string((int)board.radius) + " px across its widest, centre (" +
                      log_string(board.center.x) + "," + log_string(board.center.y) +
                      "); its boundary encloses " + log_string((int)board.area) +
                      " px; the colour this camera kept comes within " +
                      log_string(calibration.look.board_edge_gap) + " px of the nearest frame edge");
        }

        // #1392: the two shares, side by side, on the FULL frame -- before a region is
        // drawn, before a bull is looked for and whichever branch this camera takes
        // below. STEP 6.5 prints the same line again about the mask it decides on. A
        // camera that moves closer moves the first number and must not move the second,
        // and that claim is unreadable from a log that prints only the deciding one.
        log_debug("Camera " + log_string(cameraIdx + 1) + " sight on the full frame: " +
                  log_string_src(board_look::measuredOnTheFullFrame(calibration.look)));

        if (!board.found)
        {
            // There is no board here to draw a region around, and going on would fail in
            // the same words two stages down with a region built around nothing. #1321's
            // shape: one ERROR, the camera, the stage, the number against the threshold.
            const board_look::Refused looked = board_look::verdict(calibration.look);
            const string look = (looked == board_look::Refused::None || looked == board_look::Refused::RingNotTraced)
                                    ? string("")
                                    : " This camera " + board_look::refusal(calibration.look) + ".";
            log_refusal("Camera " + log_string(cameraIdx + 1) +
                        " did not calibrate: there is no board in this frame to build a region around -- " +
                        board.failure + "." + log_string_src(look));
            board_sight::recordFault("camera " + to_string(cameraIdx + 1) +
                                     " did not calibrate: there is no board in this frame -- " + board.failure);
            return calibration;
        }

        // [===STEP 1.5:===] IS THE WHOLE BOARD IN SHOT? ADR-0079 §2, and the only thing
        // this pipeline now asks about framing. Not a percentage: if the full outermost
        // red/green region is inside the frame, calibration proceeds; if it is cut off by
        // the frame's OWN edge, that camera does not see a whole board and #1318's
        // refusal already knows what to do with it. A percentage was the alternative and
        // ADR-0079 refuses it for the same reason it refuses widening the old constants
        // -- any number we picked is one a rig can sit just outside of for no reason a
        // human can see.
        //
        // Nothing here tells anybody to move a camera, because nobody can: the cameras
        // are fixed to the frame and are not aimed at install (ADR-0079 §3). The one
        // adjustable degree of freedom is where the board sits in its circle while it is
        // being mounted, and it closes when the mounting does.
        //
        // Only the two questions the evidence so far can answer are asked here, and they
        // are named rather than taken from `verdict() != None`: at this point no ring has
        // been traced, because nothing has tried, so the whole verdict would read
        // `RingNotTraced` and refuse every camera in the building -- measured, on both
        // rigs, while writing this.
        const board_look::Refused framing = board_look::verdict(calibration.look);
        if (framing == board_look::Refused::FloodedFrame ||
            framing == board_look::Refused::TooMuchRedGreen || // OD_LOOK=frame's single test
            framing == board_look::Refused::BoardClipped)
        {
            const string why = board_look::refusal(calibration.look);
            log_refusal("Camera " + log_string(cameraIdx + 1) + " did not calibrate: it " +
                        log_string_src(why) + ".");
            board_sight::recordFault("camera " + to_string(cameraIdx + 1) + " did not calibrate: it " + why);
            return calibration;
        }

        // [===STEP 2:===] The region, drawn around the board that was found.
        roi_processing::ROIParams roiParams;
        Mat roiFrame = roi_processing::processROI(orginalFrame, board.center, board.radius, debugMode, cameraIdx, roiParams);

        // [===STEP 2.5:===] Detect red-green colors using the CLEAN color detection module
        //
        // Run again, inside the region, rather than the full-frame result above being
        // masked: this stage's component filtering is what drops the text, the edge blobs
        // and the room, and it decides those against the largest region it can see. On a
        // full frame that largest region competes with whatever else in the room is red.
        // The second pass costs one bilateral filter per camera, once, at calibration.
        Mat redGreenFrame = color_processing::processColors(roiFrame, cameraIdx, debugMode, colorParams);


        // [===STEP 2.6:===] #1378: DID THE REGION CUT COLOURED BOARD?
        //
        // This is the line whose absence cost eight darts. The region is drawn from a
        // COLOUR measurement -- `measureBoard`'s smallest circle around the largest
        // red/green contour -- and on a board whose doubles ring has dropped out of that
        // mask, that circle is the TREBLE ring (ROIParams). Under #1331's 1.25 margin
        // mocks/rig-20260918 therefore drew a 243 px region around a 316 px board, the
        // ray tracer four stages down fitted a doubles ring out of the arcs that survived
        // it, and the fit did not fail: it succeeded, smaller. The board every stage below
        // measures darts against went 197117/200385/194335 px to 72171/72374/72531 -- and
        // not one line of any log, at any level, said a region had clipped anything.
        //
        // Asking the FITTED ring whether it reached the region's edge does not work, and
        // it is worth writing down because it is the obvious check: the collapsed ring
        // sits at 199 px inside a 243 px region, comfortably clear of the boundary it was
        // cut by. What is hard against that boundary is the COLOUR. So the question is
        // asked here, of the rim of the region itself, one stage after it was drawn:
        //
        //   mocks/rig-20260918 at 1.25   13.5%, 10.2%, 8.7% of the region's rim is coloured
        //   mocks/rig-20260918 at 2.107   0.0%,  0.3%, 0.0%
        //   mocks/cam_*.mp4 at either     0.0%,  0.0%, 0.0%
        //
        // 1% sits two orders of magnitude from the broken readings and three times the
        // largest innocent one, which is a room's own red touching a rim that no longer
        // touches the board. The denominator is the rim this camera really has rather than
        // 2*pi*r: the region is intersected with the frame, so on a board near an edge
        // part of its rim does not exist, and a share of a rim that was never drawn would
        // read high for the one reason this check must not fire on.
        //
        // A WARNING and not an ERROR: the camera calibrated, and the board it hands down
        // is still a board -- a smaller one, measured against a ring somebody cut. That is
        // a thing to go and look at, not a reason to refuse a rig mid-evening. #1321's
        // rule on the sentence: both numbers, against each other, in one line.
        {
            const double regionRadius = roi_processing::regionRadiusFor(board.radius, roiParams);
            const int outer = cvRound(regionRadius);
            const int inner = outer - kRegionRimPx;
            if (inner > 0)
            {
                Mat rim = Mat::zeros(redGreenFrame.size(), CV_8UC1);
                circle(rim, board.center, outer, Scalar(255), FILLED);
                circle(rim, board.center, inner, Scalar(0), FILLED);
                const int rimPixels = countNonZero(rim);

                Mat keptGray, kept;
                cvtColor(redGreenFrame, keptGray, COLOR_BGR2GRAY);
                threshold(keptGray, kept, 1, 255, THRESH_BINARY);
                bitwise_and(kept, rim, kept);
                const int colouredRim = countNonZero(kept);

                const double share = rimPixels > 0 ? (double)colouredRim / (double)rimPixels : 0.0;
                if (share > kRegionRimColourShare)
                {
                    log_warning("Camera " + to_string(cameraIdx + 1) + " drew a region that CUT coloured "
                                "board: " + to_string(colouredRim) + " of the " + to_string(rimPixels) +
                                " px on the rim of its own " + to_string(outer) + " px region key as "
                                "dartboard red or green (" + to_string((int)lround(share * 100.0)) +
                                "%, and this check allows " +
                                to_string((int)lround(kRegionRimColourShare * 100.0)) + "%), so the doubles "
                                "ring fitted below will be fitted out of what survived the cut and every "
                                "dart on this camera will be measured against a board smaller than the "
                                "board. The region is " + to_string(roiParams.roiRadiusOfBoardRadius).substr(0, 5) +
                                "x a red/green span of " + to_string((int)lround(board.radius)) +
                                " px; see #1378");
                }
            }
        }

        // [===STEP 3:===] contour DETECTION using the new contour processing module
        // Note: This step is currently commented out as it is not used in the new pipeline
        // Uncomment if contour processing is needed in the future, for now leave it here for reference
        // contour_processing::ContourParams contourParams;
        // vector<vector<Point>> contours = contour_processing::processContours(redGreenFrame, orginalFrame, cameraIdx, debugMode, contourParams);

        // [===STEP 4:===] BULL dectection using the new bull processing module
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
            //
            // #1392: this goes into `ring_pixels` and NOT over the full frame's flood
            // count, which STEP 1 took and which nothing below it may overwrite. It is
            // the colour inside the region and it is not a ring -- nothing has traced one
            // -- so `ringWasTraced` is false and the ring gate is not asked of it. It is
            // set because OD_LOOK=frame's single test is asked of exactly this numerator
            // over exactly this denominator, which is what the code did here before.
            Mat redGreenGray;
            cvtColor(redGreenFrame, redGreenGray, COLOR_BGR2GRAY);
            calibration.look.ring_pixels = countNonZero(redGreenGray);
            calibration.look.traced_doubles = false;
            calibration.look.outer_points = 0;
            calibration.look.inner_points = 0;

            const board_look::Refused looked = board_look::verdict(calibration.look);
            const string look = (looked == board_look::Refused::None || looked == board_look::Refused::RingNotTraced)
                                    ? string("")
                                    : " This camera " + board_look::refusal(calibration.look) + ".";

            log_refusal("Camera " + log_string(cameraIdx + 1) +
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
        // The one INFO line below the bull stage, so it is the one line a camera refused at
        // the DOUBLES or WIRE stage would otherwise repeat once per look -- twelve nearly
        // identical bull positions for a camera that is being set aside. It is a real
        // measurement of a real frame rather than a repeated sentence, which is why it is
        // written and not dropped; it is just not news about a camera already refused.
        log_narration("Camera " + log_string(cameraIdx + 1) + " bull at (" + log_string(bullCenter.x) + "," +
                      log_string(bullCenter.y) + "), chosen on " + log_string_src(bull.basis));

        // [===STEP 5:===] Create binary mask for contour processing
        mask_processing::MaskParams maskParams;
        // #1393: the board this camera measured goes down with the bull centre. The bull
        // carve was a fifteenth of the FRAME and is now a fraction of this; nothing else
        // about this call moved.
        mask_processing::MaskBundle masks = mask_processing::processMask(redGreenFrame, bullCenter, bull.boardRadius, cameraIdx, debugMode, maskParams);

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
        //
        // #1392: the NUMERATOR is replaced here and the denominator is not. `frame_pixels`
        // is the region's frame, which is the full frame -- processROI blacks out what is
        // outside the region rather than cropping it, so a Mat's total() is the whole
        // picture whether or not anything is masked, and that was the old denominator.
        // `board_span_px` was set at STEP 1 from the board this camera really measured,
        // at this same pixel scale, and it is what the mask below is a share of.
        //
        // And the mask is not an annulus, which is why the line in Limits is measured
        // rather than derived from millimetres. `doublesMask` is preprocessMask's output:
        // the carved red/green mask closed, opened, dilated and reduced to its LARGEST
        // CONNECTED COMPONENT. On mocks/cam_*.mp4 that component is the doubles ring; on
        // mocks/rig-20260918 the doubles ring has dropped out of the colour mask
        // altogether and it is the TREBLE ring (#1378). Both are rings and both are a
        // small share of the circle they sit in, which is the property this gate is on.
        calibration.look.ring_pixels = masks.doublesMask.empty()
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
            log_refusal("Camera " + log_string(cameraIdx + 1) +
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
            log_refusal("Camera " + log_string(cameraIdx + 1) + " " + log_string_src(why) + ".");
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
        //
        // #1442 ADDS THE OTHER SIDE, AND IT IS THE SAME REFUSAL RATHER THAN A NEW ONE.
        // `isValid` now means a board's whole ring, so this branch is reached by a camera
        // proposing twenty-two as well as by one proposing nineteen, and the only thing
        // that changes here is that the sentence says WHICH -- because the remedy differs.
        // Too few is a board partly unread and is answered with light, aim and a clean
        // board; too many is the camera reading structure a board does not have, and is
        // answered by what else is in its picture. Neither is answered by the other's
        // advice, and a reader given one number and no direction tries both.
        //
        // What this costs is worth stating where it is paid: a refused camera abstains for
        // the LIFE of the run. Calibration happens once, on one averaged frame per camera
        // (capture::readAveraged(30) at start-up), and nothing retries it -- #895's vigil
        // sits faulted rather than looking again. So a healthy camera that proposes
        // twenty-two on its one frame is set aside until the board is restarted. That is
        // #1318's answer to a camera that cannot be trusted, unchanged: the camera is
        // named and set aside, the others carry on, and the board only faults when none is
        // left. It is the right trade here because the alternative is not "score slightly
        // worse" -- it is scoring a wedge map that is wrong by a whole wedge past the gap
        // the truncation leaves, and reporting it with the confidence of a clean twenty.
        if (!wireData.isValid)
        {
            const bool moreThanABoardHas = wireData.wiresDetected > wire_processing::kWiresRequired;
            const string count = to_string(wireData.wiresDetected);
            const string needed = to_string(wire_processing::kWiresRequired);
            const string why =
                moreThanABoardHas
                    ? "the wire stage found " + count + " wire boundaries where a board has " + needed +
                          ", so it is reading something that is not the board and no " + needed +
                          " of those " + count + " are the board's; this camera cannot be scored with."
                    : "the wire stage found " + count + " wire boundaries and all " + needed +
                          " are needed to tell one wedge from the next, so this camera cannot be scored with.";
            log_refusal("Camera " + log_string(cameraIdx + 1) + " did not calibrate: " + log_string_src(why));
            board_sight::recordFault("camera " + to_string(cameraIdx + 1) +
                                     " did not calibrate: the wire stage found " + count +
                                     (moreThanABoardHas
                                          ? " wire boundaries where a board has " + needed
                                          : " of the " + needed + " wire boundaries a board has"));
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

#undef log_refusal
#undef log_narration

    // #1445: which cameras are looking at the dartboard, said once, by whoever last
    // changed the answer. Lifted out of `calibrateMultipleCameras` unedited -- every word
    // below is #1318's, #1338's and #1389's -- because that function is now the FIRST of
    // two passes and a census printed after the first one is a number that moves.
    void sayWhichCamerasSeeTheBoard(const vector<DartboardCalibration> &calibrations)
    {
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

        // #1445: the census used to be said HERE, and it is said by `initialize` now.
        // It was printed by this function, which is the first pass; a camera refused on
        // this start's averaged frame may still be calibrated by a further look, and a
        // line reading `CAMERAS: 2 of 3 ... refused: 3` three lines above `Initial
        // calibration completed successfully on 3 of 3 cameras` is a census that stopped
        // being true while the reader was reading it. ADR-0081's rule is that the gate
        // that decides is the gate that says so, and after #1445 the thing that decides
        // how many cameras this board has is not this loop.
        //
        // `sayWhichCamerasSeeTheBoard` above is that same block, unmoved and unedited, so
        // the sentence three testers grep for is byte-for-byte what it was.

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