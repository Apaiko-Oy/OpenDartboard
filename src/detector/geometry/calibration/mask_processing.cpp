#include "mask_processing.hpp"
#include "logging.hpp"
#include <cstdlib>
#include <iostream>
#include <sstream>

using namespace cv;
using namespace std;

namespace mask_processing
{
    namespace
    {
        /**
         * #1393's falsification, in the shape OD_ROI, OD_ROI_MARGIN and OD_BOARD
         * established: one binary, the carve chosen at run time, so "different build" is
         * never a confound in a before/after.
         *
         * OD_BULL_CARVE=frame restores exactly what this stage did before #1393 --
         * `min(cols, rows) / 15`, a fixed 48 px at 720p whatever board is in the picture
         * -- and is how the over-carve can be put back on the very binary that no longer
         * makes it, so the doubles and triples the ellipse fit is handed can be measured
         * both ways on one footage.
         *
         * Anything other than the exact word `frame` is ignored rather than obeyed, which
         * is OD_BOARD's rule: a half-understood spelling must not quietly select a carve
         * nobody asked for.
         */
        bool carvedAgainstTheFrame()
        {
            static bool v = []
            {
                const char *e = std::getenv("OD_BULL_CARVE");
                return e && string(e) == "frame";
            }();
            return v;
        }

        /** A number a reader can compare, rather than to_string's six decimals. */
        string decimals(double value, int places)
        {
            ostringstream out;
            out.precision(places);
            out << fixed << value;
            return out.str();
        }

        /** What the frame rule gave before #1393: a fixed 48 px at 720p. */
        int frameCarveRadius(const Mat &redGreenFrame)
        {
            return max(1, min(redGreenFrame.cols, redGreenFrame.rows) / 15);
        }

        int bullCarveRadius(const Mat &redGreenFrame, double boardRadius, const MaskParams &params)
        {
            if (carvedAgainstTheFrame() || boardRadius <= 0.0)
            {
                return frameCarveRadius(redGreenFrame);
            }
            return max(1, (int)lround(boardRadius * params.bullCarveOfBoardRadius));
        }
    }

    // Moved from ellipse_processing - preprocess mask to remove artifacts
    Mat preprocessMask(const Mat &inputMask, const MaskParams &params)
    {
        Mat cleanedMask = inputMask.clone();

        // Step 1: Closing operation - fill gaps and holes
        Mat closeKernel = getStructuringElement(MORPH_ELLIPSE,
                                                Size(params.maskCloseKernelSize, params.maskCloseKernelSize));
        morphologyEx(cleanedMask, cleanedMask, MORPH_CLOSE, closeKernel);

        // Step 2: Opening operation - remove small noise pixels
        Mat openKernel = getStructuringElement(MORPH_ELLIPSE,
                                               Size(params.maskOpenKernelSize, params.maskOpenKernelSize));
        morphologyEx(cleanedMask, cleanedMask, MORPH_OPEN, openKernel);

        // Step 3: Slight dilation - smooth boundaries for better ray casting
        Mat dilateKernel = getStructuringElement(MORPH_ELLIPSE,
                                                 Size(params.maskDilateKernelSize, params.maskDilateKernelSize));
        dilate(cleanedMask, cleanedMask, dilateKernel);

        // Step 4: Keep only the largest connected component
        Mat labels, stats, centroids;
        int nLabels = connectedComponentsWithStats(cleanedMask, labels, stats, centroids);

        if (nLabels > 1)
        {
            // Find largest component (excluding background)
            int largestIdx = 1;
            int largestArea = stats.at<int>(1, CC_STAT_AREA);

            for (int i = 2; i < nLabels; i++)
            {
                int area = stats.at<int>(i, CC_STAT_AREA);
                if (area > largestArea)
                {
                    largestArea = area;
                    largestIdx = i;
                }
            }

            // Create mask with only the largest component
            Mat finalMask = Mat::zeros(cleanedMask.size(), CV_8UC1);
            for (int y = 0; y < labels.rows; y++)
            {
                for (int x = 0; x < labels.cols; x++)
                {
                    if (labels.at<int>(y, x) == largestIdx)
                    {
                        finalMask.at<uchar>(y, x) = 255;
                    }
                }
            }
            cleanedMask = finalMask;
        }

        return cleanedMask;
    }

