#include <opencv2/imgproc.hpp>
#include "logging.hpp"
#include <iostream>

#include "wire_processing.hpp"
#include "wire_model.hpp"
#include "ring_identity.hpp"
#include "geometry_calibration.hpp"
#include "utils.hpp"
#include <cstdlib>
#include <string>

using namespace cv;
using namespace std;

namespace wire_processing
{
    namespace
    {
        /**
         * #1441's falsification, in the shape OD_ROI, OD_ROI_MARGIN, OD_BOARD,
         * OD_BULL_CARVE and OD_COLOUR_WINDOWS established: one binary, the region chosen
         * at run time, so "different build" is never a confound.
         *
         * OD_WIRE_REGION=doubles draws this stage's region at the fitted doubles ellipse
         * itself, which is what every commit before this issue read, and is row 1.00 of
         * the sweep in the header. Anything but that exact word is ignored, so a typo
         * reads inside the region this stage was measured in rather than silently inside
         * the one it was broken in.
         */
        bool wireReadsOutToTheDoubles()
        {
            static bool v = []
            {
                const char *e = std::getenv("OD_WIRE_REGION");
                return e && std::string(e) == "doubles";
            }();
            return v;
        }

        /**
         * OD_WIRE_REGION_MARGIN=<x> moves the region on one binary, the way
         * OD_ROI_MARGIN moves the board finder's, and is how the header's sweep was
         * taken. A value this cannot use is ignored rather than obeyed, and there are
         * two ways to be unusable:
         *
         *   zero or less, or anything atof cannot read -- a region of no radius is a
         *   black frame, and this stage would then refuse every camera on a wire count
         *   while the reason is a region nobody printed;
         *
         *   past the board's rim. A board is 225.5 mm to its rim and 170 mm to the outer
         *   doubles wire, so 1.33 of this ellipse is the whole board and there is
         *   nothing further out for a stage that intersects every endpoint it returns
         *   with the doubles ellipse. A value beyond it names no region on any board.
         *
         * Both are ignored rather than clamped: a clamp obeys a value nobody meant by
         * quietly turning it into one that was never asked for, which is how #1378 was
         * invisible for nineteen merges.
         */
        constexpr double kRegionPastTheRim = 225.5 / 170.0;

        double wireRegionAsked(double stated)
        {
            static double asked = []
            {
                const char *e = std::getenv("OD_WIRE_REGION_MARGIN");
                return e ? std::atof(e) : 0.0;
            }();
            return (asked > 0.0 && asked <= kRegionPastTheRim) ? asked : stated;
        }

        /**
         * #1442's falsification, in the same shape as OD_WIRE_REGION above: one binary,
         * the test chosen at run time.
         *
         * OD_WIRE_COUNT=atleast restores the one-sided test every commit before #1442
         * asked -- a count of kWiresRequired OR MORE is a whole ring -- so the population
         * this issue is about can be counted twice on one binary and the difference is
         * this comparison and nothing else. Anything but that exact word is ignored, so a
         * typo reads two-sided rather than silently reading in the behaviour the issue was
         * filed on. There is deliberately no word for the reverse: the two-sided test is
         * what this stage IS, not a mode it is in.
         */
        bool anyCountFromTwentyUpIsAWholeRing()
        {
            static bool v = []
            {
                const char *e = std::getenv("OD_WIRE_COUNT");
                return e && std::string(e) == "atleast";
            }();
            return v;
        }
    }

    bool isAWholeRing(int wiresProposed)
    {
        // The whole of #1442 is the second half of this line. kWiresRequired carries why
        // twenty-two is the same fault as nineteen rather than a milder one.
        return anyCountFromTwentyUpIsAWholeRing()
                   ? wiresProposed >= kWiresRequired
                   : wiresProposed == kWiresRequired;
    }

    RotatedRect regionOf(const DartboardCalibration &calib, const WireRegionParams &params)
    {
        // OD_WIRE_REGION=doubles restores the pre-#1441 region whole: the stage's masks
        // were the fitted doubles ellipse itself, which is a scale of 1.0.
        const float scale = wireReadsOutToTheDoubles()
                                ? 1.0f
                                : (float)wireRegionAsked(params.regionOfDoublesEllipse);
        RotatedRect region = calib.ellipses.outerDoubleEllipse;
        region.size.width *= scale;
        region.size.height *= scale;
        return region;
    }

