#include "color_processing.hpp"
#include "bull_processing.hpp"
#include "logging.hpp"
#include <cstdlib>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <string>

using namespace cv;
using namespace std;

namespace color_processing
{

    namespace
    {
        /**
         * #1394's falsification, in the shape OD_ROI, OD_ROI_MARGIN, OD_BOARD and
         * OD_BULL_CARVE established: one binary, the rule chosen at run time, so
         * "different build" is never a confound.
         *
         * OD_COLOUR_WINDOWS=frame puts back the four fractions of the FRAME WIDTH this
         * issue moved -- centralityThreshold, bullsEyeThreshold, maxDistanceFromCenter
         * and connectivityThreshold, exactly as #1323 left them -- on the same binary
         * that sizes them off the board. Anything but that exact word is ignored.
         */
        /**
         * #1394's second falsification, the same shape again: OD_BOARD_FLOOR=area puts
         * back `minBoardAreaPercent`, the 4%-of-the-frame floor this issue replaced, on
         * the same binary that runs the span floor.
         */
        bool boardFlooredOnArea()
        {
            static bool v = []
            {
                const char *e = std::getenv("OD_BOARD_FLOOR");
                return e && std::string(e) == "area";
            }();
            return v;
        }

        bool windowsDrawnOnTheFrame()
        {
            static bool v = []
            {
                const char *e = std::getenv("OD_COLOUR_WINDOWS");
                return e && std::string(e) == "frame";
            }();
            return v;
        }

        /** Two decimals, for a log line that has to carry a ratio. */
        string decimals(double v, int places)
        {
            ostringstream o;
            o << fixed << setprecision(places) << v;
            return o.str();
        }
    }

