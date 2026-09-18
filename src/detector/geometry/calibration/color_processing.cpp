#include "color_processing.hpp"
#include "logging.hpp"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <string>

using namespace cv;
using namespace std;

namespace color_processing
{
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
        {
            // Apply stronger closing to top half where rings are often broken
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

        int largestIdx = 0;
        int largestArea = 0;
        for (int i = 1; i < nLabels; i++)
        {
            int area = stats.at<int>(i, CC_STAT_AREA);
            if (area > largestArea)
            {
                largestArea = area;
                largestIdx = i;
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
        // What is NOT moved: SECTION 5's bull's-eye enhancement is a frame-centred
        // window too, and it runs before anything here has measured anything. It only
        // ever ADDS pixels to the red mask, so it cannot drop a bull, and its dilation
        // is part of what #1320's bull-to-board ratios were measured through -- moving
        // it would move the ground those constants stand on for a gain no camera needs.
        const double frameArea = static_cast<double>(enhancedMask.cols) * enhancedMask.rows;

        vector<vector<Point>> boardContours;
        findContours(enhancedMask, boardContours, RETR_EXTERNAL, CHAIN_APPROX_SIMPLE);
        double boardArea = 0.0;
        int boardIndex = -1;
        for (size_t c = 0; c < boardContours.size(); c++)
        {
            const double enclosed = contourArea(boardContours[c]);
            if (enclosed > boardArea)
            {
                boardArea = enclosed;
                boardIndex = static_cast<int>(c);
            }
        }

        Point2f boardCenter = imageCenter;
        bool boardMeasured = false;
        if (boardIndex >= 0 && boardArea >= frameArea * params.minBoardAreaPercent)
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
            const string where = " the largest coloured region encloses " + to_string(static_cast<long>(boardArea)) +
                                 " px, " + share.str() + "% of the frame, and a board encloses at least " + least.str() + "%";
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

        for (int i = 1; i < nLabels; i++)
        {
            int area = stats.at<int>(i, CC_STAT_AREA);
            Point2f componentCenter(centroids.at<double>(i, 0), centroids.at<double>(i, 1));

            bool isSizeOK = (area > max(params.minLargeComponentSize, largestArea / params.largestAreaDivisor));
            double distToCenter = norm(componentCenter - boardCenter);
            bool isCentral = (distToCenter < enhancedMask.cols * params.centralityThreshold);
            bool isBullsEyeArea = (distToCenter < enhancedMask.cols * params.bullsEyeThreshold);

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
            bool isTooFarFromCenter = (distToCenter > (enhancedMask.cols * params.maxDistanceFromCenter / 2));

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
                        if (dist < enhancedMask.cols * params.connectivityThreshold)
                        {
                            isConnected = true;
                            break;
                        }
                    }
                }
            }

            // KEEP COMPONENT if it's good dartboard stuff, REJECT if it's obvious text
            if (!isEdgeText && !isPositionalText &&
                (i == largestIdx ||
                 (area > largestArea / params.largestAreaRatio && isCentral && !isLikelyText && !isTooSmall) ||
                 (isSizeOK && isConnected && !isTooFarFromCenter) ||
                 isBullsEyeArea))
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