    Mat detectMetalWires(const Mat &frame, const Mat &colorMask, const DartboardCalibration &calib)
    {
        Mat wireEnhanced;

        // STEP 1: Create ROI mask with 5% buffer around the wire stage's own region
        // (#1441: `regionOf`, which is a fraction of the FITTED doubles ellipse. At
        // scale 1.0 this is the outer doubles ring and therefore exactly what this line
        // read before that issue.)
        Mat roiMask = Mat::zeros(frame.size(), CV_8UC1);
        RotatedRect bufferedRing = regionOf(calib);
        bufferedRing.size.width *= 1.05f;
        bufferedRing.size.height *= 1.05f;
        ellipse(roiMask, bufferedRing, Scalar(255), -1);

        // Apply ROI mask to source image
        Mat roiImage;
        frame.copyTo(roiImage, roiMask);

        // STEP 2: Convert to HSV for better color filtering
        Mat hsv;
        cvtColor(roiImage, hsv, COLOR_BGR2HSV);

        // STEP 3: Remove black pixels more aggressively (keep only bright pixels)
        Mat nonBlackMask;
        inRange(hsv, Scalar(0, 0, 100), Scalar(180, 255, 255), nonBlackMask);

        // STEP 4: Remove green pixels (HSV range for green)
        Mat nonGreenMask;
        inRange(hsv, Scalar(40, 50, 50), Scalar(80, 255, 255), nonGreenMask);
        bitwise_not(nonGreenMask, nonGreenMask);

        // STEP 5: Remove red pixels (HSV range for red - two ranges due to hue wrap)
        Mat redMask1, redMask2, nonRedMask;
        inRange(hsv, Scalar(0, 50, 50), Scalar(15, 255, 255), redMask1);
        inRange(hsv, Scalar(165, 50, 50), Scalar(180, 255, 255), redMask2);
        bitwise_or(redMask1, redMask2, nonRedMask);
        bitwise_not(nonRedMask, nonRedMask);

        // STEP 6: Combine all masks to keep only bright, non-green, non-red pixels
        bitwise_and(nonBlackMask, nonGreenMask, wireEnhanced);
        bitwise_and(wireEnhanced, nonRedMask, wireEnhanced);
        bitwise_and(wireEnhanced, roiMask, wireEnhanced);

        // STEP 7: Invert the mask so wire areas are white
        bitwise_not(wireEnhanced, wireEnhanced);
        bitwise_and(wireEnhanced, roiMask, wireEnhanced);

        // STEP 8: Simple morphological operations to clean up
        Mat kernel = getStructuringElement(MORPH_ELLIPSE, Size(5, 5));
        morphologyEx(wireEnhanced, wireEnhanced, MORPH_CLOSE, kernel);

        // Remove small noise
        Mat smallKernel = getStructuringElement(MORPH_ELLIPSE, Size(3, 3));
        morphologyEx(wireEnhanced, wireEnhanced, MORPH_OPEN, smallKernel);

        // STEP 9: Extract green areas from colorMask and subtract them
        vector<Mat> colorChannels;
        split(colorMask, colorChannels);
        Mat greenChannel = colorChannels[1]; // Green channel from colorMask

        // Create green mask
        Mat greenMask;
        threshold(greenChannel, greenMask, 50, 255, THRESH_BINARY);

        // Subtract green areas from wireEnhanced (remove doubles/triples)
        subtract(wireEnhanced, greenMask, wireEnhanced);

        // STEP 10: Apply final tighter ROI mask (-5% from the region) to clean up outer artifacts
        Mat finalRoiMask = Mat::zeros(frame.size(), CV_8UC1);
        RotatedRect tighterRing = regionOf(calib);
        tighterRing.size.width *= 0.95f; // -5% instead of +5%
        tighterRing.size.height *= 0.95f;
        ellipse(finalRoiMask, tighterRing, Scalar(255), -1);

        // Apply the final tighter mask
        bitwise_and(wireEnhanced, finalRoiMask, wireEnhanced);

        // STEP 11: Advanced noise cleanup - remove small isolated dots and artifacts
        vector<vector<Point>> contours;
        findContours(wireEnhanced, contours, RETR_EXTERNAL, CHAIN_APPROX_SIMPLE);

        // Create clean mask by filtering out small contours
        Mat cleanMask = Mat::zeros(wireEnhanced.size(), CV_8UC1);
        for (size_t i = 0; i < contours.size(); i++)
        {
            double area = contourArea(contours[i]);
            if (area > 100)
            { // Only keep contours larger than 100 pixels
                drawContours(cleanMask, contours, i, Scalar(255), -1);
            }
        }

        // Final morphological cleanup to smooth edges
        Mat finalKernel = getStructuringElement(MORPH_ELLIPSE, Size(3, 3));
        morphologyEx(cleanMask, wireEnhanced, MORPH_CLOSE, finalKernel);

        // Apply final mask to wireEnhanced
        Mat bullMask = Mat::zeros(wireEnhanced.size(), CV_8UC1);
        ellipse(bullMask, calib.ellipses.outerBullEllipse, Scalar(255), -1);

        // Remove bull area from wire mask
        wireEnhanced = wireEnhanced & ~bullMask;

        return wireEnhanced;
    }

