#include "bull_processing.hpp"
#include "logging.hpp"
#include "color_processing.hpp"
#include "utils.hpp"
#include <cmath>
#include <iomanip>
#include <sstream>

using namespace cv;
using namespace std;

namespace bull_processing
{
    namespace
    {
        /** A number a reader can compare, rather than to_string's six decimals. */
        string decimals(double value, int places)
        {
            ostringstream out;
            out << fixed << setprecision(places) << value;
            return out.str();
        }

        /** The radius a filled region of this area would have if it were a disc. */
        double discRadius(double area)
        {
            return sqrt(max(0.0, area) / CV_PI);
        }

        /** Why a candidate is not the bull. Empty means it is still in the running. */
        struct Refusal
        {
            string reason;
            bool refused() const { return !reason.empty(); }
        };
    }

    namespace
    {
        // ---- #1320/#1340 the board, measured: the one place it is done -------------------
        //
        // The largest outermost region in the red/green mask is the dartboard: its
        // boundary is the outside of the doubles ring, because that is the last coloured
        // thing on a board. WHICH region that is has not moved -- it is still the one
        // enclosing the most area. What is measured of it is #1340's: the board's radius
        // and its middle come from the smallest circle enclosing that boundary rather
        // than from the area the boundary encloses, because a doubles ring broken into
        // arcs -- the ordinary case on a board lit from one side -- loses most of its
        // enclosed area and none of its extent. BullParams carries the measurements.
        //
        // The area is still taken, because it is what the refusal used to be about and
        // what every log line before this one quoted, and the two are logged side by
        // side: on a ring that closed they agree, and where they part the difference is
        // the arcs the mask lost.
        //
        // #1331 took the contours as a parameter so that processBull, which has already
        // found them to score candidates with, does not find them twice.
        BoardSighting measureBoardFrom(const vector<vector<Point>> &contours,
                                       const vector<Vec4i> &hierarchy,
                                       const Point &frameCenter,
                                       const BullParams &params)
        {
            BoardSighting board;
            board.center = frameCenter;

            if (contours.empty())
            {
                // The count is in the sentence for #1321's reason: a line that carries no
                // number is a line nothing can contradict.
                board.failure = "the red/green frame yields 0 contours, and at least 1 region is "
                                "needed before there is a board to measure or a candidate to "
                                "measure against it";
                return board;
            }

            int boardIndex = -1;
            double boardArea = 0.0;
            for (size_t i = 0; i < contours.size(); i++)
            {
                if (hierarchy[i][3] != -1) // not an outermost contour
                    continue;
                const double area = contourArea(contours[i]);
                if (area > boardArea)
                {
                    boardArea = area;
                    boardIndex = static_cast<int>(i);
                }
            }

            Point2f boardSpanCenter(static_cast<float>(frameCenter.x), static_cast<float>(frameCenter.y));
            float boardSpan = 0.0f;
            if (boardIndex >= 0)
            {
                minEnclosingCircle(contours[boardIndex], boardSpanCenter, boardSpan);
            }

            board.area = boardArea;
            const double minBoardRadius = params.minBoardRadius();
            const double smallestBullOfBoard = params.bullRadiusOfBoardRadius * params.minBullRadiusFactor;
            if (boardIndex < 0 || boardSpan < minBoardRadius)
            {
                board.failure = "the board cannot be measured: the largest red/green region spans " +
                                decimals(boardSpan, 1) + " px of radius, enclosing " +
                                to_string(static_cast<long>(boardArea)) + " pixels, and a board this stage can " +
                                "size a bull against has to span at least " + decimals(minBoardRadius, 1) +
                                " px -- the radius at which the smallest bull it would accept, " +
                                decimals(smallestBullOfBoard, 3) + " of the board, is still " +
                                decimals(params.smallestMeasurableBullRadius, 1) +
                                " px and so survives this stage's own 7x7 blur";
                return board;
            }

            board.found = true;
            board.radius = boardSpan;
            board.center = Point(cvRound(boardSpanCenter.x), cvRound(boardSpanCenter.y));
            return board;
        }
    }

    BoardSighting measureBoard(const Mat &redGreenFrame, const Point &frameCenter, const BullParams &params)
    {
        // The same first three steps processBull takes, because the board has to be told
        // from the same picture the bull is: the blur closes the pinholes in a printed
        // ring, and the threshold is what turns a colour frame into regions at all.
        Mat blurredFrame;
        GaussianBlur(redGreenFrame, blurredFrame, Size(7, 7), 2.0);

        Mat grayMask;
        cvtColor(blurredFrame, grayMask, COLOR_BGR2GRAY);
        Mat binaryMask;
        threshold(grayMask, binaryMask, 1, 255, THRESH_BINARY);

        vector<vector<Point>> contours;
        vector<Vec4i> hierarchy;
        findContours(binaryMask, contours, hierarchy, RETR_TREE, CHAIN_APPROX_SIMPLE);

        return measureBoardFrom(contours, hierarchy, frameCenter, params);
    }

