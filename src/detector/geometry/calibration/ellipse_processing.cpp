#include "ellipse_processing.hpp"
#include "logging.hpp"
#include "utils.hpp"
#include <cmath>

using namespace cv;
using namespace std;

namespace ellipse_processing
{
    // Rolling baseline ring width validation
    vector<Point> performDoubleRayTrace(const Mat &preprocessedMask, const Point &bullCenter, const EllipseParams &params,
                                        vector<Point> &allInnerPoints, vector<Point> &allOuterPoints, vector<bool> &rayValidFlags)
    {
        vector<double> ringWidths;
        allInnerPoints.clear();
        allOuterPoints.clear();
        rayValidFlags.clear();

        log_debug("Starting double ray trace from (" + log_string(bullCenter.x) + "," + log_string(bullCenter.y) + ")");

        // PHASE 1: Cast all rays and measure inner/outer boundaries + ring widths
        for (double angle = 0; angle < 360; angle += params.angleStepDegrees)
        {
            double radians = angle * CV_PI / 180.0;
            Point2f direction(cos(radians), sin(radians));

            // Find INNER boundary (first white pixel)
            Point innerBoundary = bullCenter;
            bool foundInner = false;

            for (int distance = params.innerRayStartDistance; distance < params.maxRayDistance; distance++)
            {
                Point checkPoint = bullCenter + Point(direction.x * distance, direction.y * distance);

                if (checkPoint.x < 0 || checkPoint.x >= preprocessedMask.cols ||
                    checkPoint.y < 0 || checkPoint.y >= preprocessedMask.rows)
                    break;

                if (preprocessedMask.at<uchar>(checkPoint) > 0)
                {
                    innerBoundary = checkPoint;
                    foundInner = true;
                    break;
                }
            }

            // Find OUTER boundary (last white pixel before sustained black)
            Point outerBoundary = bullCenter;
            bool foundOuter = false;

            if (foundInner)
            {
                Point lastWhitePixel = innerBoundary;
                int consecutiveBlackCount = 0;
                const int minConsecutiveBlack = 5;

                for (int distance = norm(innerBoundary - bullCenter); distance < params.maxRayDistance; distance++)
                {
                    Point checkPoint = bullCenter + Point(direction.x * distance, direction.y * distance);

                    if (checkPoint.x < 0 || checkPoint.x >= preprocessedMask.cols ||
                        checkPoint.y < 0 || checkPoint.y >= preprocessedMask.rows)
                    {
                        if (lastWhitePixel != innerBoundary)
                        {
                            outerBoundary = lastWhitePixel;
                            foundOuter = true;
                        }
                        break;
                    }

                    bool isWhite = (preprocessedMask.at<uchar>(checkPoint) > 0);

                    if (isWhite)
                    {
                        lastWhitePixel = checkPoint;
                        consecutiveBlackCount = 0;
                    }
                    else
                    {
                        consecutiveBlackCount++;
                        if (consecutiveBlackCount >= minConsecutiveBlack && lastWhitePixel != innerBoundary)
                        {
                            outerBoundary = lastWhitePixel;
                            foundOuter = true;
                            break;
                        }
                    }
                }
            }

            // Calculate ring width
            bool validRay = foundInner && foundOuter;
            double ringWidth = validRay ? norm(outerBoundary - innerBoundary) : -1;

            // Store all data
            allInnerPoints.push_back(innerBoundary);
            allOuterPoints.push_back(outerBoundary);
            rayValidFlags.push_back(validRay);

            if (validRay)
            {
                ringWidths.push_back(ringWidth);
            }
            else
            {
                ringWidths.push_back(-1);
            }
        }

        log_debug("Double ray trace completed. Found " + log_string(count(rayValidFlags.begin(), rayValidFlags.end(), true)) + " valid rays");

        // PHASE 2: Rolling baseline validation
        vector<Point> validatedOuterPoints;
        vector<bool> finalValidFlags(rayValidFlags.size(), false);

        if (count(rayValidFlags.begin(), rayValidFlags.end(), true) >= params.minValidRingMeasurements)
        {

            // Create rolling baseline of ring widths
            vector<double> rollingBaseline;
            vector<double> validRingWidths;

            // Collect all valid ring widths
            for (size_t i = 0; i < rayValidFlags.size(); i++)
            {
                if (rayValidFlags[i] && ringWidths[i] > 0)
                {
                    validRingWidths.push_back(ringWidths[i]);
                }
            }

            if (validRingWidths.size() < params.minValidRingMeasurements)
            {
                log_debug("Not enough valid ring widths for rolling baseline");
                return vector<Point>();
            }

            // Calculate global statistics for outlier detection
            sort(validRingWidths.begin(), validRingWidths.end());
            double globalMedian = validRingWidths[validRingWidths.size() / 2];

            // Calculate standard deviation
            double sum = 0;
            for (double width : validRingWidths)
            {
                sum += (width - globalMedian) * (width - globalMedian);
            }
            double stdDev = sqrt(sum / validRingWidths.size());

            log_debug("Global ring width stats - median: " + log_string(globalMedian) + ", stdDev: " + log_string(stdDev));

            // Rolling validation - go around the circle
            double rollingExpected = globalMedian; // Start with global median
            int validCount = 0;

            for (size_t i = 0; i < rayValidFlags.size(); i++)
            {
                if (!rayValidFlags[i] || ringWidths[i] <= 0)
                    continue;

                double currentWidth = ringWidths[i];
                double angle = i * params.angleStepDegrees;

                // Check against rolling expected value
                double deviation = abs(currentWidth - rollingExpected);
                double stdDeviation = abs(currentWidth - globalMedian) / stdDev;

                bool passesJumpTest = (deviation <= params.maxRingWidthJump);
                bool passesOutlierTest = (stdDeviation <= params.ringWidthOutlierThreshold);

                // temporary removed to not clutter the output
                // cout << "DEBUG: Ray " << angle << "° - width=" << currentWidth
                //      << ", expected=" << rollingExpected << ", deviation=" << deviation
                //      << ", stdDev=" << stdDeviation << " - ";

                if (passesJumpTest && passesOutlierTest)
                {
                    // ACCEPT this ray
                    validatedOuterPoints.push_back(allOuterPoints[i]);
                    finalValidFlags[i] = true;
                    validCount++;

                    // Update rolling expected (smooth transition)
                    double alpha = 0.3; // Smoothing factor
                    rollingExpected = alpha * currentWidth + (1.0 - alpha) * rollingExpected;

                    // temporary removed to not clutter the output
                    // cout << "ACCEPTED (newExpected=" << rollingExpected << ")" << endl;
                }
                else
                {
                    // temporary removed to not clutter the output
                    // cout << "REJECTED (jump=" << !passesJumpTest
                    //      << ", outlier=" << !passesOutlierTest << ")" << endl;
                }
            }

            // Update rayValidFlags to reflect final validation
            rayValidFlags = finalValidFlags;

            log_debug("Rolling baseline validation result: " + log_string(validatedOuterPoints.size()) + " validated rays from " + log_string(allOuterPoints.size()) + " total rays");

            return validatedOuterPoints;
        }

        log_debug("Insufficient ring width measurements, using all valid rays");
        vector<Point> allValidOuterPoints;
        for (size_t i = 0; i < rayValidFlags.size(); i++)
        {
            if (rayValidFlags[i])
            {
                allValidOuterPoints.push_back(allOuterPoints[i]);
            }
        }
        return allValidOuterPoints;
    }