    // Hough line-based wire detection
    vector<Point2f> findWiresByHoughLines(const Mat &mask, const Mat &colorMask, const DartboardCalibration &calib, bool debug_mode, const WireDetectionConfig &config)
    {
        vector<Point2f> wirePoints;

        log_debug("STARTING HOUGH LINE WIRE DETECTION for camera " + log_string(calib.camera_index));

        // Get the wire mask
        Mat wireMask = detectMetalWires(mask, colorMask, calib);

        // Apply edge detection
        Mat edges;
        Canny(wireMask, edges, config.cannyLow, config.cannyHigh);

        // Detect lines using Hough transform
        vector<Vec4i> lines;
        HoughLinesP(edges, lines, 1, CV_PI / 180, config.houghThreshold, config.houghMinLineLength, config.houghMaxLineGap);

        log_debug("Found " + log_string(lines.size()) + " Hough lines");

        // Filter lines that are radial (pass near bull center)
        vector<Vec4i> radialLines;
        Point2f bullCenter = Point2f(calib.bullCenter);

        for (const Vec4i &line : lines)
        {
            Point2f p1(line[0], line[1]);
            Point2f p2(line[2], line[3]);

            // Calculate distance from line to bull center
            Point2f lineVec = p2 - p1;
            Point2f centerVec = bullCenter - p1;

            // Distance from point to line formula
            float lineLength = norm(lineVec);
            if (lineLength < 10)
                continue; // Skip very short lines

            Point2f normalizedLine = lineVec / lineLength;
            float distanceToCenter = abs(centerVec.x * normalizedLine.y - centerVec.y * normalizedLine.x);

            if (distanceToCenter < config.houghDistanceTolerance)
            {
                radialLines.push_back(line);
            }
        }

        log_debug("Filtered to " + log_string(radialLines.size()) + " radial lines");

        // Convert lines to wire endpoints
        for (const Vec4i &line : radialLines)
        {
            Point2f p1(line[0], line[1]);
            Point2f p2(line[2], line[3]);

            // Determine which point is farther from center (that's our wire endpoint)
            float dist1 = norm(p1 - bullCenter);
            float dist2 = norm(p2 - bullCenter);

            Point2f farPoint = (dist1 > dist2) ? p1 : p2;
            Point2f direction = farPoint - bullCenter;
            direction = direction / norm(direction); // Normalize

            // Calculate angle and extend to ellipse boundary
            float angle = atan2(direction.y, direction.x);
            Point2f wireEnd = math::intersectRayWithEllipse(bullCenter, angle, calib.ellipses.outerDoubleEllipse);

            wirePoints.push_back(wireEnd);
        }

        // Sort by angle
        sort(wirePoints.begin(), wirePoints.end(), [&calib](const Point2f &a, const Point2f &b)
             {
            Point2f dirA = a - Point2f(calib.bullCenter);
            Point2f dirB = b - Point2f(calib.bullCenter);
            float angleA = atan2(dirA.y, dirA.x);
            float angleB = atan2(dirB.y, dirB.x);
            return angleA < angleB; });

        // Debug visualization
        if (debug_mode)
        {
            odfs::ensureDirectory("debug_frames/wire_processing");

            // Show detected lines
            Mat linesDebug = mask.clone();

            // Draw all detected lines in gray
            for (const Vec4i &line : lines)
            {
                cv::line(linesDebug, Point(line[0], line[1]), Point(line[2], line[3]), Scalar(128, 128, 128), 1);
            }

            // Draw radial lines in cyan
            for (const Vec4i &line : radialLines)
            {
                cv::line(linesDebug, Point(line[0], line[1]), Point(line[2], line[3]), Scalar(255, 255, 0), 2);
            }

            // Draw final wire lines in yellow
            for (size_t i = 0; i < wirePoints.size(); i++)
            {
                cv::line(linesDebug, calib.bullCenter, wirePoints[i], Scalar(0, 255, 255), 2);
                circle(linesDebug, wirePoints[i], 5, Scalar(0, 255, 255), -1);
                putText(linesDebug, to_string(i), Point(wirePoints[i].x + 5, wirePoints[i].y),
                        FONT_HERSHEY_SIMPLEX, 0.4, Scalar(255, 255, 255), 1);
            }

            imwrite("debug_frames/wire_processing/hough_lines_result_" + to_string(calib.camera_index) + ".jpg", linesDebug);
            imwrite("debug_frames/wire_processing/hough_edges_" + to_string(calib.camera_index) + ".jpg", edges);
        }

        log_debug("HOUGH LINE DETECTION COMPLETE: Found " + log_string(wirePoints.size()) + " wire points");

        return wirePoints;
    }

