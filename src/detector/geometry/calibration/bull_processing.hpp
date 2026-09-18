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
        // ratio is the board's rather than this camera's, and it is the number the band
        // is drawn around -- but the band is wide, and both rigs in this repository say
        // why. What reaches this stage is not the bull, it is the bull after
        // color_processing's blur, bull's-eye dilation and multi-scale closing, and
        // those add pixels to a radius rather than a fraction to a ratio. So the smaller
        // the board sits in the frame, the further above 0.0935 its bull measures:
        //
        //   mocks/cam_{1,2,3}.mp4     board radius 247, 242, 250 px -> 0.100, 0.099, 0.098
        //   mocks/rig-20260918/       board radius 151, 153, 151 px -> 0.155, 0.154, 0.154
        //
        // Every constant in this repository was fitted against the first rig alone, and
        // this is one the second rig moved: a band of 0.5x to 2.0x fits both, and leaves
        // the second rig 1.2x from the ceiling, which is a constant that happens to fit.
        // The ceiling is 3.0x for that reason. The floor is what #1320 is about and it is
        // the one measured against the fault: the issue's speck is 205 px of area, 8.1 px
        // of radius, on a board whose bull is at x=710 and whose left edge is the x=392
        // the speck sits on -- a board radius near 320, so 0.025 of it, a quarter of the
        // floor. On the smallest board either rig shows, 151 px, that same speck is 0.054,
        // still below it.
        double bullRadiusOfBoardRadius = 0.0935; // Outer bull radius / doubles radius
        double minBullRadiusFactor = 0.5;        // Reject below half the ideal radius
        double maxBullRadiusFactor = 3.0;        // Reject above three times the ideal radius

        // Where the bull may be. A circle seen at an angle projects to an ellipse whose
        // centre is not the projection of the circle's centre, so the bull is measurably
        // off the middle of the region it sits in -- 0.21, 0.22 and 0.28 of the board
        // radius on the three mock cameras, and 0.14, 0.14 and 0.16 on the three of
        // rig-20260918, which are aimed straighter. The gate is 0.45, which is 1.6x the
        // furthest either rig has to go. #1320's speck is some 320 px from the bull on a
        // board of about that radius, which is past 1.0.
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