    // Main function uses MaskBundle and proper terminology
    EllipseReport processEllipse(
        const Mat &originalFrame,
        const mask_processing::MaskBundle &masks,
        const Point &bullCenter,
        const Point &frameCenter,
        int camera_idx,
        bool debug_mode,
        const EllipseParams &params)
    {
        log_debug("Ellipse processing camera " + log_string(camera_idx) + " starting...");
        EllipseBoundaryData result;

        // #1321's reason, in #1330's place: a local, returned beside the geometry rather
        // than stored inside it, because the geometry is fwritten to the cache and a
        // std::string in there is a heap pointer on its way to a file.
        string doublesFailure;

        // SECTION 1: RAY TRACING for DOUBLES (most accurate method)
        if (masks.doublesMask.empty())
        {
            // #1321: the silent branch. Nothing was logged here at any level, and every
            // stage below refuses on the flag this leaves false.
            doublesFailure = "no doubles mask was produced from the red/green frame";
            log_debug("No doubles mask to ray trace");
        }
        else
        {
            log_debug("Processing doubles ring with ray tracing...");

            // No preprocessing needed - mask is already clean!
            int whitePixels = countNonZero(masks.doublesMask);
            log_debug("Doubles mask has " + log_string(whitePixels) + " white pixels");

            vector<Point> allInnerPoints, allOuterPoints;
            vector<bool> rayValidFlags;
            vector<Point> finalBoundaryPoints;

            if (whitePixels < params.minWhitePixelsThreshold)
            {
                // #1321: also silent until now. A dark or washed-out frame lands here,
                // and the count that decided it is the first thing a tester wants.
                doublesFailure = "the doubles mask holds " + to_string(whitePixels) +
                                 " white pixels and this stage needs at least " +
                                 to_string(params.minWhitePixelsThreshold);
                log_debug("Not enough white pixels for doubles: " + log_string(whitePixels));
            }
            else
            {
                // Perform ray tracing on clean doubles mask
                finalBoundaryPoints = performDoubleRayTrace(masks.doublesMask, bullCenter, params,
                                                            allInnerPoints, allOuterPoints, rayValidFlags);

                if (finalBoundaryPoints.size() >= params.minValidRays)
                {
                    try
                    {
                        // Fit ellipse to validated outer boundary points
                        result.outerDoubleEllipse = fitEllipse(finalBoundaryPoints);
                        result.validOuterPoints = finalBoundaryPoints.size();
                        log_debug("SUCCESS - Fitted outer double ellipse from " + log_string(result.validOuterPoints) + " boundary points");

                        // Fit ellipse to inner boundary points (validated ones only)
                        vector<Point> validatedInnerPoints;
                        set<pair<int, int>> acceptedOuterPoints;
                        for (const Point &pt : finalBoundaryPoints)
                        {
                            acceptedOuterPoints.insert({pt.x, pt.y});
                        }

                        for (size_t i = 0; i < allOuterPoints.size(); i++)
                        {
                            if (acceptedOuterPoints.count({allOuterPoints[i].x, allOuterPoints[i].y}) > 0 &&
                                i < allInnerPoints.size())
                            {
                                validatedInnerPoints.push_back(allInnerPoints[i]);
                            }
                        }

                        if (validatedInnerPoints.size() >= 5)
                        {
                            result.innerDoubleEllipse = fitEllipse(validatedInnerPoints);
                            result.validInnerPoints = validatedInnerPoints.size();
                            log_debug("SUCCESS - Fitted inner double ellipse from " + log_string(result.validInnerPoints) + " inner points");
                        }
                        else
                        {
                            // Fallback: scale outer ellipse for inner boundary
                            result.innerDoubleEllipse = result.outerDoubleEllipse;
                            result.innerDoubleEllipse.size.width *= 0.92;
                            result.innerDoubleEllipse.size.height *= 0.92;
                            result.validInnerPoints = 0;
                            log_debug("FALLBACK - Calculated inner double ellipse from outer");
                        }

                        result.hasValidDoubles = true;
                    }
                    catch (const cv::Exception &e)
                    {
                        log_debug("Double ellipse fitting failed: " + string(e.what()));
                        doublesFailure = "fitting the doubles ellipse from " +
                                         to_string(finalBoundaryPoints.size()) +
                                         " boundary points threw: " + string(e.what());
                        result.hasValidDoubles = false;
                    }
                }
                else
                {
                    log_debug("Not enough boundary points for doubles: " + log_string(finalBoundaryPoints.size()));
                    doublesFailure = to_string(allOuterPoints.size()) + " rays traced, " +
                                     to_string(finalBoundaryPoints.size()) +
                                     " gave a boundary point, and at least " +
                                     to_string(params.minValidRays) +
                                     " are needed to fit the doubles ellipse (doubles mask " +
                                     to_string(whitePixels) + " white pixels)";
                    result.hasValidDoubles = false;
                }
            }
        }

        // SECTION 2: RAY TRACING for TRIPLES -- the same trace the doubles ring gets.
        //
        // #1499: this was contour fitting -- fitEllipse over the largest contour for the
        // outer edge, and over any contour at least 10% smaller for the inner -- and what
        // it fitted was not the edge it was named after. A treble mask whose annulus is
        // broken anywhere (a wire gap the closing did not bridge, a dart) is ONE C-shaped
        // contour holding BOTH edges, fitEllipse lands in the MIDDLE of the band, and
        // there is no second contour so no inner edge exists at all. Measured on
        // mocks/rig-20260922, the deployment hardware: all three cameras fitted the
        // "outer" treble at 0.5766/0.5814/0.5791 of the board -- the band's own middle,
        // (99+107)/2/170 = 0.6059 in millimetres, read through the few percent the traced
        // reference overstates (see holdRingsToTheBoard) -- and the band hold refused
        // every one, correctly, as not being the outer edge. With the outer treble zeroed
        // on every camera a treble is unpublishable by construction: scorePoint's
        // in_outer_triple is false for every dart, which is #1499's title.
        //
        // The ray trace does not have that failure: first white pixel out of the bull is
        // the inner edge, last white before sustained black is the outer, per ray, and a
        // break in the annulus only costs the rays that cross it. Measured against the
        // frame's own paint (testers/i1499_band_census.cpp, morphology-free HSV, medians
        // over ~108 rays): on mocks/rig-20260918 the traced treble edges sit on the
        // painted band within 0.02 of the board span on every camera, and the paint
        // band's midpoint lies inside the traced band on 108 of 108 rays, all three
        // cameras.
        if (!masks.triplesMask.empty())
        {
            log_debug("Processing triples ring with ray tracing...");

            vector<Point> tAllInner, tAllOuter;
            vector<bool> tFlags;
            vector<Point> tBoundary = performDoubleRayTrace(masks.triplesMask, bullCenter, params,
                                                            tAllInner, tAllOuter, tFlags);
            if (tBoundary.size() >= params.minValidRays)
            {
                try
                {
                    result.outerTripleEllipse = fitEllipse(tBoundary);
                    log_debug("SUCCESS - Fitted outer triple ellipse from " + log_string(tBoundary.size()) + " ray boundary points");

                    // The validated rays' inner points, the way the doubles fit keeps
                    // only the inner points whose outer point survived validation.
                    set<pair<int, int>> acceptedTripleOuter;
                    for (const Point &pt : tBoundary)
                    {
                        acceptedTripleOuter.insert({pt.x, pt.y});
                    }
                    vector<Point> validatedTripleInner;
                    for (size_t i = 0; i < tAllOuter.size(); i++)
                    {
                        if (acceptedTripleOuter.count({tAllOuter[i].x, tAllOuter[i].y}) > 0 &&
                            i < tAllInner.size())
                        {
                            validatedTripleInner.push_back(tAllInner[i]);
                        }
                    }
                    if (validatedTripleInner.size() >= 5)
                    {
                        result.innerTripleEllipse = fitEllipse(validatedTripleInner);
                        log_debug("SUCCESS - Fitted inner triple ellipse from " + log_string(validatedTripleInner.size()) + " inner points");
                    }
                    else
                    {
                        log_debug("FAILED - Not enough validated inner points for the inner triple edge");
                    }

                    result.hasValidTriples = (result.outerTripleEllipse.size.area() > 0 && result.innerTripleEllipse.size.area() > 0);
                }
                catch (const cv::Exception &e)
                {
                    log_debug("FAIL - Triple ellipse fitting failed: " + string(e.what()));
                    result.hasValidTriples = false;
                }
            }
            else
            {
                log_debug("Not enough boundary points for triples: " + log_string(tBoundary.size()));
                result.hasValidTriples = false;
            }

            // A treble ring broken into a C has one contour and no hole: there is no inner
            // contour to fit, and one ellipse through both edges of the C lands between
            // them. The maintainer's rig on 2026-09-22, camera 1: that fit read 0.575 of the
            // board, between the ring's real 0.537 and 0.611, the band check refused it,
            // and the camera could only ever call a treble a single. So where the contours
            // do not give both edges, the ring is traced the way the doubles ring is --
            // rays from the bull, which do not care where the ring is broken.
            if (!result.hasValidTriples)
            {
                vector<Point> rayInner, rayOuter;
                vector<bool> rayValid;
                const vector<Point> outerPoints =
                    performDoubleRayTrace(masks.triplesMask, bullCenter, params, rayInner, rayOuter, rayValid);
                if (outerPoints.size() >= params.minValidRays)
                {
                    set<pair<int, int>> accepted;
                    for (const Point &pt : outerPoints)
                        accepted.insert({pt.x, pt.y});
                    vector<Point> innerPoints;
                    for (size_t i = 0; i < rayOuter.size() && i < rayInner.size(); i++)
                    {
                        if (accepted.count({rayOuter[i].x, rayOuter[i].y}) > 0)
                            innerPoints.push_back(rayInner[i]);
                    }
                    if (innerPoints.size() >= 5)
                    {
                        try
                        {
                            result.outerTripleEllipse = fitEllipse(outerPoints);
                            result.innerTripleEllipse = fitEllipse(innerPoints);
                            result.hasValidTriples = true;
                            log_debug("SUCCESS - Fitted triple ellipses by ray trace from " +
                                      log_string(outerPoints.size()) + " outer and " +
                                      log_string(innerPoints.size()) + " inner points");
                        }
                        catch (const cv::Exception &e)
                        {
                            log_debug("FAIL - Triple ray-trace fit failed: " + string(e.what()));
                        }
                    }
                }
                else
                {
                    log_debug("Triple ray trace gave " + log_string(outerPoints.size()) + " points, " +
                              log_string(params.minValidRays) + " needed");
                }
            }
        }

        // SECTION 3: CONTOUR FITTING for BULL RINGS (simple method)
        if (!masks.outerBullMask.empty())
        {
            log_debug("Processing outer bull (25-point) with contour fitting...");

            vector<vector<Point>> outerBullContours;
            findContours(masks.outerBullMask, outerBullContours, RETR_EXTERNAL, CHAIN_APPROX_SIMPLE);

            if (!outerBullContours.empty())
            {
                try
                {
                    // Sort by area and fit to largest contour
                    sort(outerBullContours.begin(), outerBullContours.end(),
                         [](const vector<Point> &a, const vector<Point> &b)
                         {
                             return contourArea(a) > contourArea(b);
                         });

                    if (outerBullContours[0].size() >= 5)
                    {
                        result.outerBullEllipse = fitEllipse(outerBullContours[0]);
                        log_debug("SUCCESS - Fitted outer bull ellipse from contour");
                    }
                }
                catch (const cv::Exception &e)
                {
                    log_debug("Outer bull ellipse fitting failed: " + string(e.what()));
                }
            }
        }

        // SECTION 3.1: CONTOUR FITTING for INNER BULL RING (50-point)
        if (!masks.bullMask.empty())
        {
            log_debug("Processing bullseye (50-point) with contour fitting...");

            vector<vector<Point>> bullContours;
            findContours(masks.bullMask, bullContours, RETR_EXTERNAL, CHAIN_APPROX_SIMPLE);

            if (!bullContours.empty())
            {
                try
                {
                    // Sort by area and fit to largest contour
                    sort(bullContours.begin(), bullContours.end(),
                         [](const vector<Point> &a, const vector<Point> &b)
                         {
                             return contourArea(a) > contourArea(b);
                         });

                    if (bullContours[0].size() >= 5)
                    {
                        result.innerBullEllipse = fitEllipse(bullContours[0]);
                        log_debug("SUCCESS - Fitted bullseye ellipse from contour");
                    }
                }
                catch (const cv::Exception &e)
                {
                    log_debug("Bullseye ellipse fitting failed: " + string(e.what()));
                }
            }
        }

        // Set bull validation flag
        result.hasValidBulls = (result.outerBullEllipse.size.area() > 0 || result.innerBullEllipse.size.area() > 0);

        // SECTION 3.2: #1485 -- WHICH RING IS THAT? Asked of the five fitted rings, once,
        // here: this is the last statement that can still say which contour became which
        // ring, and `score_processing::scorePoint` is the first reader that cannot. The
        // rule, the bands and why a refused ring is zeroed rather than corrected are in
        // ellipse_processing.hpp beside the millimetres they come out of.
        //
        // At log_info and not log_debug, deliberately. The state this repairs was silent:
        // an outer bull ellipse at 0.97 of the board drew a scoring page, calibrated three
        // of three, reported itself whole and published every dart on the board as a 25,
        // with nothing anywhere saying which contour it had measured.
        log_info("Camera " + log_string(camera_idx + 1) + " rings: " +
                 log_string_src(holdRingsToTheBoard(result)));

        // SECTION 4: PERSPECTIVE ANALYSIS (using doubles as reference)
        if (result.hasValidDoubles)
        {
            Point ellipseCenter(result.outerDoubleEllipse.center);
            result.offsetX = ellipseCenter.x - bullCenter.x;
            result.offsetY = ellipseCenter.y - bullCenter.y;
            result.offsetMagnitude = norm(Point2f(result.offsetX, result.offsetY));
            result.offsetAngle = atan2(result.offsetY, result.offsetX) * 180.0 / CV_PI;

            log_debug("Perspective offset - X=" + log_string(result.offsetX) + ", Y=" + log_string(result.offsetY) + ", magnitude=" + log_string(result.offsetMagnitude) + "px");
        }

        // Set hasDetectedEllipses flag
        result.hasDetectedEllipses = (result.hasValidDoubles && result.hasValidTriples && result.hasValidBulls);

        // SECTION 5: DEBUG VISUALIZATION
        if (debug_mode)
        {
            Mat ellipseVis = originalFrame.clone();

            // Draw all detected ellipses with different colors
            if (result.hasValidDoubles)
            {
                ellipse(ellipseVis, result.outerDoubleEllipse, Scalar(255, 0, 255), 3);
                ellipse(ellipseVis, result.innerDoubleEllipse, Scalar(255, 255, 0), 2);
            }

            if (result.hasValidTriples)
            {
                ellipse(ellipseVis, result.outerTripleEllipse, Scalar(255, 0, 255), 2);
                ellipse(ellipseVis, result.innerTripleEllipse, Scalar(255, 255, 0), 2);
            }

            if (result.hasValidBulls)
            {
                ellipse(ellipseVis, result.outerBullEllipse, Scalar(255, 0, 255), 2);
                ellipse(ellipseVis, result.innerBullEllipse, Scalar(255, 255, 0), 2);
            }

            // // Draw bull center
            // circle(ellipseVis, bullCenter, 8, Scalar(0, 0, 0), -1);
            // circle(ellipseVis, bullCenter, 10, Scalar(255, 255, 255), 2);

            odfs::ensureDirectory("debug_frames/ellipse_processing");
            imwrite("debug_frames/ellipse_processing/ellipse_result_" + to_string(camera_idx) + ".jpg", ellipseVis);

            log_debug("Saved ellipse visualization with all ring types");
        }

        return EllipseReport{result, doublesFailure};
    }

} // namespace ellipse_processing