    // wire detection - no filtering, trusting the mask
    vector<Point2f> findWiresByColorTransitions(const Mat &mask, const Mat &colorMask, const DartboardCalibration &calib, bool debug_mode)
    {
        vector<Point2f> wirePoints;

        log_debug("STARTING WIRE DETECTION for camera " + log_string(calib.camera_index));

        // Get the wire mask (areas between dartboard segments)
        Mat wireMask = detectMetalWires(mask, colorMask, calib);

        // Find contours (each should represent a dartboard segment)
        vector<vector<Point>> contours;
        findContours(wireMask, contours, RETR_EXTERNAL, CHAIN_APPROX_SIMPLE);
        log_debug("Found " + log_string(contours.size()) + " raw contours");

        // Just extract angles from ALL contours the mask gives us
        for (size_t i = 0; i < contours.size(); i++)
        {
            const vector<Point> &segment = contours[i];

            // Only skip truly empty contours
            if (segment.empty())
            {
                continue;
            }

            log_debug("Processing contour " + log_string(i) + " with " + log_string(segment.size()) + " points");

            // Find min/max angles of this segment
            vector<float> segmentAngles;
            for (const Point &pt : segment)
            {
                Point2f dir = Point2f(pt) - Point2f(calib.bullCenter);
                float angle = atan2(dir.y, dir.x);
                segmentAngles.push_back(angle);
            }

            sort(segmentAngles.begin(), segmentAngles.end());

            float leftAngle = segmentAngles.front();
            float rightAngle = segmentAngles.back();

            // Handle angle wraparound (when segment crosses 0°)
            if (rightAngle - leftAngle > CV_PI)
            {
                // Find the largest gap - that's where the wraparound is
                float maxGap = 0;
                size_t gapIdx = 0;
                for (size_t j = 1; j < segmentAngles.size(); j++)
                {
                    float gap = segmentAngles[j] - segmentAngles[j - 1];
                    if (gap > maxGap)
                    {
                        maxGap = gap;
                        gapIdx = j;
                    }
                }
                leftAngle = segmentAngles[gapIdx];
                rightAngle = segmentAngles[gapIdx - 1];
            }

            // Create wire endpoints by intersecting with ellipse
            Point2f leftWire = math::intersectRayWithEllipse(
                Point2f(calib.bullCenter), leftAngle, calib.ellipses.outerDoubleEllipse);
            Point2f rightWire = math::intersectRayWithEllipse(
                Point2f(calib.bullCenter), rightAngle, calib.ellipses.outerDoubleEllipse);

            wirePoints.push_back(leftWire);
            wirePoints.push_back(rightWire);

            log_debug("Segment " + log_string(i) + " -> wires at " +
                      log_string(leftAngle * 180.0f / CV_PI) + "° and " +
                      log_string(rightAngle * 180.0f / CV_PI) + "°");
        }

        // Sort all wire points by angle for consistent ordering
        sort(wirePoints.begin(), wirePoints.end(), [&calib](const Point2f &a, const Point2f &b)
             {
            Point2f dirA = a - Point2f(calib.bullCenter);
            Point2f dirB = b - Point2f(calib.bullCenter);
            float angleA = atan2(dirA.y, dirA.x);
            float angleB = atan2(dirB.y, dirB.x);
            return angleA < angleB; });

        // debuging code
        if (debug_mode)
        {

            // Create debug visualizations
            odfs::ensureDirectory("debug_frames/wire_processing");

            // Existing debug: Show all contours
            Mat contoursImg = Mat::zeros(mask.size(), CV_8UC3);
            for (size_t i = 0; i < contours.size(); i++)
            {
                drawContours(contoursImg, contours, i, {255, 255, 255}, 1);

                if (!contours[i].empty())
                {
                    Moments m = moments(contours[i]);
                    if (m.m00 > 0)
                    {
                        Point center(m.m10 / m.m00, m.m01 / m.m00);
                        putText(contoursImg, to_string(i), center, FONT_HERSHEY_SIMPLEX, 0.5, Scalar(255, 255, 255), 1);
                    }
                }
            }
            imwrite("debug_frames/wire_processing/all_contours_" + to_string(calib.camera_index) + ".jpg", contoursImg);

            // DEBUG CODE: START HERE
            // Layered visualization on blue background
            Mat layeredDebug = Mat::zeros(mask.size(), CV_8UC3);

            // Layer 1: Blue mask (50% transparent)
            for (int y = 0; y < wireMask.rows; y++)
            {
                for (int x = 0; x < wireMask.cols; x++)
                {
                    if (wireMask.at<uchar>(y, x) > 0)
                    {
                        layeredDebug.at<Vec3b>(y, x) = Vec3b(255, 100, 0); // Blue with some intensity
                    }
                }
            }

            // Layer 2: Cyan contours (3px, some transparency)
            for (size_t i = 0; i < contours.size(); i++)
            {
                drawContours(layeredDebug, contours, i, Scalar(255, 255, 0), 3); // Cyan - good on blue
            }

            // Layer 3: Yellow/White wire lines (2px)
            for (size_t i = 0; i < wirePoints.size(); i++)
            {
                Scalar lineColor = (i % 2 == 0) ? Scalar(0, 255, 255) : Scalar(255, 255, 255); // Yellow/White - good on blue+cyan
                line(layeredDebug, calib.bullCenter, wirePoints[i], lineColor, 2);
                circle(layeredDebug, wirePoints[i], 4, lineColor, -1);
            }

            imwrite("debug_frames/wire_processing/wire_processing_result_" + to_string(calib.camera_index) + ".jpg", layeredDebug);

            // Same layers but on actual frame
            Mat frameLayered = mask.clone();

            // Layer 1: Blue mask overlay on frame (50% blend)
            Mat blueMask = Mat::zeros(mask.size(), CV_8UC3);
            for (int y = 0; y < wireMask.rows; y++)
            {
                for (int x = 0; x < wireMask.cols; x++)
                {
                    if (wireMask.at<uchar>(y, x) > 0)
                    {
                        blueMask.at<Vec3b>(y, x) = Vec3b(255, 0, 0); // Pure blue
                    }
                }
            }
            addWeighted(frameLayered, 0.7, blueMask, 0.3, 0, frameLayered);

            // Layer 2: Cyan contours (3px)
            for (size_t i = 0; i < contours.size(); i++)
            {
                drawContours(frameLayered, contours, i, Scalar(255, 255, 0), 3); // Cyan
            }

            // Layer 3: Yellow/White wire lines (2px)
            for (size_t i = 0; i < wirePoints.size(); i++)
            {
                Scalar lineColor = (i % 2 == 0) ? Scalar(0, 255, 255) : Scalar(255, 255, 255); // Yellow/White
                line(frameLayered, calib.bullCenter, wirePoints[i], lineColor, 2);
                circle(frameLayered, wirePoints[i], 4, lineColor, -1);
            }

            imwrite("debug_frames/wire_processing/wire_process_frame_result_" + to_string(calib.camera_index) + ".jpg", frameLayered);
            // DEBUG CODE: START HERE
        }

        log_debug("WIRE DETECTION COMPLETE: Found " + log_string(wirePoints.size()) + " wire points from " +
                  log_string(contours.size()) + " segments (NO FILTERING)");

        return wirePoints;
    }

