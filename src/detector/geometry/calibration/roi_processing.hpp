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
         * The board handed in is the smallest circle enclosing the outermost red/green
         * region, whose boundary is the outside of the doubles ring -- so at 1.0 the
         * region would cut the ring's own outer edge, and everything outside the doubles
         * (the number ring, the wire ends) with it. The margin is not fitted to a rig: it
         * is #1340's own arithmetic about what that measurement can under-read. A doubles
         * ring broken to a third of an arc encloses a circle of 0.87 of the board's
         * radius, so 1/0.87 = 1.15 is the worst under-reading this measurement admits,
         * and 1.25 is that with a tenth of the board on top of it.
         *
         * It cannot decide whether a board is framed and it cannot refuse a camera; the
         * region is intersected with the frame, so a margin that reaches past the frame
         * edge simply stops there. Both rigs, measured on the full frame and with the
         * board radius this multiplies: mocks 291/314/308 px -> 364/393/386 px of region
         * radius; rig-20260918 194/195/197 px -> 243/244/247 px.
         */
        float roiRadiusOfBoardRadius = 1.25f;
    };

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
