#pragma once

#include <opencv2/opencv.hpp>

using namespace cv;

namespace roi_processing
{
    /**
     * #1331 / ADR-0079: the region is drawn around a board that has already been found.
     *
     * Until this issue it was drawn around the middle of the FRAME, at a fixed 80% of the
     * frame's own dimensions, with three hand-fitted scale factors on top of it --
     * `roiSizePercent`, `horizontalScale = 0.95f  // (was 1.1f - too wide!)`,
     * `verticalScale` and `perspectiveMargin`. On 1280x720 that admitted x 154 to 1126
     * and y 44 to 676, and #1322 was filed against a rig whose board reached x=1130: it
     * was clipped by tens of pixels against a number nobody chose.
     *
     * The shipped mocks were never clipped by it and the measurement says so: both rigs
     * read the same six boards through the old ellipse and on the full frame, to the
     * pixel -- 291, 314 and 308 px on mocks/cam_*.mp4, 194, 195 and 197 px on
     * mocks/rig-20260918. What the ellipse cost was never visible on a camera aimed at
     * the middle of its own frame, which is exactly why it survived this long. Move that
     * same mock 230 px right and 150 px down, with its whole board still in shot and 27 px
     * of daylight to the frame edge, and the old ellipse measures its board at 176.9 px
     * where it really is 287.7 -- 0.61 of it -- and traces its doubles ring with 60 of 120
     * rays instead of 96, ten above the 50 at which a camera is refused outright.
     * `OD_ROI=frame` puts the ellipse back at run time and is how those numbers are taken
     * on one binary.
     *
     * ADR-0079 §1 refuses widening those constants on its own evidence: the number you
     * would widen to is fitted against a measurement the clipping distorts. So the board
     * is found first -- `bull_processing::measureBoard` on the full frame, the same
     * measurement #1320 and #1340 built and not a second opinion -- and this stage is
     * handed the answer.
     *
     * What is left is one constant and it decides nothing about admission. Nothing in
     * this file can refuse a camera any more: whether a board is framed is ADR-0079 §2's
     * question, asked of the FRAME's own edges in geometry_calibration, in pixels.
     */
    struct ROIParams
    {
        /**
         * How much wider than the board the region is drawn.
         *
         * #1378 moved this from 1.25 and the reason is which "board" it multiplies. The
         * number handed in is `bull_processing::measureBoard`'s: the smallest circle
         * enclosing the LARGEST OUTERMOST RED/GREEN CONTOUR. That is a colour
         * measurement, and it is the only thing available this early -- the doubles ring
         * is not fitted until STEP 6, four stages below here -- but what it lands on
         * depends on how much of a board's colour survives the mask, and #1340 had
         * already measured that it is not always the doubles ring.
         *
         * 1.25 was derived from a doubles ring BROKEN INTO ARCS: a third of one encloses
         * a circle of 0.87 of the board, so 1/0.87 = 1.15, plus a tenth. That worst case
         * is real and it is not the worst one. When the doubles ring drops out of the
         * colour mask ALTOGETHER -- a dull board under room light, which is what
         * mocks/rig-20260918 is -- the largest surviving red/green contour is the TREBLE
         * ring, and `minEnclosingCircle` then measures the trebles. A treble's outer wire
         * is at 107 mm where the double's is at 170 mm, so the measurement reads 107/170
         * = 0.629 of the board, and no margin under 1.59 can reach the doubles edge from
         * there.
         *
         * Measured, on the two fixtures, as the ratio of the FITTED doubles ellipse's own
         * semi-major axis to what this stage was handed:
         *
         *   mocks/cam_*.mp4     span 291, 314, 308 px   fitted semi-major 288, 282, 284
         *                       -> 0.99, 0.90, 0.92 : the span IS the board
         *   mocks/rig-20260918  span 194, 195, 197 px   fitted semi-major 316, 318, 316
         *                       -> 1.63, 1.63, 1.60 : the span is the treble ring
         *
         * 1.60 to 1.63 against the 1.59 the board's own millimetres predict, the
         * remainder being the blur and dilation #1320 measured adding pixels to a radius.
         * At 1.25 the rig's region cut its doubles ring at 0.77 of the way out and the
         * fit collapsed onto what was left: its three fitted boards went 197117, 200385
         * and 194335 px to 72171, 72374 and 72531 -- 36.6% -- and with them the
         * denominator #1339 and #1345 measure every dart against. Eight of nineteen
         * three-camera-corroborated dart windows were lost with it (#1378).
         *
         * So the constant is the same arithmetic, carried to the worst case that really
         * happens. The region must reach the board's RIM, because the number ring and the
         * wire ends live between the doubles and the rim and the colour stage is what
         * reads them: a board is 225.5 mm to its rim and a treble 107 mm, so 225.5/107 =
         * 2.107. Nothing is fitted to a rig. The sweep says the same thing about
         * headroom: on the rig the fitted board is 72k at 1.25 and 1.40, 197134/199112/
         * 194208 at 1.60, and exactly the pre-#1331 197117/200385/194335 from 1.80 through
         * 2.30, 2.60 and 3.00; on the mocks it is 183859/173006/175444 at every one of
         * those. 2.1 sits in the middle of a plateau on which neither fixture moves,
         * which is #1339's rule for a constant, and 1.25 sat 0.35 below a cliff.
         *
         * It cannot decide whether a board is framed and it cannot refuse a camera; the
         * region is intersected with the frame, so a margin that reaches past the frame
         * edge simply stops there -- which on the mocks, whose boards fill their frames,
         * it now does. `OD_ROI_MARGIN=<x>` moves it at run time on one binary, and
         * `OD_ROI=frame` still restores the ellipse ADR-0079 §1 retired.
         *
         * Both rigs, measured on the full frame, with the board radius this multiplies:
         * mocks 291/314/308 px -> 614/663/651 px of region radius; rig-20260918
         * 194/195/197 px -> 410/412/416 px. The frame keeps whatever of that runs off it,
         * and on the mocks -- whose boards fill their frames -- most of it does.
         *
         * And the board the MOTION stage measures against, which is the OTHER quantity
         * this pipeline calls "the board" and the one #1378's figures are in: the doubles
         * ring FITTED at STEP 6, 183859/173006/175444 px on the mocks and
         * 197117/200385/194335 px on mocks/rig-20260918. `1331-framing` section 2.5 pins
         * all six, because they have now moved twice under a tester that pinned their
         * consequence rather than themselves.
         */
        float roiRadiusOfBoardRadius = 2.107f;
    };

    /**
     * The region's radius in pixels, for the one board handed in.
     *
     * Exposed because `geometry_calibration` has to be able to ask, after STEP 6, whether
     * the doubles ring it just fitted fits INSIDE the region this stage drew. #1378 is
     * what happens when nobody can: the ring was cut, the fit landed on what survived,
     * every stage below read a board 36.6% of the right size, and not one line of any log
     * said a region had clipped anything.
     */
    double regionRadiusFor(double boardRadius, const ROIParams &params = ROIParams());

    /**
     * The region, drawn around the board that was found.
     *
     * `boardCenter` and `boardRadius` come from `bull_processing::measureBoard` on the
     * full frame. The result is the frame with everything outside that region blacked
     * out, at the frame's own size -- which is what the one consumer, the colour stage,
     * has always been handed, and why its own frame-relative windows still mean what
     * they meant.
     */
    Mat processROI(
        const Mat &frame,
        const Point &boardCenter,
        double boardRadius,
        bool debug_mode = false,
        int camera_idx = 0,
        const ROIParams &params = ROIParams());

} // namespace roi_processing