    // Group wires by angular proximity
    vector<vector<Point2f>> groupWiresByAngle(const vector<Point2f> &wires, Point2f center, float angleTolerance)
    {
        vector<vector<Point2f>> groups;
        vector<bool> used(wires.size(), false);

        for (size_t i = 0; i < wires.size(); i++)
        {
            if (used[i])
                continue;

            vector<Point2f> group;
            Point2f dir1 = wires[i] - center;
            float angle1 = atan2(dir1.y, dir1.x) * 180.0f / CV_PI;

            group.push_back(wires[i]);
            used[i] = true;

            // Find all wires within angular tolerance
            for (size_t j = i + 1; j < wires.size(); j++)
            {
                if (used[j])
                    continue;

                Point2f dir2 = wires[j] - center;
                float angle2 = atan2(dir2.y, dir2.x) * 180.0f / CV_PI;

                float angleDiff = abs(angle1 - angle2);
                if (angleDiff > 180.0f)
                    angleDiff = 360.0f - angleDiff;

                if (angleDiff <= angleTolerance)
                {
                    group.push_back(wires[j]);
                    used[j] = true;
                }
            }

            groups.push_back(group);
        }

        return groups;
    }

    // Score a wire based on line quality (your brilliant scoring idea!)
    float scoreWire(Point2f wire, Point2f center, const Mat &wireMask)
    {
        // Cast ray from center through wire point
        Point2f direction = wire - center;
        direction = direction / norm(direction);

        float score = 0.0f;
        int samples = 0;

        // Sample along the line from center to wire
        for (float dist = 10.0f; dist < norm(wire - center); dist += 2.0f)
        {
            Point2f samplePoint = center + direction * dist;

            if (samplePoint.x < 2 || samplePoint.y < 2 ||
                samplePoint.x >= wireMask.cols - 2 || samplePoint.y >= wireMask.rows - 2)
            {
                continue;
            }

            // Check 2-pixel padding on both sides of the line
            Point2f perpendicular(-direction.y, direction.x);

            // Sample left and right of the line
            Point2f leftPoint = samplePoint + perpendicular * 2.0f;
            Point2f rightPoint = samplePoint - perpendicular * 2.0f;

            if (leftPoint.x >= 0 && leftPoint.y >= 0 && leftPoint.x < wireMask.cols && leftPoint.y < wireMask.rows &&
                rightPoint.x >= 0 && rightPoint.y >= 0 && rightPoint.x < wireMask.cols && rightPoint.y < wireMask.rows)
            {

                // Check for white pixels (wire areas) on both sides
                int leftWhite = (wireMask.at<uchar>(leftPoint.y, leftPoint.x) > 128) ? 1 : 0;
                int rightWhite = (wireMask.at<uchar>(rightPoint.y, rightPoint.x) > 128) ? 1 : 0;

                // Score higher if we have white on both sides (wire boundary)
                score += leftWhite + rightWhite;
                samples++;
            }
        }

        return (samples > 0) ? score / samples : 0.0f;
    }

    // Select average wire position from a group (simpler than scoring!)
    Point2f selectAverageWireFromGroup(const vector<Point2f> &group, Point2f center)
    {
        if (group.empty())
            return Point2f(0, 0);
        if (group.size() == 1)
            return group[0];

        // Calculate average position
        Point2f avgPosition(0, 0);
        for (const Point2f &wire : group)
        {
            avgPosition += wire;
        }
        avgPosition = avgPosition / (float)group.size();

        // Convert to angle and extend to ellipse boundary for consistency
        Point2f direction = avgPosition - center;
        float avgAngle = atan2(direction.y, direction.x);

        log_debug("Average wire from group of " + log_string(group.size()) + " at angle: " +
                  log_string(avgAngle * 180.0f / CV_PI) + "°");

        return avgPosition;
    }

    // What the two suppliers proposed, before anything is grouped, fitted or kept.
    // #1467 made this its own function: the fit and the census both read it, and the
    // census must read the SAME candidates the stage read.
    vector<Point2f> wireCandidates(const Mat &frame, const Mat &colorMask, const DartboardCalibration &calib, const WireDetectionConfig &config)
    {
        vector<Point2f> contourWires = findWiresByColorTransitions(frame, colorMask, calib, false);
        vector<Point2f> houghWires = findWiresByHoughLines(frame, colorMask, calib, false, config);

        log_debug("Contour method found " + log_string(contourWires.size()) + " wires");
        log_debug("Hough method found " + log_string(houghWires.size()) + " wires");

        vector<Point2f> allWires = contourWires;
        allWires.insert(allWires.end(), houghWires.begin(), houghWires.end());

        log_debug("Combined total: " + log_string(allWires.size()) + " wire candidates");
        return allWires;
    }

