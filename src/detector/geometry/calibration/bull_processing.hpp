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
        // #1320/#1340: the board this stage sizes a candidate against is the one in the
        // same mask the candidates come from -- the largest red/green region, whose outer
        // boundary is the outside of the doubles ring. Nothing above this stage has
        // measured the board: the ROI is a fixed 80% of the frame (roi_processing) and
        // the ring ellipses are not fitted until STEP 6, three stages below here. So the
        // scale is taken here.
        //
        // It is taken from that region's EXTENT and not from the area it fills, and that
        // is #1340's repair. #1320 took the radius a disc of that area would have, which
        // is exact while the doubles ring closes into one boundary and collapses as soon
        // as it does not -- and on a real rig it routinely does not, because the ring
        // breaks wherever the light falls off it. Measured, on one board at one distance:
        //
        //   mocks/cam_{1,2,3}.mp4   disc radius 247, 242, 250 px   (#1320)
        //   mocks/rig-20260918      disc radius 151, 153, 151 px   (#1320)
        //   the SAME rig, live      disc radius 105 to 108 px      (#1340, four runs)
        //
        // The third row is the second row's hardware, untouched, on a different evening:
        // 34985, 29674, 35428 and 36444 px of enclosed area against a floor of 36864,
        // the last of them short by 420 pixels -- 1.1%. The board did not shrink by a
        // third between the two rows; its ring stopped closing. #1323 measured the same
        // split one stage earlier, in color_processing's own mask, where the rig's three
        // cameras enclose 2.45%, 7.61% and 7.41% of the frame -- 85, 149 and 147 px of
        // disc radius for three cameras 30 cm from one board. A measurement that moves by
        // 1.8x while the hardware does not is not measuring the board, and a FLOOR on it
        // was deciding which cameras calibrated at all.
        //
        // The smallest circle enclosing that boundary does not care which arcs are
        // missing. A closed ring gives its own radius; half of one gives the same radius,
        // because the ends of a half ring are a diameter apart; a third of one gives 0.87
        // of it. It can over-measure and it cannot collapse, and the direction matters:
        // an over-measured board widens the band a bull may sit in, and a wrong bull is
        // refused one stage later by the ellipse fit and two later by the wire count,
        // where a collapsed board refuses the camera outright right here.
        //
        // The centre moves with it, and that half was silently wrong too. The centroid of
        // a broken ring's boundary polygon is the centroid of the part-disc it encloses
        // -- for a half, 4R/3pi, some 0.42 of the board radius from the real middle,
        // against a maxOffsetOfBoardRadius of 0.45. A board whose ring had broken was
        // being measured around a point most of the way to the gate that refuses a bull
        // for being off centre.
        double smallestMeasurableBullRadius = 3.0; // px; see minBoardRadius() below

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
        //
        // #1340 note: those six ratios were measured through the disc radius that is no
        // longer taken. All six are boards whose ring closed in THIS stage's mask -- that
        // is what a disc radius of 247 and of 151 on those two rigs means -- and on a
        // closed ring the two radii agree to within the eccentricity of the ellipse a
        // board projects to: a few percent, in the direction that LOWERS the ratio and so
        // moves it toward 0.0935. Nothing here is refitted and the band does not move.
        // The rig whose ring does NOT close never reached a ratio to be in this table:
        // it was refused above, which is the whole of #1340.
        double bullRadiusOfBoardRadius = 0.0935; // Outer bull radius / doubles radius
        double minBullRadiusFactor = 0.5;        // Reject below half the ideal radius
        double maxBullRadiusFactor = 3.0;        // Reject above three times the ideal radius

        /**
         * #1340: how big a board has to be before this stage will size a bull on it, in
         * the terms this stage actually needs rather than in a share of the frame.
         *
         * 4% of the frame was a statement about where somebody mounted a camera. A
         * dartboard has no opinion about what fraction of a picture it fills, and the
         * maintainer's rig cannot be made to fill more of one: the setup was dismantled
         * and rebuilt during #1340's session specifically to test that, and the board
         * radius went from 152.6 / 149.8 / 154.0 px to 151.0 / 150.2 / 154.2. With this
         * lens at this distance ~150 px is what the hardware produces, and
         * mocks/cam_*.mp4's 247.8 px is a property of whatever camera recorded that
         * fixture rather than a target anything can be held to.
         *
         * What this stage does need of a board is that the smallest bull it would accept
         * on one is still something it can measure. That smallest is
         * bullRadiusOfBoardRadius * minBullRadiusFactor of the board radius -- 0.047 --
         * and what it has to survive is this stage's own first act, a 7x7 Gaussian: a
         * blob narrower than that kernel's own radius has no shape left to take a
         * circularity from, whatever the frame around it happens to be. So three pixels
         * is the smallest bull, and the floor is the board radius that yields one.
         *
         * That is 64.2 px of board radius. Both rigs clear it -- 247.8 px by 3.9x and the
         * 150 px the maintainer's rig produces by 2.3x -- where the old floor, 36864 px of
         * enclosed area and so a disc radius of 108, was grazed by that rig on every run
         * anybody measured. #1320's speck does not clear it: 205 px of area is 8.1 px of
         * disc radius and at most 16 px of extent even if every pixel of it lay on one
         * circle, so it is refused by a factor of four by a number fitted to nothing.
         *
         * One consequence is worth stating because it needs no rig to check. A region of
         * area A has an enclosing radius of at least sqrt(A/pi), so ANY region enclosing
         * 12950 px or more clears this floor whatever shape it is in. All four areas the
         * maintainer's rig measured -- 34985, 29674, 35428, 36444 -- are over that by
         * 2.3x or better, so every camera #1340 was filed about passes on its area alone.
         * The extent is what stops the board it then measures being a third too small.
         */
        double minBoardRadius() const
        {
            return smallestMeasurableBullRadius / (bullRadiusOfBoardRadius * minBullRadiusFactor);
        }

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