    Mat processColors(
        const Mat &roiFrame,
        int camera_idx,
        bool debug_mode,
        const ColorParams &params)
    {

        Mat redMask, greenMask;

        // ===== SECTION 1: PREPROCESSING =====
        Mat filteredFrame;
        bilateralFilter(roiFrame, filteredFrame, params.bilateralD, params.bilateralSigmaColor, params.bilateralSigmaSpace);

        Mat hsvFrame;
        cvtColor(filteredFrame, hsvFrame, COLOR_BGR2HSV);

        // ===== SECTION 2: COLOR DETECTION =====
        // This handles the color variation from bottom to top of dartboard
        Mat adaptiveRedMask = Mat::zeros(hsvFrame.size(), CV_8UC1);

        for (int y = 0; y < hsvFrame.rows; y++)
        {
            // Calculate vertical position factor (0 at bottom, 1 at top)
            float verticalFactor = static_cast<float>(hsvFrame.rows - y) / hsvFrame.rows;

            // Apply a non-linear transform to make upper areas more sensitive
            if (verticalFactor > 0.5)
            {
                verticalFactor = 0.5 + pow(verticalFactor - 0.5, 0.8) * 0.5;
            }

            // Calculate adaptive parameters based on vertical position
            int hueShift = cvRound(params.topRedHueShift * verticalFactor);
            int satShift = cvRound(params.topRedSatShift * verticalFactor);
            int valShift = cvRound(params.topRedValShift * verticalFactor);

            // Apply different thresholds for each row based on vertical position
            Mat rowMask1, rowMask2;

            // First red range with adaptive parameters
            inRange(hsvFrame.row(y),
                    Scalar(params.redLowHue1 + hueShift,
                           max(0, params.redLowSat1 - satShift),
                           max(0, params.redLowVal1 - valShift)),
                    Scalar(params.redHighHue1 + hueShift,
                           params.redHighSat1,
                           params.redHighVal1),
                    rowMask1);

            // Second red range with adaptive parameters
            inRange(hsvFrame.row(y),
                    Scalar(params.redLowHue2,
                           params.redLowSat2 - satShift,
                           params.redLowVal2 - valShift),
                    Scalar(params.redHighHue2,
                           params.redHighSat2,
                           params.redHighVal2),
                    rowMask2);

            // Combine results
            rowMask1 = rowMask1 | rowMask2;
            rowMask1.copyTo(adaptiveRedMask.row(y));
        }

        // ===== SECTION 3: STANDARD COLOR DETECTION =====
        // Standard detection as fallback
        Mat redMask1, redMask2;
        inRange(hsvFrame,
                Scalar(params.redLowHue1, params.redLowSat1, params.redLowVal1),
                Scalar(params.redHighHue1, params.redHighSat1, params.redHighVal1), redMask1);
        inRange(hsvFrame,
                Scalar(params.redLowHue2, params.redLowSat2, params.redLowVal2),
                Scalar(params.redHighHue2, params.redHighSat2, params.redHighVal2), redMask2);

        // Combine standard detection with adaptive detection
        redMask = redMask1 | redMask2 | adaptiveRedMask;

        // Detect green color
        inRange(hsvFrame,
                Scalar(params.greenLowHue, params.greenLowSat, params.greenLowVal),
                Scalar(params.greenHighHue, params.greenHighSat, params.greenHighVal), greenMask);

        // ===== SECTION 4: BASIC MORPHOLOGY =====
        // Apply small closing to connect nearby segments
        morphologyEx(redMask, redMask, MORPH_CLOSE, getStructuringElement(MORPH_ELLIPSE, Size(params.basicCloseKernelSize, params.basicCloseKernelSize)));
        morphologyEx(greenMask, greenMask, MORPH_CLOSE, getStructuringElement(MORPH_ELLIPSE, Size(params.basicCloseKernelSize, params.basicCloseKernelSize)));

        // ===== SECTION 5: BULL'S EYE ENHANCEMENT =====
        // After detecting red and green
        Mat redBeforeSection5 = redMask.clone(); // #1394: what this section ADDS, measured below
        Mat bullsEyeMask = redMask.clone();
        Rect centerRegion(
            roiFrame.cols / 2 - roiFrame.cols / params.bullsEyeRegionSize,
            roiFrame.rows / 2 - roiFrame.rows / params.bullsEyeRegionSize,
            roiFrame.cols / (params.bullsEyeRegionSize / 2),
            roiFrame.rows / (params.bullsEyeRegionSize / 2));
        Mat centerMask = Mat::zeros(bullsEyeMask.size(), CV_8UC1);
        rectangle(centerMask, centerRegion, Scalar(255), -1);
        bullsEyeMask = bullsEyeMask & centerMask;
        dilate(bullsEyeMask, bullsEyeMask, getStructuringElement(MORPH_ELLIPSE, Size(params.bullsEyeDilateKernel, params.bullsEyeDilateKernel)));
        redMask = redMask | bullsEyeMask;
        Mat section5Added;
        bitwise_and(redMask, ~redBeforeSection5, section5Added);

        // ===== SECTION 6: COMBINE COLORS & INITIAL CLEANING =====
        Mat redGreenMask = redMask | greenMask;

        // Basic cleaning of the mask
        Mat cleanMask;
        morphologyEx(redGreenMask, cleanMask, MORPH_OPEN, getStructuringElement(MORPH_ELLIPSE, Size(params.cleanOpenKernelSize, params.cleanOpenKernelSize)));
        morphologyEx(cleanMask, cleanMask, MORPH_CLOSE, getStructuringElement(MORPH_ELLIPSE, Size(params.cleanCloseKernelSize, params.cleanCloseKernelSize)));

        Mat enhancedMask = cleanMask.clone();

        // ===== SECTION 6: MULTI-SCALE MORPHOLOGY =====
        for (int size = 3; size <= params.maxMorphologyKernelSize; size += 2)
        {
            morphologyEx(enhancedMask, enhancedMask, MORPH_CLOSE,
                         getStructuringElement(MORPH_ELLIPSE, Size(size, size)));
        }

        // ===== SECTION 6.5: RING CONNECTION ENHANCEMENT =====
        Mat beforeSection65;
        {
            // Apply stronger closing to top half where rings are often broken
            beforeSection65 = enhancedMask.clone(); // #1394: what this repair reaches, measured below
            int topHeight = enhancedMask.rows / 2;
            Rect topRegion(0, 0, enhancedMask.cols, topHeight);
            Mat topMask = enhancedMask(topRegion).clone();

            // More aggressive closing for top half
            morphologyEx(topMask, topMask, MORPH_CLOSE,
                         getStructuringElement(MORPH_ELLIPSE, Size(params.topRegionCloseKernel, params.topRegionCloseKernel)));

            // Copy back to enhanced mask
            topMask.copyTo(enhancedMask(topRegion));
        }

        // ===== SECTION 7: FINAL TARGETED COMPONENT FILTERING =====
        // Go back to original approach but add SPECIFIC text blob removal
        Mat labels, stats, centroids;
        int nLabels = connectedComponentsWithStats(enhancedMask, labels, stats, centroids);

        // The region every rule below is sized off, by bull_processing::chooseBoardRegion:
        // the largest unless the room outgrew the board.
        int largestIdx = 0;
        int largestArea = 0;
        {
            vector<bull_processing::Region> regions;
            for (int i = 1; i < nLabels; i++)
            {
                bull_processing::Region r;
                r.area = stats.at<int>(i, CC_STAT_AREA);
                r.box = Rect(stats.at<int>(i, CC_STAT_LEFT), stats.at<int>(i, CC_STAT_TOP),
                             stats.at<int>(i, CC_STAT_WIDTH), stats.at<int>(i, CC_STAT_HEIGHT));
                r.span = 0.5 * max(r.box.width, r.box.height);
                regions.push_back(r);
            }
            const int chosen = bull_processing::chooseBoardRegion(regions, enhancedMask.size(),
                                                                  bull_processing::BullParams().minBoardRadius());
            if (chosen >= 0)
            {
                largestIdx = chosen + 1;
                largestArea = static_cast<int>(regions[chosen].area);
            }
        }

        Mat filteredMask = Mat::zeros(enhancedMask.size(), CV_8UC1);
        Point2f imageCenter(enhancedMask.cols / 2.0f, enhancedMask.rows / 2.0f);

        // ===== SECTION 7.1 (#1323): WHERE THE BOARD IS =====
        //
        // The rules below ask how far a component sits from "the middle", and until
        // #1323 that middle was the middle of the FRAME. On a camera aimed square at the
        // board those are the same place and the rules do what they look like they do:
        // a speck is dropped and the bull is kept. On a camera whose board sits low and
        // right the frame-width/10 bull's-eye window sits OFF the board -- the real bull
        // falls outside it and is dropped, and a speck that happens to fall inside it
        // survives. That is #1323. On the fixture it was measured on -- the mocks aimed
        // 180 px right and 90 px low -- the window is 128 px across and its middle is
        // 157 px from the bull it is supposed to be around, so the bull is dropped and
        // the camera fails with a board fully in shot.
        //
        // The board can be measured here, and it is measured the way #1320 measures it
        // one stage later rather than in some second way: the largest OUTERMOST contour
        // in the mask is the board, because its boundary is the outside of the doubles
        // ring and that is the last coloured thing on a board; the centroid of that
        // boundary polygon is the middle of the board the rings describe; and a board
        // has to enclose 4% of the frame before anything is measured against it, which
        // is #1320's own floor and its own sentence.
        //
        // The polygon centroid is what is used and the pixel centroid is not, and the
        // difference is not a detail. connectedComponentsWithStats has already computed
        // a centroid for every component, which is free and wrong: it is the middle of
        // the coloured PIXELS, so a board whose top rings are broken -- which is the
        // normal case, it is why SECTION 6.5 exists -- weighs low, and measured on the
        // two rigs in this repository it sits 148 px below the bull on one camera and
        // 174 px from it on another. The window it would centre is 128 px, so the free
        // number was tried and it drops the bull on three of the six cameras that
        // calibrate today: mocks camera 3 and rig-20260918 cameras 1 and 3 all stopped
        // finding one. The boundary polygon does not care which rings inside it are
        // missing: on the five cameras it can be measured on the bull is 21 to 68 px
        // from it, and all six keep the centre they had.
        //
        // Where there is no such region -- a frame with nothing on it, a lens cap, a
        // room -- there is no board to be off the middle of. The rule says so in the log
        // and falls back to the middle of the frame, which is the behaviour that shipped
        // before this issue.
        //
        // WHAT IS NOT MOVED, and #1394's measured reason for each. Three things in this
        // file are keyed to the frame and stay keyed to the frame: SECTION 2's red ramp
        // over frame rows, SECTION 5's bull's-eye rectangle at the frame's middle and
        // SECTION 6.5's ring repair over the frame's top half. All three run BEFORE this
        // block, so none of them can be moved onto the board without measuring the board
        // a second time, earlier, from a different mask -- and a second opinion about
        // where the board is, in the file whose whole subject is where the board is, is
        // the thing #1320, #1323 and #1340 each spent a slice not introducing. SECTION
        // 7.15 counts what each of them really does, per camera, so these are numbers
        // rather than positions.
        //
        // SECTION 5's rectangle. It only ever ADDS pixels to the red mask, which is the
        // reason #1323 gave, and #1394 measured whether the pixels it adds are in the
        // right place: 1133 to 1555 px of red per camera on both shipped fixtures, and
        // ZERO of it off the board on any of the six. On #1323's own off-aimed fixture --
        // the mocks shifted 180 right and 90 low, so the board's middle is 170 px from
        // the frame's -- it adds 1299 px on the board and 160 px off it, 11% of the add,
        // and that camera still finds its bull at (796,373) and traces 98 outer points.
        // Its dilation is also part of what #1320's bull-to-board ratios were measured
        // through, so moving it would move the ground those constants stand on.
        //
        // SECTION 6.5's ring repair. This is the one #1394 tried to move and REVERTED,
        // and the reason is the strongest measurement in this slice. The miss is real: on
        // the off-aimed camera 5183 px of the board's own upper half lies below the
        // frame's middle and is never reached, against 436 px on mocks camera 2. But
        // moving the repair onto the board needs the board measured before the repair,
        // and THE REPAIR IS WHAT MAKES THE BOARD MEASURABLE. With this block lifted above
        // SECTION 6.5 -- the only change needed, nothing else touches `enhancedMask` in
        // between -- mocks camera 1's largest outermost contour spans 171 px instead of
        // 289, because the 9x9 closing is what joins its board into one region at all.
        // On that degraded measurement the mocks fall to 2 of 3 cameras with 4 ERROR/WARN
        // lines, camera 3 loses its bull outright, camera 2's fitted board moves 173006 px
        // to 175972, and the off-aimed camera's bull moves from (796,373) to (792,370).
        // So the repair stays where it is and over the frame's top half, and the 5183 px
        // is the cost of that, written down rather than argued away.
        //
        // SECTION 2's red ramp. It reaches full strength at frame row 0 and #1394
        // measured how much of its range is actually spent across the board: 62.3%, 69.2%
        // and 67.6% on the mocks, 41.0% and 41.9% on mocks/rig-20260918, 68.7% on the
        // off-aimed camera. So on the rig -- the fixture whose board sits smaller in
        // frame, which is where every constant in this family runs out first -- three
        // fifths of the ramp is spent on the room. It is also the only one of the three
        // that cannot be moved even in principle without reordering the stage, because it
        // builds the red mask the board would have to be measured from. What limits the
        // damage is what the ramp DOES: it widens the red range, so its cost is extra
        // candidate blobs, and judging those is the job of the windows above, which are
        // now drawn on the board.
        const double frameArea = static_cast<double>(enhancedMask.cols) * enhancedMask.rows;

        vector<vector<Point>> boardContours;
        findContours(enhancedMask, boardContours, RETR_EXTERNAL, CHAIN_APPROX_SIMPLE);
        double boardArea = 0.0;
        int boardIndex = -1;
        {
            vector<bull_processing::Region> regions;
            for (size_t c = 0; c < boardContours.size(); c++)
            {
                bull_processing::Region r;
                r.area = contourArea(boardContours[c]);
                r.box = boundingRect(boardContours[c]);
                Point2f centre;
                float span = 0.0f;
                minEnclosingCircle(boardContours[c], centre, span);
                r.span = span;
                regions.push_back(r);
            }
            boardIndex = bull_processing::chooseBoardRegion(regions, enhancedMask.size(),
                                                            bull_processing::BullParams().minBoardRadius());
            if (boardIndex >= 0)
                boardArea = regions[boardIndex].area;
        }

        Point2f boardCenter = imageCenter;
        bool boardMeasured = false;
        float boardSpan = 0.0f;

        // #1394: the SPAN of the boundary, measured before it is judged, the way
        // `bull_processing::measureBoard` measures it one stage later -- the smallest
        // circle around that contour -- and not in a second way. It is the only length
        // this stage has: the windows below are fractions of it, and so is the floor that
        // decides whether there is a board here at all.
        const double minSpan = bull_processing::BullParams().minBoardRadius();
        if (boardIndex >= 0)
        {
            Point2f spanCentre;
            minEnclosingCircle(boardContours[boardIndex], spanCentre, boardSpan);
        }

        bool bigEnough = false;
        if (boardIndex >= 0)
        {
            if (boardFlooredOnArea())
            {
                bigEnough = (boardArea >= frameArea * params.minBoardAreaPercent);
            }
            else
            {
                bigEnough = (boardSpan >= minSpan);
            }
        }

        if (bigEnough)
        {
            const Moments boardMoments = moments(boardContours[boardIndex]);
            if (boardMoments.m00 > 0)
            {
                boardCenter = Point2f(static_cast<float>(boardMoments.m10 / boardMoments.m00),
                                      static_cast<float>(boardMoments.m01 / boardMoments.m00));
                boardMeasured = true;
            }
        }

        {
            ostringstream share, least;
            share << fixed << setprecision(2) << (frameArea > 0 ? (100.0 * boardArea / frameArea) : 0.0);
            least << fixed << setprecision(2) << (params.minBoardAreaPercent * 100.0);
            // #1394: the floor that really decided, with its own two numbers. The area
            // sentence is kept beside it under both rules, because it is the sentence
            // #1323's tester reads and because a reader comparing the two floors on one
            // camera should not have to run the binary twice to see both.
            string where = " the largest coloured region spans " + to_string((int)lround(boardSpan)) +
                           " px across its widest, and a board this stage can size a window against spans at least " +
                           to_string((int)lround(minSpan)) + " px (it encloses " +
                           to_string(static_cast<long>(boardArea)) + " px, " + share.str() + "% of the frame)";
            if (boardFlooredOnArea())
            {
                where = " OD_BOARD_FLOOR=area, so the largest coloured region encloses " +
                        to_string(static_cast<long>(boardArea)) + " px, " + share.str() +
                        "% of the frame, and a board encloses at least " + least.str() + "% (it spans " +
                        to_string((int)lround(boardSpan)) + " px)";
            }
            if (boardMeasured)
            {
                log_debug("Camera " + log_string(camera_idx + 1) + " board measured from the coloured mask:" +
                          log_string_src(where) + ". Its middle is (" + log_string((int)boardCenter.x) + "," +
                          log_string((int)boardCenter.y) + "), and a small blob is kept or dropped on its distance from "
                                                          "THAT rather than from the middle of the frame at (" +
                          log_string((int)imageCenter.x) + "," + log_string((int)imageCenter.y) + ")");
            }
            else
            {
                log_debug("Camera " + log_string(camera_idx + 1) + " has no board to measure here:" +
                          log_string_src(where) + ". There is nothing in this frame to be off the middle of, so the "
                                                  "centrality rules fall back to the middle of the FRAME at (" +
                          log_string((int)imageCenter.x) + "," + log_string((int)imageCenter.y) +
                          ") -- which is the middle of a board only on a camera aimed square at one");
            }
        }

        // ===== SECTION 7.15 (#1394): THE THREE SITES THAT ARE NOT WINDOWS =====
        //
        // SECTION 2's red ramp, SECTION 5's bull's-eye rectangle and SECTION 6.5's ring
        // repair are all keyed to the FRAME -- its rows, its middle, its top half -- and
        // all three run before anything here has measured anything, so none of them can be
        // MOVED onto the board without a second board measurement earlier in this
        // function. What they can be is counted, here, where the board is known: the issue
        // asks for each of them to be moved or given a measured reason, and a reason is a
        // number. Nothing below changes a pixel.
        if (boardMeasured && boardSpan > 0.0f)
        {
            Mat onBoard = Mat::zeros(enhancedMask.size(), CV_8UC1);
            circle(onBoard, Point(cvRound(boardCenter.x), cvRound(boardCenter.y)), cvRound(boardSpan),
                   Scalar(255), FILLED);

            Mat addedOn, addedOff;
            bitwise_and(section5Added, onBoard, addedOn);
            bitwise_and(section5Added, ~onBoard, addedOff);

            // SECTION 6.5 rewrites the top half of the FRAME. Two numbers say what that
            // costs on this camera: how much of what it changed was not board at all, and
            // how much of the BOARD's own top half sits below the frame's middle and is
            // therefore never reached.
            int repairedOffBoard = 0, boardTopUnrepaired = 0;
            if (!beforeSection65.empty())
            {
                Mat changed;
                bitwise_xor(enhancedMask, beforeSection65, changed);
                Mat off;
                bitwise_and(changed, ~onBoard, off);
                repairedOffBoard = countNonZero(off);

                Mat boardTop = Mat::zeros(enhancedMask.size(), CV_8UC1);
                const int top = max(0, cvRound(boardCenter.y - boardSpan));
                const int mid = cvRound(boardCenter.y);
                const int half = enhancedMask.rows / 2;
                if (mid > half)
                {
                    Rect unreached(0, max(half, top), enhancedMask.cols,
                                   min(mid, enhancedMask.rows) - max(half, top));
                    if (unreached.height > 0)
                    {
                        rectangle(boardTop, unreached, Scalar(255), FILLED);
                        Mat missed;
                        bitwise_and(boardTop, onBoard, missed);
                        bitwise_and(missed, beforeSection65, missed);
                        boardTopUnrepaired = countNonZero(missed);
                    }
                }
            }

            // SECTION 2's ramp runs over the frame's rows and reaches full strength at
            // frame row 0. What matters is how much of its range is spent ACROSS the
            // board, which is the only place the lighting gradient it compensates for is
            // a gradient over a dartboard.
            auto ramp = [&](int y)
            {
                float f = static_cast<float>(enhancedMask.rows - y) / enhancedMask.rows;
                if (f > 0.5f)
                {
                    f = 0.5f + pow(f - 0.5f, 0.8f) * 0.5f;
                }
                return f;
            };
            const int boardTopRow = max(0, cvRound(boardCenter.y - boardSpan));
            const int boardBottomRow = min(enhancedMask.rows - 1, cvRound(boardCenter.y + boardSpan));
            const double rampTop = ramp(boardTopRow), rampBottom = ramp(boardBottomRow);

            log_debug("Camera " + log_string(camera_idx + 1) + " frame-keyed stages, measured against the board: " +
                      log_string_src(string("SECTION 5's rectangle adds ")) + log_string(countNonZero(addedOn)) +
                      " px of red on the board and " + log_string(countNonZero(addedOff)) +
                      " px off it; SECTION 6.5's repair changed " + log_string(repairedOffBoard) +
                      " px that are not board and left " + log_string(boardTopUnrepaired) +
                      " px of the board's own upper half below the frame's middle unreached; SECTION 2's ramp runs " +
                      log_string_src(decimals(rampBottom, 3)) + " to " + log_string_src(decimals(rampTop, 3)) +
                      " across board rows " + log_string(boardBottomRow) + " to " + log_string(boardTopRow) +
                      ", which is " + log_string_src(decimals(100.0 * (rampTop - rampBottom), 1)) +
                      "% of the range it spends on the whole frame");
        }

        // ===== SECTION 7.2 (#1394): HOW BIG THE WINDOWS ARE =====
        //
        // #1323 moved the four distances below onto the board and deliberately left their
        // SIZE a fraction of the frame's width. A fraction of the frame answers a question
        // about the lens; every one of these four asks a question about the BOARD -- is
        // this blob the bull, is it a ring, is it a number or the room -- which is a
        // distance in board radii. `ColorParams` holds the derivations.
        //
        // The length is `boardSpan`: the smallest circle around the same boundary
        // `boardCenter` is the centroid of. It is the span and not the board, and which
        // ring it lands on is known rather than assumed -- `roi_processing::ROIParams`
        // measured it on both fixtures and it is the doubles ring on one and the treble
        // ring on the other. THREE of the four windows move; the outer cutoff is left on
        // the frame because that ambiguity breaks it in both directions at once, and
        // `ColorParams::maxDistanceFromCenter` carries the measurement that says so.
        //
        // Where no board could be measured, the windows are the frame's, exactly as they
        // were before this issue -- the same fallback, and the same sentence in the log,
        // that #1323 established for the centre they are drawn around.
        double bullsEyeWindow = 0.0, centralityWindow = 0.0, farWindow = 0.0, connectivityWindow = 0.0;
        bool windowsOnBoard = boardMeasured && boardSpan > 0.0f && !windowsDrawnOnTheFrame();
        // The outer cutoff is on the frame under BOTH rules and always has been: it is the
        // one window of the four whose board-radii stop cannot be converted into this
        // stage's only length without breaking one fixture or the other, and
        // `ColorParams::maxDistanceFromCenter` carries both sets of numbers.
        farWindow = enhancedMask.cols * params.maxDistanceFromCenter / 2;
        if (windowsOnBoard)
        {
            const double boardRadius = boardSpan * params.boardRadiusOfBoardSpan;
            bullsEyeWindow = boardSpan * params.bullsEyeOfBoardSpan;
            centralityWindow = boardRadius * params.centralityOfBoardRadius;
            connectivityWindow = boardRadius * params.connectivityOfBoardRadius;
        }
        else
        {
            bullsEyeWindow = enhancedMask.cols * params.bullsEyeThreshold;
            centralityWindow = enhancedMask.cols * params.centralityThreshold;
            connectivityWindow = enhancedMask.cols * params.connectivityThreshold;
        }

        {
            // Both rules on the line whichever chose, because until #1394 nothing anywhere
            // printed either, and the four numbers a camera really used are the only way to
            // read the census below. No ternary reaches log_string_src: `+` binds tighter
            // than `?:`, so a ternary handed to that macro is pointer arithmetic on a string
            // literal and always takes its first branch (#1393 shipped one and caught it).
            string rule = "the FRAME's width";
            if (windowsOnBoard)
            {
                rule = "a board spanning " + to_string((int)lround(boardSpan)) + " px";
            }
            log_debug("Camera " + log_string(camera_idx + 1) + " colour windows off " + log_string_src(rule) +
                      ": bull's-eye " + log_string((int)lround(bullsEyeWindow)) + " px, centrality " +
                      log_string((int)lround(centralityWindow)) + " px, outer cutoff " +
                      log_string((int)lround(farWindow)) + " px, connectivity " +
                      log_string((int)lround(connectivityWindow)) +
                      " px; under the frame rule they are " +
                      log_string((int)lround(enhancedMask.cols * params.bullsEyeThreshold)) + ", " +
                      log_string((int)lround(enhancedMask.cols * params.centralityThreshold)) + ", " +
                      log_string((int)lround(enhancedMask.cols * params.maxDistanceFromCenter / 2)) + " and " +
                      log_string((int)lround(enhancedMask.cols * params.connectivityThreshold)) +
                      " px on every camera, every rig and every mounting");
        }

        {
            // #1323's own measurement, reproduced here rather than quoted, because #1394
            // shrinks the bull's-eye window on the rig and what decides whether that is
            // safe is how far the point these windows are drawn around sits from the bull.
            //
            // The free number is `centroids`, which connectedComponentsWithStats has
            // already computed for every component: the middle of the coloured PIXELS. A
            // board whose top rings are broken -- the normal case, it is why SECTION 6.5
            // exists -- weighs low, so that centroid sinks. #1323 measured it at 148 px
            // below the bull on one camera and 174 px from it on another and refused it
            // for a window of 128 px. `boardCenter` is the centroid of the boundary
            // POLYGON, which does not care which rings inside it are missing.
            //
            // Both are printed with the gap between them, so the next reader of these
            // constants can see on their own footage what #1394 had to take on trust from
            // a comment.
            if (boardMeasured && largestIdx > 0)
            {
                const Point2f pixels(static_cast<float>(centroids.at<double>(largestIdx, 0)),
                                     static_cast<float>(centroids.at<double>(largestIdx, 1)));
                log_debug("Camera " + log_string(camera_idx + 1) +
                          " board middle: the boundary polygon's centroid is (" +
                          log_string((int)lround(boardCenter.x)) + "," + log_string((int)lround(boardCenter.y)) +
                          ") and the free pixel centroid #1323 refused is (" + log_string((int)lround(pixels.x)) +
                          "," + log_string((int)lround(pixels.y)) + "), " +
                          log_string((int)lround(norm(pixels - boardCenter))) +
                          " px away, against a bull's-eye window of " +
                          log_string((int)lround(bullsEyeWindow)) + " px");
            }
        }

        // What each window really keeps and drops, counted rather than inferred (#1393's
        // rule: the honest answer to "did this window mask anything" is the thing it took,
        // not the state of the stages below it). For each window, a component is COUNTED
        // when the final keep/drop decision flips as that window alone is forced open and
        // forced shut -- so a window that decides nothing on this camera says zero, and a
        // window doing the work says which components and how many pixels.
        int decidesN[4] = {0, 0, 0, 0}, decidesPx[4] = {0, 0, 0, 0};
        int admitsN[4] = {0, 0, 0, 0}, admitsPx[4] = {0, 0, 0, 0};
        // WHERE the decided components are, which is the half of the census that says
        // whether a widened window admits ring or room. A distance in spans is the only
        // reading that means the same thing on two rigs: `boardSpan` is the radius of the
        // smallest circle around everything this camera sees as one coloured region.
        vector<string> decided[4];

        for (int i = 1; i < nLabels; i++)
        {
            int area = stats.at<int>(i, CC_STAT_AREA);
            Point2f componentCenter(centroids.at<double>(i, 0), centroids.at<double>(i, 1));

            bool isSizeOK = (area > max(params.minLargeComponentSize, largestArea / params.largestAreaDivisor));
            double distToCenter = norm(componentCenter - boardCenter);
            bool isCentral = (distToCenter < centralityWindow);
            bool isBullsEyeArea = (distToCenter < bullsEyeWindow);

            // Enhanced text filter - specifically target edge text blobs
            int left = stats.at<int>(i, CC_STAT_LEFT);
            int top = stats.at<int>(i, CC_STAT_TOP);
            int width = stats.at<int>(i, CC_STAT_WIDTH);
            int height = stats.at<int>(i, CC_STAT_HEIGHT);
            double aspectRatio = (double)width / height;
            bool isLikelyText = (aspectRatio > params.textAspectRatioMax || aspectRatio < params.textAspectRatioMin) && area < params.textMaxArea;

            // Specific edge-based text removal
            bool isEdgeText = false;
            double edgeDistance = min({left, top, enhancedMask.cols - (left + width), enhancedMask.rows - (top + height)});
            if (edgeDistance < enhancedMask.cols * params.edgeTextThreshold && area < params.edgeTextMaxArea)
            {
                isEdgeText = true;
            }

            // Position-based text filtering (target known problem areas)
            bool isBottomLeftText = (left < enhancedMask.cols * params.bottomLeftTextX && (top + height) > enhancedMask.rows * params.bottomLeftTextY);
            bool isTopRightText = ((left + width) > enhancedMask.cols * params.topRightTextX && top < enhancedMask.rows * params.topRightTextY);
            bool isPositionalText = (isBottomLeftText || isTopRightText) && area < params.positionalTextMaxArea;

            bool isTooSmall = (area < params.minBlobArea);
            bool isTooFarFromCenter = (distToCenter > farWindow);

            // Connectivity check (keep this - it helps with inner rings)
            bool isConnected = false;
            if (area > params.minConnectedArea)
            {
                for (int j = 1; j < nLabels; j++)
                {
                    if (i != j && stats.at<int>(j, CC_STAT_AREA) > params.minConnectedNeighborArea)
                    {
                        Point2f otherCenter(centroids.at<double>(j, 0), centroids.at<double>(j, 1));
                        double dist = norm(componentCenter - otherCenter);
                        if (dist < connectivityWindow)
                        {
                            isConnected = true;
                            break;
                        }
                    }
                }
            }

            // #1394: the whole keep/drop decision as one expression, so each window can be
            // asked what it alone decides by forcing it open and forcing it shut. The four
            // arguments are the four windows in the order the census below prints them.
            auto keptWith = [&](bool central, bool bullsEye, bool tooFar, bool connected)
            {
                return !isEdgeText && !isPositionalText &&
                       (i == largestIdx ||
                        (area > largestArea / params.largestAreaRatio && central && !isLikelyText && !isTooSmall) ||
                        (isSizeOK && connected && !tooFar) ||
                        bullsEye);
            };
            const bool keep = keptWith(isCentral, isBullsEyeArea, isTooFarFromCenter, isConnected);
            {
                // A window can only be forced open where it is asked at all: a component
                // under `minConnectedArea` is never tested for connectivity, so counting it
                // as decided BY connectivity would be a number about the area floor.
                const bool connectable = (area > params.minConnectedArea);
                const bool openShut[4][2] = {
                    {keptWith(true, isBullsEyeArea, isTooFarFromCenter, isConnected),
                     keptWith(false, isBullsEyeArea, isTooFarFromCenter, isConnected)},
                    {keptWith(isCentral, true, isTooFarFromCenter, isConnected),
                     keptWith(isCentral, false, isTooFarFromCenter, isConnected)},
                    {keptWith(isCentral, isBullsEyeArea, false, isConnected),
                     keptWith(isCentral, isBullsEyeArea, true, isConnected)},
                    {keptWith(isCentral, isBullsEyeArea, isTooFarFromCenter, connectable),
                     keptWith(isCentral, isBullsEyeArea, isTooFarFromCenter, false)},
                };
                for (int k = 0; k < 4; k++)
                {
                    if (openShut[k][0] != openShut[k][1])
                    {
                        decidesN[k]++;
                        decidesPx[k] += area;
                        if (keep)
                        {
                            admitsN[k]++;
                            admitsPx[k] += area;
                        }
                        // The big ones by name. A 5791 px blob admitted at 1.3 spans is a
                        // finding; forty 60 px specks are the noise floor this stage exists
                        // to remove, and listing them would bury it.
                        if (area >= 400 && decided[k].size() < 8)
                        {
                            string verdict = "DROPPED";
                            if (keep)
                            {
                                verdict = "kept";
                            }
                            string spans = "n/a";
                            if (boardSpan > 0.0f)
                            {
                                spans = decimals(distToCenter / boardSpan, 2);
                            }
                            decided[k].push_back(verdict + " " + to_string(area) + " px at (" +
                                                 to_string((int)lround(componentCenter.x)) + "," +
                                                 to_string((int)lround(componentCenter.y)) + "), " +
                                                 to_string((int)lround(distToCenter)) + " px out = " +
                                                 spans + " spans");
                        }
                    }
                }
            }

            // KEEP COMPONENT if it's good dartboard stuff, REJECT if it's obvious text
            if (keep)
            {
                // Copy component to filtered mask
                for (int y = top; y < top + height; y++)
                {
                    for (int x = left; x < left + width; x++)
                    {
                        if (y >= 0 && y < labels.rows && x >= 0 && x < labels.cols &&
                            labels.at<int>(y, x) == i)
                        {
                            filteredMask.at<uchar>(y, x) = 255;
                        }
                    }
                }
            }
        }

        {
            // The census #1394 is argued from. A window reading "0 of 0" on both fixtures
            // decides nothing there and its size is a claim about footage nobody has shot;
            // one reading "3 kept of 5 decided" is doing the work the issue is about.
            static const char *names[4] = {"centrality", "bull's-eye", "outer cutoff", "connectivity"};
            const double windowPx[4] = {centralityWindow, bullsEyeWindow, farWindow, connectivityWindow};
            string census;
            for (int k = 0; k < 4; k++)
            {
                if (k > 0)
                {
                    census += "; ";
                }
                census += string(names[k]) + " at " + to_string((int)lround(windowPx[k])) + " px decides " +
                          to_string(decidesN[k]) + " of " + to_string(nLabels - 1) + " components (" +
                          to_string(decidesPx[k]) + " px), keeping " + to_string(admitsN[k]) + " of them (" +
                          to_string(admitsPx[k]) + " px)";
                for (size_t d = 0; d < decided[k].size(); d++)
                {
                    census += " [" + decided[k][d] + "]";
                }
            }
            log_debug("Camera " + log_string(camera_idx + 1) + " colour windows kept " +
                      log_string(countNonZero(filteredMask)) + " px of " +
                      log_string(countNonZero(enhancedMask)) + ": " + log_string_src(census));
        }

        // ===== FINAL SECTION: CREATE COLORED OUTPUT =====
        Mat redGreenFrame = Mat::zeros(roiFrame.size(), CV_8UC3);

        for (int y = 0; y < filteredMask.rows; y++)
        {
            for (int x = 0; x < filteredMask.cols; x++)
            {
                if (filteredMask.at<uchar>(y, x) > 0)
                {
                    if (redMask.at<uchar>(y, x) > 0)
                    {
                        redGreenFrame.at<Vec3b>(y, x) = Vec3b(0, 0, 255); // Red
                    }
                    else if (greenMask.at<uchar>(y, x) > 0)
                    {
                        redGreenFrame.at<Vec3b>(y, x) = Vec3b(0, 255, 0); // Green
                    }
                    else
                    {
                        // Gray for structural pixels
                        bool nearbyColor = false;
                        int checkDistance = params.nearbyColorCheckDistance;

                        for (int ny = max(0, y - checkDistance); ny <= min(filteredMask.rows - 1, y + checkDistance) && !nearbyColor; ny++)
                        {
                            for (int nx = max(0, x - checkDistance); nx <= min(filteredMask.cols - 1, x + checkDistance) && !nearbyColor; nx++)
                            {
                                if (redMask.at<uchar>(ny, nx) > 0 || greenMask.at<uchar>(ny, nx) > 0)
                                {
                                    nearbyColor = true;
                                    break;
                                }
                            }
                        }

                        if (nearbyColor)
                        {
                            redGreenFrame.at<Vec3b>(y, x) = Vec3b(80, 80, 80); // Gray structural pixels
                        }
                    }
                }
            }
        }

        // Save debug images
        if (debug_mode)
        {
            odfs::ensureDirectory("debug_frames/color_processing");
            imwrite("debug_frames/color_processing/red_green_frame_" + to_string(camera_idx) + ".jpg", redGreenFrame);
        }

        return redGreenFrame; // Return colored frame directly!
    }

    // Helper for when individual masks are needed
    pair<Mat, Mat> getIndividualColorMasks(const Mat &roiFrame, const ColorParams &params)
    {
        // Simplified version of color detection that just returns the masks
        Mat hsvFrame;
        cvtColor(roiFrame, hsvFrame, COLOR_BGR2HSV);

        // Basic red detection
        Mat redMask1, redMask2;
        inRange(hsvFrame,
                Scalar(params.redLowHue1, params.redLowSat1, params.redLowVal1),
                Scalar(params.redHighHue1, params.redHighSat1, params.redHighVal1), redMask1);
        inRange(hsvFrame,
                Scalar(params.redLowHue2, params.redLowSat2, params.redLowVal2),
                Scalar(params.redHighHue2, params.redHighSat2, params.redHighVal2), redMask2);
        Mat redMask = redMask1 | redMask2;

        // Basic green detection
        Mat greenMask;
        inRange(hsvFrame,
                Scalar(params.greenLowHue, params.greenLowSat, params.greenLowVal),
                Scalar(params.greenHighHue, params.greenHighSat, params.greenHighVal), greenMask);

        return {redMask, greenMask};
    }

} // namespace color_detection