    // Ensemble method combining both approaches - AVERAGE VERSION
    vector<Point2f> findWiresByEnsemble(const Mat &mask, const Mat &colorMask, const DartboardCalibration &calib, bool debug_mode, const WireDetectionConfig &config)
    {
        log_debug("STARTING ENSEMBLE WIRE DETECTION (AVERAGE) for camera " + log_string(calib.camera_index));

        vector<Point2f> allWires = wireCandidates(mask, colorMask, calib, config);

        // Group wires by angular proximity (±9° tolerance for 18° dartboard segments)
        vector<vector<Point2f>> wireGroups = groupWiresByAngle(allWires, Point2f(calib.bullCenter), 9.0f);

        log_debug("Grouped into " + log_string(wireGroups.size()) + " angular groups");

        // Select AVERAGE wire from each group (no complex scoring!)
        vector<Point2f> finalWires;
        for (size_t i = 0; i < wireGroups.size(); i++)
        {
            Point2f avgWire = selectAverageWireFromGroup(wireGroups[i], Point2f(calib.bullCenter));
            finalWires.push_back(avgWire);

            log_debug("Group " + log_string(i) + " has " + log_string(wireGroups[i].size()) + " candidates, selected average");
        }

        // Sort by angle
        sort(finalWires.begin(), finalWires.end(), [&calib](const Point2f &a, const Point2f &b)
             {
            Point2f dirA = a - Point2f(calib.bullCenter);
            Point2f dirB = b - Point2f(calib.bullCenter);
            float angleA = atan2(dirA.y, dirA.x);
            float angleB = atan2(dirB.y, dirB.x);
            return angleA < angleB; });

        // Debug visualization
        if (debug_mode)
        {
            odfs::ensureDirectory("debug_frames/wire_processing");

            Mat ensembleDebug = mask.clone();

            // Draw grouped candidates in different colors
            vector<Scalar> colors = {
                Scalar(255, 0, 0), Scalar(0, 255, 0), Scalar(0, 0, 255),
                Scalar(255, 255, 0), Scalar(255, 0, 255), Scalar(0, 255, 255)};

            // Draw all candidates in gray
            for (const Point2f &wire : allWires)
            {
                line(ensembleDebug, calib.bullCenter, wire, Scalar(255, 255, 0), 1);
                circle(ensembleDebug, wire, 3, Scalar(128, 128, 128), 1);
            }

            // Draw each group in different colors
            for (size_t i = 0; i < wireGroups.size(); i++)
            {
                Scalar color = colors[i % colors.size()];
                for (const Point2f &wire : wireGroups[i])
                {
                    circle(ensembleDebug, wire, 6, color, 2);
                }
            }

            // Draw final averaged wires in bright yellow (thicker)
            for (size_t i = 0; i < finalWires.size(); i++)
            {
                line(ensembleDebug, calib.bullCenter, finalWires[i], Scalar(0, 255, 255), 2);
                circle(ensembleDebug, finalWires[i], 6, Scalar(0, 255, 255), 1);
                putText(ensembleDebug, to_string(i), Point(finalWires[i].x + 10, finalWires[i].y),
                        FONT_HERSHEY_SIMPLEX, 0.5, Scalar(255, 255, 255), 2);
            }

            imwrite("debug_frames/wire_processing/ensemble_average_result_" + to_string(calib.camera_index) + ".jpg", ensembleDebug);
        }

        log_debug("ENSEMBLE AVERAGE DETECTION COMPLETE: Selected " + log_string(finalWires.size()) + " averaged wires from " + log_string(allWires.size()) + " candidates");

        return finalWires;
    }