    BullSighting processBull(const Mat &redGreenFrame, const Point &frameCenter, int camera_idx, bool debug_mode, const BullParams &params)
    {
        log_debug("Bull detection camera " + log_string(camera_idx) + " starting...");

        BullSighting sighting;
        sighting.center = frameCenter; // where a debug overlay draws until something is found

        // Step 1: Create blur mask
        Mat blurredFrame;
        GaussianBlur(redGreenFrame, blurredFrame, Size(7, 7), 2.0);

        // Step 2: Convert to black/white - anything not black becomes white
        Mat grayMask;
        cvtColor(blurredFrame, grayMask, COLOR_BGR2GRAY);
        Mat binaryMask;
        threshold(grayMask, binaryMask, 1, 255, THRESH_BINARY); // Any non-zero pixel becomes white

        // Debug: Save masks
        if (debug_mode)
        {
            odfs::ensureDirectory("debug_frames/bull_processing");
            imwrite("debug_frames/bull_processing/blurred_frame_" + to_string(camera_idx) + ".jpg", blurredFrame);
            imwrite("debug_frames/bull_processing/binary_mask_" + to_string(camera_idx) + ".jpg", binaryMask);
        }

        // Step 3: Find contours
        vector<vector<Point>> contours;
        vector<Vec4i> hierarchy;
        findContours(binaryMask, contours, hierarchy, RETR_TREE, CHAIN_APPROX_SIMPLE);

        log_debug("Found " + log_string(contours.size()) + " contours total");

        // ---- Step 3.5: the board, before anything is scored against it ------------------
        //
        // #1331 moved the measurement itself into measureBoard() above, unchanged, so
        // that calibration can make it one stage earlier -- on the FULL frame, where
        // nothing has yet had a chance to cut a board's edge off. This call is the same
        // measurement on the frame that came back out of the region calibration then drew
        // around what it found, so a board that reaches this stage is a whole one.
        const BoardSighting board = measureBoardFrom(contours, hierarchy, frameCenter, params);
        const double boardArea = board.area;
        if (!board.found)
        {
            sighting.failure = board.failure;
            return sighting;
        }

        sighting.boardRadius = board.radius;
        sighting.boardCenter = board.center;

        const double idealRadius = sighting.boardRadius * params.bullRadiusOfBoardRadius;
        const double minRadius = idealRadius * params.minBullRadiusFactor;
        const double maxRadius = idealRadius * params.maxBullRadiusFactor;
        const double maxOffset = sighting.boardRadius * params.maxOffsetOfBoardRadius;

        // Both radii, on purpose. The span is what everything below is sized against;
        // the disc radius is what #1320 sized against and what the old refusal was a
        // floor on. A ring that closed makes them agree to within the eccentricity of
        // the ellipse the board projects to; a gap between them is the arcs this mask
        // lost, and is the difference between a camera that calibrates under #1340 and
        // one that did not before it.
        log_debug("Board measured from the red/green mask: radius " + log_string((int)sighting.boardRadius) +
                  " px across its widest, centre (" + log_string(sighting.boardCenter.x) + "," +
                  log_string(sighting.boardCenter.y) + "); its boundary encloses " +
                  log_string((int)boardArea) + " px, which a filled disc would carry at radius " +
                  log_string_src(decimals(discRadius(boardArea), 1)) + " px; a bull here is " +
                  log_string_src(decimals(minRadius, 1)) + " to " +
                  log_string_src(decimals(maxRadius, 1)) + " px in radius and within " +
                  log_string_src(decimals(maxOffset, 1)) + " px of that centre");

        // Step 4: score every candidate against the board, and refuse the ones that
        // cannot be a bull on it however round they are.
        double bestScore = 0;
        int bestContourIndex = -1;
        double bestCircularity = 0, bestRadius = 0, bestOffset = 0;
        int refusedOnSize = 0, refusedOnPosition = 0, refusedOnShape = 0;
        vector<Refusal> refusals(contours.size());

        // The roundest thing that was refused, so a camera that finds nothing can say
        // what came closest and on what it lost -- which is the line #1320 was filed
        // over: a 205-pixel speck at C=0.71 winning because nothing asked anything else.
        double roundestRefusedCircularity = -1.0;
        string roundestRefusal;

        for (size_t i = 0; i < contours.size(); i++)
        {
            const auto &contour = contours[i];
            const double area = contourArea(contour);
            const double perimeter = arcLength(contour, true);
            if (perimeter <= 0 || area <= 0)
                continue;

            const double circularity = (4 * CV_PI * area) / (perimeter * perimeter);

            Point2f center2f;
            float enclosingRadius;
            minEnclosingCircle(contour, center2f, enclosingRadius);
            const Point center(center2f);

            // The radius is taken from the area rather than from the enclosing circle:
            // a bull is a filled blob, and an enclosing circle is decided by whichever
            // pixel is furthest out.
            const double radius = discRadius(area);
            const double offset = norm(Point2f(center) - Point2f(sighting.boardCenter));

            Refusal refusal;
            if (radius < minRadius || radius > maxRadius)
            {
                refusal.reason = "size: it is " + decimals(radius, 1) + " px across the radius, " +
                                 decimals(radius / sighting.boardRadius, 4) + " of the board radius " +
                                 decimals(sighting.boardRadius, 1) + " px, and a bull on this board is " +
                                 decimals(minRadius, 1) + " to " + decimals(maxRadius, 1) + " px";
                refusedOnSize++;
            }
            else if (offset > maxOffset)
            {
                refusal.reason = "position: it sits " + decimals(offset, 1) + " px from the middle of the board at (" +
                                 to_string(sighting.boardCenter.x) + "," + to_string(sighting.boardCenter.y) +
                                 "), and a bull on this board is within " + decimals(maxOffset, 1) + " px";
                refusedOnPosition++;
            }
            else if (circularity < params.minCircularity)
            {
                refusal.reason = "shape: circularity " + decimals(circularity, 2) + " and a bull is at least " +
                                 decimals(params.minCircularity, 2);
                refusedOnShape++;
            }
            refusals[i] = refusal;

            // How well the radius matches a bull's, on a log scale so that half and
            // double the ideal radius score the same, and nothing.
            const double sizeFit = 1.0 - min(1.0, fabs(log(radius / idealRadius)) / log(2.0));
            const double centrality = 1.0 - min(1.0, offset / maxOffset);
            const double score = circularity * params.circularityWeight +
                                 sizeFit * params.sizeWeight +
                                 centrality * params.centralityWeight;

            if (area >= params.debugAreaFloor)
            {
                log_debug("Contour " + log_string(i) +
                          ": area=" + log_string((int)area) +
                          ", radius=" + log_string_src(decimals(radius, 1)) +
                          ", circ=" + log_string_src(decimals(circularity, 2)) +
                          ", centre=(" + log_string(center.x) + "," + log_string(center.y) + ")" +
                          ", offset=" + log_string_src(decimals(offset, 1)) +
                          (refusal.refused()
                               ? ", REFUSED on " + log_string_src(refusal.reason)
                               : ", score=" + log_string_src(decimals(score, 3))));
            }

            if (refusal.refused())
            {
                if (circularity > roundestRefusedCircularity)
                {
                    roundestRefusedCircularity = circularity;
                    roundestRefusal = "the roundest thing refused was " + to_string((long)area) +
                                      " pixels at (" + to_string(center.x) + "," + to_string(center.y) +
                                      ") with circularity " + decimals(circularity, 2) +
                                      ", refused on " + refusal.reason;
                }
                continue;
            }

            if (score > bestScore)
            {
                bestScore = score;
                bestContourIndex = static_cast<int>(i);
                bestCircularity = circularity;
                bestRadius = radius;
                bestOffset = offset;
                sighting.center = center;
            }
        }

        const int refused = refusedOnSize + refusedOnPosition + refusedOnShape;

        if (bestContourIndex == -1)
        {
            sighting.center = frameCenter;
            const string scored = to_string(contours.size()) +
                                  (contours.size() == 1 ? string(" region was") : string(" regions were"));
            const string turnedDown = to_string(refused) +
                                      (refused == 1 ? string(" was refused") : string(" were refused"));
            sighting.failure = "no candidate on this board can be a bull: " + scored +
                               " scored against a board of radius " + decimals(sighting.boardRadius, 1) +
                               " px and " + turnedDown + " (" + to_string(refusedOnSize) + " on size, " +
                               to_string(refusedOnPosition) + " on position, " + to_string(refusedOnShape) +
                               " on shape)" +
                               (roundestRefusal.empty() ? string() : string("; ") + roundestRefusal);
            return sighting;
        }

        sighting.found = true;
        sighting.score = bestScore;
        sighting.radius = bestRadius;
        sighting.basis = "circularity " + decimals(bestCircularity, 2) + ", radius " + decimals(bestRadius, 1) +
                         " px (" + decimals(bestRadius / sighting.boardRadius, 3) + " of the board radius " +
                         decimals(sighting.boardRadius, 1) + " px, a bull is " +
                         decimals(params.bullRadiusOfBoardRadius, 3) + ") and " + decimals(bestOffset, 1) +
                         " px off the middle of the board; " + to_string(refused) + " of " +
                         to_string(contours.size()) + " refused (" + to_string(refusedOnSize) + " on size, " +
                         to_string(refusedOnPosition) + " on position, " + to_string(refusedOnShape) + " on shape)";

        // Enhanced debug visualization
        if (debug_mode)
        {
            Mat bullDebug = redGreenFrame.clone();

            // Dim the background to 20% to make outlines pop
            bullDebug = bullDebug * 0.2;

            // Collect contour info for top display
            vector<string> contourInfo;
            vector<Scalar> contourColors;
            int validContourCount = 0;

            // Draw ALL contours with new color scheme and collect info
            for (size_t i = 0; i < contours.size(); i++)
            {
                double area = contourArea(contours[i]);
                if (area < params.debugAreaFloor)
                    continue; // Skip tiny ones in visualization

                double perimeter = arcLength(contours[i], true);
                double circularity = (perimeter > 0) ? (4 * CV_PI * area) / (perimeter * perimeter) : 0;

                Point2f center2f;
                float radius;
                minEnclosingCircle(contours[i], center2f, radius);

                // #1320: the picture says what the decision says. It used to label a
                // contour INNER or OUTER, which is a fact about the hierarchy and had
                // nothing to do with why the thing won.
                Scalar color;
                string status;
                if (static_cast<int>(i) == bestContourIndex)
                {
                    color = Scalar(255, 255, 0); // Bright CYAN for best
                    status = "BEST";
                }
                else if (refusals[i].refused())
                {
                    color = Scalar(255, 255, 255); // WHITE for refused
                    status = "REFUSED " + refusals[i].reason.substr(0, refusals[i].reason.find(':'));
                }
                else
                {
                    color = Scalar(255, 0, 255); // MAGENTA for a candidate that lost on score
                    status = "PASSED";
                }

                drawContours(bullDebug, contours, i, color, 3);

                // Find leftmost point of the contour
                Point leftmostPoint = contours[i][0];
                for (const Point &pt : contours[i])
                {
                    if (pt.x < leftmostPoint.x)
                    {
                        leftmostPoint = pt;
                    }
                }

                // Small number overlay at leftmost point (white text with black outline)
                string numberLabel = to_string(validContourCount);
                Point labelPos = leftmostPoint + Point(-15, 5); // Slightly left of leftmost point

                putText(bullDebug, numberLabel, labelPos,
                        FONT_HERSHEY_SIMPLEX, 0.7, Scalar(0, 0, 0), 3); // Black outline
                putText(bullDebug, numberLabel, labelPos,
                        FONT_HERSHEY_SIMPLEX, 0.7, Scalar(255, 255, 255), 2); // White text

                // Collect info for top display with color
                string info = "Contour " + to_string(validContourCount) + ": A=" + to_string(int(area)) +
                              " R=" + decimals(discRadius(area), 1) +
                              " C=" + decimals(circularity, 2) + " (" + status + ")";
                contourInfo.push_back(info);
                contourColors.push_back(color);

                validContourCount++;
            }

            // Draw the board this was all measured against, and the bull on it
            circle(bullDebug, sighting.boardCenter, static_cast<int>(sighting.boardRadius), Scalar(0, 200, 200), 1);
            circle(bullDebug, sighting.boardCenter, static_cast<int>(maxOffset), Scalar(0, 120, 200), 1);
            circle(bullDebug, sighting.center, 4, Scalar(255, 255, 255), -1);

            // Display contour info at top with colored backgrounds
            int yPos = 30;
            for (size_t i = 0; i < contourInfo.size(); i++)
            {
                const string &info = contourInfo[i];
                Scalar bgColor = contourColors[i];

                // Get text size for background rectangle
                Size textSize = getTextSize(info, FONT_HERSHEY_SIMPLEX, 0.5, 1, nullptr);

                // Draw colored background (dimmed version of contour color)
                Scalar dimmedColor = bgColor * 0.3; // 30% of original color
                rectangle(bullDebug, Point(10, yPos - 20), Point(15 + textSize.width, yPos + 5),
                          dimmedColor, -1);

                // Draw text in white for readability
                putText(bullDebug, info, Point(15, yPos),
                        FONT_HERSHEY_SIMPLEX, 0.5, Scalar(255, 255, 255), 1);
                yPos += 25;
            }

            imwrite("debug_frames/bull_processing/bull_detection_" + to_string(camera_idx) + ".jpg", bullDebug);
        }

        return sighting;
    }

} // namespace bull_processing
