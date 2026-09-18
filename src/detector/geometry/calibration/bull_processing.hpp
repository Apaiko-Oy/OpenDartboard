#pragma once

#include <opencv2/opencv.hpp>
#include <string>

using namespace cv;
using namespace std;

namespace bull_processing
{
    // Simplified parameters for bull detection only
    struct BullParams
    {
        // #1320: the board this stage sizes a candidate against is the one in the same
        // mask the candidates come from -- the largest red/green region, whose outer
        // boundary is the outside of the doubles ring. Nothing above this stage has
        // measured the board: the ROI is a fixed 80% of the frame (roi_processing) and
        // the ring ellipses are not fitted until STEP 6, three stages below here. So the
        // scale is taken here, and the only absolute number in it is how much of the
        // frame a board has to fill before it can be measured at all.
        double minBoardAreaPercent = 0.04; // A board smaller than this is not measurable

        // A dartboard is 451 mm across the doubles ring and its outer bull is 31.8 mm
        // across, so the bull's radius is 15.9/170 = 0.0935 of the doubles radius. That
        // ratio is the board's, not this camera's, and it survives perspective to well
        // within the band below.
        double bullRadiusOfBoardRadius = 0.0935; // Outer bull radius / doubles radius
        double minBullRadiusFactor = 0.5;        // Reject below half the ideal radius
        double maxBullRadiusFactor = 2.0;        // Reject above twice the ideal radius

        // Where the bull may be. A circle seen at an angle projects to an ellipse whose
        // centre is not the projection of the circle's centre, so the bull is measurably
        // off the middle of the region it sits in -- 0.22, 0.27 and 0.33 of the board
        // radius on the three mock cameras. The gate is what a board camera can do, not
        // what these three happen to do.
        double maxOffsetOfBoardRadius = 0.45; // Reject further than this from the board

        double minCircularity = 0.3; // Reject a candidate no rounder than this

        // What the survivors are ranked on. Circularity alone is what #1320 is about.
        double circularityWeight = 0.40; // How round it is
        double sizeWeight = 0.35;        // How close its radius is to a bull's
        double centralityWeight = 0.25;  // How close it sits to the board's middle

        // Candidates below this are still gated and counted, they are just not each
        // given a line in the debug log.
        double debugAreaFloor = 200.0;

        // Strategy B: Hough circle detection parameters
        double lowFeatureDensityThreshold = 0.05; // Threshold for low feature density
        double houghDp1 = 1.0;                    // DP for low density
        double houghDp2 = 1.2;                    // DP for high density
        int houghMinDistDivisor1 = 10;            // Min distance divisor for low density
        int houghMinDistDivisor2 = 8;             // Min distance divisor for high density
        int houghCannyThreshold1 = 70;            // Canny threshold for low density
        int houghCannyThreshold2 = 80;            // Canny threshold for high density
        int houghAccThreshold1 = 20;              // Accumulator threshold for low density
        int houghAccThreshold2 = 25;              // Accumulator threshold for high density
        int houghMinRadiusDivisor = 12;           // Minimum radius divisor
        double houghMaxRadiusDivisor = 1.5;       // Maximum radius divisor

        // Strategy C: Density map parameters
        double densityScoreBase = 0.3;     // Base score for density strategy
        double densityScoreDivisor = 20.0; // Divisor for density score calculation
    };

    /**
     * #1320: what bull detection answers, instead of a Point that is the frame centre
     * both when the bull is at the frame centre and when nothing was found at all.
     *
     * `found` false is the whole point of the struct: the caller must fail the camera
     * rather than trace the doubles ring from the least-bad blob, and `failure` is the
     * sentence it says while doing so -- every number in it measured, in the shape #1321
     * established for a calibration that could not be completed.
     */
    struct BullSighting
    {
        Point center{0, 0};      // Where the bull is; only meaningful when found
        bool found = false;      // A candidate passed every gate
        double score = 0.0;      // What it was ranked on
        double radius = 0.0;     // Its radius in pixels
        double boardRadius = 0.0;// The board it was sized against, in pixels
        Point boardCenter{0, 0}; // The middle of that board
        string basis;            // What the winner was chosen on, in words and numbers
        string failure;          // Why nothing was chosen; empty when found
    };

    // Find the bull, or say why there is not one
    BullSighting processBull(
        const Mat &redGreenFrame,
        const Point &frameCenter,
        int camera_idx = 0,
        bool debug_mode = false,
        const BullParams &params = BullParams());

} // namespace bull_processing