    WireData processWires(const Mat &frame, const Mat &colorMask, const DartboardCalibration &calib, bool enableDebug, const WireDetectionConfig &config)
    {
        WireData result;
        result.camera_index = calib.camera_index;

        if (frame.empty() || colorMask.empty())
        {
            // #1321: a fact nothing else has reported, so it stays at ERROR -- and it
            // now says which camera and which of the two inputs was missing.
            log_error("Camera " + log_string(calib.camera_index + 1) +
                      " wire detection has nothing to read: " +
                      string(frame.empty() ? "the frame is empty" : "the colour mask is empty") + ".");
            return result;
        }

        if (!calib.ellipses.hasValidDoubles)
        {
            // #1321: an echo, not a second fault. calibrateSingleCamera has already said
            // which camera failed and what count fell short; this stage is declining to
            // run on a result that was never produced, which is correct behaviour and
            // not news. It was one of the three ERROR lines a tester got instead of a
            // reason.
            log_debug("Declining to detect wires for camera " + log_string(calib.camera_index + 1) +
                      ": no valid doubles ellipse (already reported)");
            return result;
        }

        log_debug("Starting wire detection for camera " + log_string(calib.camera_index));
        log_debug("Frame size: " + log_string(frame.cols) + "x" + log_string(frame.rows));

        // #1441: the region this stage read inside, said out loud. #1378 was invisible
        // for nineteen merges because a region clipped a ring and no line of any log
        // mentioned a region at all; this stage now names its own beside the count it is
        // about to refuse a camera on.
        const RotatedRect wireRegion = regionOf(calib);
        log_debug("Camera " + log_string(calib.camera_index + 1) + " wire region: " +
                  log_string((int)wireRegion.size.width) + "x" + log_string((int)wireRegion.size.height) +
                  " px, drawn from the doubles ring fitted at STEP 6 (" +
                  log_string((int)calib.ellipses.outerDoubleEllipse.size.width) + "x" +
                  log_string((int)calib.ellipses.outerDoubleEllipse.size.height) +
                  " px) and not from the board finder's search region");

        vector<Point2f> colorWires;

        if (wire_model::modelNotAsked())
        {
            // OD_WIRE_MODEL=count: the counting path, whole, on this same binary.
            log_debug("Using ENSEMBLE detection method (Contour + Hough + Scoring)");
            colorWires = findWiresByEnsemble(frame, colorMask, calib, enableDebug, config);
        }
        else
        {
            // #1467: FIT TWENTY RATHER THAN COUNT TO TWENTY.
            const vector<Point2f> candidates = wireCandidates(frame, colorMask, calib, config);
            result.fit_candidates = (int)candidates.size();
            result.fit_asked = true;

            // WHICH RING THE CONIC IS, asked of #1423 rather than assumed. The model
            // measures the bull's offset in units of the conic's OWN radius, so a conic
            // that is really the treble ring makes the same bull read 1.589 times
            // further out and the recovered tilt is wrong while still looking plausible.
            //
            // It is a PRECONDITION and it is asserted rather than relied on (#1295).
            // #1423's identity is about the span STEP 1 measured, and what this stage
            // holds is the ellipse STEP 6 ray-traced -- two different measurements of
            // possibly two different rings. So the two are held against each other here:
            // the span times `boardRadiusOfSpan()` is the board's radius according to
            // #1423, and the conic's semi-major axis is the board's radius according to
            // the ray tracer. Agreement within #1423's own band is the precondition;
            // disagreement is said out loud, in both numbers, and is not quietly fixed.
            ring_identity::Sighting sighting;
            sighting.ring = static_cast<ring_identity::Ring>(calib.look.ring_measured);
            sighting.reach = calib.look.ring_reach_of_span;

            const double semiMajor = 0.5 * max(calib.ellipses.outerDoubleEllipse.size.width,
                                               calib.ellipses.outerDoubleEllipse.size.height);
            const double boardFromSpan = calib.look.board_span_px * sighting.boardRadiusOfSpan();
            const double band = ring_identity::Spec().band(); // 1.2605, and nothing chosen
            const double trebleOfDoubles = 1.0 / ring_identity::Spec().boardRadiusOfTrebleSpan();

            double conicOfDoubles = 1.0;
            if (boardFromSpan > 0.0 && semiMajor > 0.0)
            {
                const double ratio = semiMajor / boardFromSpan;
                if (ratio > 1.0 / band && ratio < band)
                {
                    conicOfDoubles = 1.0; // the ray tracer and #1423 agree: this is the doubles ring
                }
                else if (ratio > trebleOfDoubles / band && ratio < trebleOfDoubles * band)
                {
                    conicOfDoubles = ring_identity::Spec().boardRadiusOfTrebleSpan();
                    log_warning("Camera " + log_string(calib.camera_index + 1) +
                                " wire model: the fitted conic is " + log_string((int)semiMajor) +
                                " px where " + log_string((int)boardFromSpan) +
                                " px is this board's radius by its ring identity, so the ring that was "
                                "traced is the TREBLE ring; the plane is built at " +
                                log_string(ring_identity::Spec().boardRadiusOfTrebleSpan()) +
                                " of it rather than at its own radius.");
                }
                else
                {
                    log_warning("Camera " + log_string(calib.camera_index + 1) +
                                " wire model: the fitted conic is " + log_string((int)semiMajor) +
                                " px and this board's radius by its ring identity is " +
                                log_string((int)boardFromSpan) +
                                " px, which is neither the doubles ring nor the treble ring of the "
                                "other; the plane is built on the conic as traced and its tilt is "
                                "worth no more than that.");
                }
            }

            const wire_model::Plane plane =
                wire_model::planeOf(calib.ellipses.outerDoubleEllipse, Point2f(calib.bullCenter), conicOfDoubles);
            const wire_model::Fit fit = wire_model::fitTwentyFold(plane, candidates);

            result.fit_coherence = fit.coherence;
            result.fit_inlier_fraction = fit.inlierFraction;
            result.fit_trusted = fit.built && fit.coherence >= wire_model::minimumCoherence();

            if (!plane.built)
            {
                // The bull is not inside the ring it is supposed to be the centre of, or
                // the conic has no radius. There is no plane, so there is no ring, and
                // there is deliberately no fallback to counting: a ring nobody can place
                // is the plausible wrong answer this issue exists to refuse.
                log_warning("Camera " + log_string(calib.camera_index + 1) +
                            " wire model: no board plane could be built from a conic of " +
                            log_string((int)calib.ellipses.outerDoubleEllipse.size.width) + "x" +
                            log_string((int)calib.ellipses.outerDoubleEllipse.size.height) +
                            " px and a bull at (" + log_string(calib.bullCenter.x) + "," +
                            log_string(calib.bullCenter.y) + ").");
            }
            else if (result.fit_trusted)
            {
                const wire_model::Ring ring = wire_model::ringFrom(plane, fit, candidates, Point2f(calib.bullCenter));
                colorWires = ring.endpoints;
                result.fit_snapped = ring.snapped;
            }

            // Default level, not DEBUG: this is the fit-quality line #1458 observed does
            // not exist, and a quality number nobody prints is a quality number nobody
            // reads. #1321's rule on the sentence -- the numbers, against each other, in
            // one line.
            log_info("Camera " + log_string(calib.camera_index + 1) + " wire model: " +
                     log_string(fit.candidates) + " candidates, tilt " + log_string(plane.tilt) +
                     ", coherence R=" + log_string(fit.coherence) + " against a minimum of " +
                     log_string(wire_model::minimumCoherence()) + ", " +
                     log_string((int)(100.0 * fit.inlierFraction)) + "% of them within " +
                     log_string(wire_model::kResidualCutDeg) + " degrees of the ring, rms " +
                     log_string(fit.rmsResidualDeg) + " degrees; " +
                     (result.fit_trusted
                          ? log_string(result.fit_snapped) + " of the twenty boundaries were placed by a "
                                                             "candidate and the rest by the model"
                          : string("this fit is not trusted and no ring was generated from it")));
        }

        // #1317: a push, not an indexed copy. This loop used to run i over 0..19 and read
        // colorWires[i] with nothing looking at colorWires.size(), so a nine-wire board
        // read eleven Point2f past the end of the vector and the result carried them.
        // WireEndpoints::add stops at its own capacity, and the source is walked by the
        // source's own length, so neither side can be overrun by any count at all.
        result.wiresDetected = (int)colorWires.size();
        for (const Point2f &wire : colorWires)
        {
            if (!result.wireEndpoints.add(wire))
            {
                break;
            }
        }

        // The one threshold, read from the one place it is stated, asked of the count that
        // was FOUND. Two things were wrong with the spelling this replaces, a merge apart.
        // #1317 repaired the first: `result.wireEndpoints.size() == 20` over a
        // std::array<Point2f,20> was a tautology, with `// Allow some tolerance` beside it.
        // #1442 repairs the second: with a real `.size()` the question was still asked of
        // a store BOUNDED at twenty, which can read short and can never read long, so
        // twenty-two filled it to twenty and passed. `isAWholeRing` asks wiresDetected.
        result.isValid = result.readable();

        // #1317: what was found, not what the array can hold. On the rig that issue was
        // filed from, the line above this one said "Selected 9 averaged wires" and this
        // one said twenty.
        //
        // #1442: and when it says more than twenty it now says what became of the rest.
        // "keeping the first 20" was true and read as bookkeeping; the endpoints past the
        // twentieth are still dropped, but they are dropped from a reading this stage is
        // about to refuse, and the log should not be the only place that knows there were
        // twenty-two.
        log_debug("Found " + log_string(result.wiresDetected) + " wire boundaries using ensemble" +
                  (result.wiresDetected > kWiresRequired
                       ? ", which is more than a board has; the first " + log_string(kWiresRequired) +
                             " are kept for the picture and this camera is refused on the count"
                       : ""));
        if (result.isValid)
        {
            log_debug("Wire detection completed successfully");
        }
        else
        {
            // Not an ERROR here: calibrateSingleCamera is the one place that reports a
            // camera's failure, and it names the camera and this count. #1321's rule --
            // a reader told three times learns nothing the first telling did not say.
            log_debug("Wire detection did not complete: " + log_string(result.wiresDetected) +
                      (result.wiresDetected > kWiresRequired
                           ? " wire boundaries where a board has " + log_string(kWiresRequired)
                           : " of the " + log_string(kWiresRequired) + " wire boundaries a board has"));
        }

        // Handle debug output internally
        if (enableDebug)
        {
            log_debug("Saving debug images");

            // Create debug visualization using the FINAL PROCESSED wire endpoints
            Mat debug = frame.clone();

            // Draw detected wire endpoints - these are the FINAL results after sorting
            for (size_t i = 0; i < result.wireEndpoints.size(); i++)
            {
                Point2f wireEnd = result.wireEndpoints[i];

                // Draw wire line from center to endpoint
                line(debug, calib.bullCenter, wireEnd, Scalar(0, 0, 255), 2);

                // Draw endpoint circle
                circle(debug, wireEnd, 5, Scalar(0, 255, 255), -1);

                // Add wire index number (not segment number)
                putText(debug, to_string(i), Point(wireEnd.x + 10, wireEnd.y), FONT_HERSHEY_SIMPLEX, 0.6, Scalar(255, 255, 255), 2);
            }

            odfs::ensureDirectory("debug_frames/wire_processing");
            imwrite("debug_frames/wire_processing/wire_edges_" + to_string(calib.camera_index) + ".jpg", detectMetalWires(frame, colorMask, calib));
            imwrite("debug_frames/wire_processing/wire_result_" + to_string(calib.camera_index) + ".jpg", debug);
        }

        return result;
    }

} // namespace wire_processing