    MaskBundle processMask(const Mat &redGreenFrame, Point bullCenter, double boardRadius, int camera_idx, bool debug_mode, const MaskParams &params)
    {
        MaskBundle result;

        // Step 1: Create basic mask (before bull carving)
        Mat basicMask;
        cvtColor(redGreenFrame, basicMask, COLOR_BGR2GRAY);
        threshold(basicMask, basicMask, params.binaryThreshold, 255, THRESH_BINARY);

        // Step 2: Extract bull red area (50-point bullseye)
        Mat bullRedMask = Mat::zeros(redGreenFrame.size(), CV_8UC1);
        const int searchRadius = bullCarveRadius(redGreenFrame, boardRadius, params);

        for (int y = max(0, bullCenter.y - searchRadius); y < min(redGreenFrame.rows, bullCenter.y + searchRadius); y++)
        {
            for (int x = max(0, bullCenter.x - searchRadius); x < min(redGreenFrame.cols, bullCenter.x + searchRadius); x++)
            {
                double distFromBull = norm(Point(x, y) - bullCenter);
                if (distFromBull <= searchRadius)
                {
                    Vec3b pixel = redGreenFrame.at<Vec3b>(y, x);
                    if (pixel == Vec3b(0, 0, 255)) // Red pixel
                    {
                        bullRedMask.at<uchar>(y, x) = 255;
                    }
                }
            }
        }

        Mat dilateKernel = getStructuringElement(MORPH_ELLIPSE, Size(5, 5));
        dilate(bullRedMask, bullRedMask, dilateKernel);

        result.bullMask = bullRedMask.clone(); // 50-point bullseye (double bull)

        // #1393: said per camera, without --debug, because this number decides the shape
        // of every mask below it and until now nothing anywhere printed it -- neither
        // the radius nor what it took. Both rules are on the line whichever chose, so a
        // reader of either run sees what the other would have done on this footage, and
        // the pixel count is what makes "the over-carve was masking nothing" a
        // measurement rather than an inference from the stages downstream.
        const int carved = countNonZero(bullRedMask);
        const int derived = max(1, (int)lround(boardRadius * params.bullCarveOfBoardRadius));
        const int frameRule = frameCarveRadius(redGreenFrame);
        log_info("Camera " + log_string(camera_idx + 1) + " bull carve: " + log_string(searchRadius) +
                 " px, carving " + log_string(carved) + " px of red; " +
                 log_string_src(carvedAgainstTheFrame()
                                    ? "OD_BULL_CARVE=frame, a fifteenth of the frame, where " +
                                          decimals(params.bullCarveOfBoardRadius, 4) + "x its board of " +
                                          to_string((int)lround(boardRadius)) + " px would give " +
                                          to_string(derived) + " px"
                                    : decimals(params.bullCarveOfBoardRadius, 4) + "x a board of " +
                                          to_string((int)lround(boardRadius)) + " px, where a fifteenth of " +
                                          "the frame would give " + to_string(frameRule) + " px"));

        // Step 3: Create fullMask - carved version that reveals ring structure
        result.fullMask = basicMask.clone();
        result.fullMask.setTo(0, bullRedMask); // Carve out bull to reveal underlying rings

        // Step 4: Create doubles mask (preprocessed, for ellipse detection)
        result.doublesMask = preprocessMask(result.fullMask, params); // Apply preprocessing to carved mask

        // Step 5: Create triples mask by subtracting doubles from full mask
        Mat triplesMaskRaw = result.fullMask.clone();
        triplesMaskRaw.setTo(0, result.doublesMask);                 // Remove doubles area from full mask
        result.triplesMask = preprocessMask(triplesMaskRaw, params); // Preprocess the remaining triples area

        // Step 6: Create outer bull mask by subtracting both doubles and triples
        Mat outerBullMaskRaw = result.fullMask.clone();
        outerBullMaskRaw.setTo(0, result.doublesMask);                   // Remove doubles
        outerBullMaskRaw.setTo(0, result.triplesMask);                   // Remove triples
        result.outerBullMask = preprocessMask(outerBullMaskRaw, params); // Preprocess the remaining outer bull area

        result.isValid = true;

        if (debug_mode)
        {
            odfs::ensureDirectory("debug_frames/mask_processing");
            imwrite("debug_frames/mask_processing/full_mask_" + to_string(camera_idx) + ".jpg", result.fullMask);
            imwrite("debug_frames/mask_processing/doubles_mask_" + to_string(camera_idx) + ".jpg", result.doublesMask);
            imwrite("debug_frames/mask_processing/triples_mask_" + to_string(camera_idx) + ".jpg", result.triplesMask);
            imwrite("debug_frames/mask_processing/outer_bull_mask_" + to_string(camera_idx) + ".jpg", result.outerBullMask);
            imwrite("debug_frames/mask_processing/bull_mask_" + to_string(camera_idx) + ".jpg", result.bullMask);

            // Create 2x2 grid visualization (no bull mask needed)
            int width = result.fullMask.cols;
            int height = result.fullMask.rows;
            Mat gridImage = Mat::zeros(height * 2, width * 2, CV_8UC1);

            // Top row: full_mask (top-left), doubles_mask (top-right)
            result.fullMask.copyTo(gridImage(Rect(0, 0, width, height)));
            result.doublesMask.copyTo(gridImage(Rect(width, 0, width, height)));

            // Bottom row: triples_mask (bottom-left), outer_bull_mask (bottom-right)
            result.triplesMask.copyTo(gridImage(Rect(0, height, width, height)));
            result.outerBullMask.copyTo(gridImage(Rect(width, height, width, height)));

            imwrite("debug_frames/mask_processing/mask_grid_" + to_string(camera_idx) + ".jpg", gridImage);
        }

        return result;
    }

} // namespace mask_processing
