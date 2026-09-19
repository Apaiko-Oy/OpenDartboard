#pragma once

#include <opencv2/opencv.hpp>
#include <string>

using namespace cv;
using namespace std;

namespace color_processing
{
    // Structure for color detection parameters - allows for easy configuration
    struct ColorParams
    {
        // Red color HSV ranges for dartboard detection
        int redLowHue1 = 0;    // First red range - low hue (wraps around HSV circle)
        int redLowSat1 = 60;   // First red range - minimum saturation
        int redLowVal1 = 60;   // First red range - minimum value/brightness
        int redHighHue1 = 20;  // First red range - high hue
        int redHighSat1 = 255; // First red range - maximum saturation
        int redHighVal1 = 255; // First red range - maximum value/brightness

        // Second red range (for hue wrapping around 180°)
        int redLowHue2 = 160;  // Second red range - low hue (handles HSV wraparound)
        int redLowSat2 = 60;   // Second red range - minimum saturation
        int redLowVal2 = 60;   // Second red range - minimum value/brightness
        int redHighHue2 = 180; // Second red range - high hue (HSV maximum)
        int redHighSat2 = 255; // Second red range - maximum saturation
        int redHighVal2 = 255; // Second red range - maximum value/brightness

        // Adaptive red detection for varying lighting (top vs bottom of dartboard)
        int topRedHueShift = 5;  // Hue adjustment for top portion of dartboard
        int topRedSatShift = 30; // Saturation adjustment for top portion of dartboard
        int topRedValShift = 20; // Value/brightness adjustment for top portion of dartboard

        // Green color HSV range for dartboard detection
        int greenLowHue = 38;   // Green range - low hue (green section of HSV)
        int greenLowSat = 60;   // Green range - minimum saturation
        int greenLowVal = 60;   // Green range - minimum value/brightness
        int greenHighHue = 88;  // Green range - high hue
        int greenHighSat = 255; // Green range - maximum saturation
        int greenHighVal = 255; // Green range - maximum value/brightness

        // White/bright area detection (for potential future use)
        int whiteLowVal = 210; // White detection - minimum brightness threshold
        int whiteHighSat = 15; // White detection - maximum saturation (low = white)

        // Image preprocessing parameters
        int bilateralD = 9;           // Bilateral filter diameter (noise reduction)
        int bilateralSigmaColor = 75; // Bilateral filter color variance (edge preservation)
        int bilateralSigmaSpace = 75; // Bilateral filter spatial variance (smoothing)

        // Basic morphological operations
        int basicCloseKernelSize = 3;    // Small closing to connect nearby segments
        int cleanOpenKernelSize = 3;     // Opening to remove small noise
        int cleanCloseKernelSize = 5;    // Closing to fill small gaps
        int maxMorphologyKernelSize = 7; // Maximum kernel size for multi-scale morphology

        // Bull's eye enhancement
        int bullsEyeRegionSize = 8;   // Bull's eye region divisor (image_width/8)
        int bullsEyeDilateKernel = 5; // Dilation kernel for bull's eye enhancement

        // Ring connection enhancement
        int topRegionCloseKernel = 9; // Aggressive closing kernel for top region

        // Component size filtering
        int minLargeComponentSize = 100;    // Minimum area for large dartboard components
        int minBlobArea = 80;               // Minimum area for any blob to be considered
        double maxDistanceFromCenter = 0.6; // Maximum distance from center as ratio of image width

        // Text detection and filtering
        double textAspectRatioMin = 0.4; // Minimum aspect ratio for text detection
        double textAspectRatioMax = 2.5; // Maximum aspect ratio for text detection
        int textMaxArea = 500;           // Maximum area for components classified as text

        // Component geometric filtering.
        //
        // #1323: these three distances -- centrality, the bull's-eye window and the
        // outer cutoff -- are measured from the middle of the BOARD wherever the board
        // can be measured in this frame, and from the middle of the frame only where it
        // cannot. The windows themselves are untouched: this issue MOVED them, it did
        // not widen them, because a window widened until one case passes is exactly the
        // repair it exists to avoid.
        //
        // #1394 moved their SIZE too, and these four are kept for `OD_COLOUR_WINDOWS=frame`
        // alone -- the shape `roi_processing` keeps ADR-0079 SS1's four hand-fitted numbers
        // in for `OD_ROI=frame`. At 1280 wide they are a fixed 320, 128, 384 and 96 px on
        // every camera, every rig and every mounting: the same window on a board 150 px
        // across and on one 400 px across.
        double centralityThreshold = 0.25;    // Distance threshold for central components (25% of image)
        double bullsEyeThreshold = 0.1;       // Distance threshold for bull's eye area (10% of image)
        double connectivityThreshold = 0.075; // Distance threshold for connected components

        // #1394: the same four windows, in board radii, which is the unit the question
        // each of them asks is really in -- is this blob the bull, is it a ring, is it a
        // number or the room? All four KEEP: a window too small drops real board, and one
        // too large lets a number or the room through to `bull_processing`. So each is
        // derived as a STOP rather than as a measurement -- #1393's rule, one stage over:
        // the furthest a search can reach and still be certain what it finds is the thing
        // it is looking for. Every millimetre below is quoted from
        // `perspective_processing::DartboardSpec`.
        //
        // WHAT THE LENGTH IS, AND WHAT IT IS NOT. The stage measures one length: the
        // smallest circle around the largest outermost contour of its own mask, the same
        // quantity `bull_processing::measureBoard` takes one stage later. That is a SPAN
        // and it is not the board. `roi_processing::ROIParams` measured which ring it
        // lands on, on both fixtures, against the doubles ellipse finally fitted at STEP 6:
        // 0.99, 0.90 and 0.92 of the board on mocks/cam_*.mp4, where the span IS the
        // doubles ring, and 1.63, 1.63 and 1.60 on mocks/rig-20260918, where the doubles
        // ring is not in the colour mask at all and the span is the TREBLE ring. So one
        // span means the board on one rig and 107/170 of it on the other, and no stage
        // here can tell which -- the same uncertainty, and the same measurement, that
        // `roiRadiusOfBoardRadius = 2.107` is built out of.
        //
        // The three OUTER windows therefore multiply `boardSpan * boardRadiusOfBoardSpan`,
        // which is the largest board a span can mean, because erring small on any of the
        // three drops coloured board. The INNER one multiplies the span itself, and that
        // is not an inconsistency: see `bullsEyeOfBoardSpan`.

        /**
         * The largest board a span can be, and the same arithmetic
         * `roi_processing::roiRadiusOfBoardRadius` is built from carried one ring less
         * far. A treble's outer wire is at `outerTripleRadius` 107 mm where a double's is
         * at `outerDoubleRadius` 170 mm, so a span that has landed on the treble ring is
         * 107/170 of the board and the board is 170/107 = 1.589 of the span. A span that
         * really is the doubles ring reads 1.589x too large, which is slack in the
         * direction that keeps board rather than the direction that drops it; the tester
         * says what that slack actually admits on both fixtures.
         */
        double boardRadiusOfBoardSpan = 1.589; // outerDoubleRadius / outerTripleRadius

        /**
         * The bull's-eye window, and the one window drawn against the SPAN rather than
         * against the board the span implies.
         *
         * What it does is unusual and worth saying: `isBullsEyeArea` is the last clause of
         * the keep, so anything inside it survives the text, edge, size and position
         * filters outright. It is a search for the bull and the 25-ring, and it is not a
         * measurement of either -- #1393's distinction, and here the gap is enormous. The
         * bull is `bullRadius` 6.35 mm of a 170 mm board, 0.037; the 25-ring reaches
         * `bull25Radius` 15.9 mm, 0.0935. A window of 0.0935 would find nothing, because
         * the point it is drawn around is not the bull: it is the centroid of the board's
         * outer boundary, and #1323 measured that sitting 21 to 68 px from the bull on the
         * five cameras where it can be measured at all.
         *
         * So the geometry supplies a stop. Between the 25-ring at 15.9 mm and the inner
         * edge of the trebles at `innerTripleRadius` 99 mm a dartboard is black and cream:
         * there is no red and no green in that annulus on any board, which is the same
         * fact `mask_processing::bullCarveOfBoardRadius` is argued from. 99/170 = 0.582 is
         * therefore the furthest this search can reach and still be certain that whatever
         * coloured thing it finds is the bull's or the 25-ring's.
         *
         * It multiplies the SPAN and not `boardSpan * boardRadiusOfBoardSpan` because the
         * stop then holds under BOTH readings of the span, which is the only window here
         * of which that is true: 0.582 of a span that is the doubles ring is 0.582 R, and
         * 0.582 of a span that is the treble ring is 0.366 R. Both land inside the empty
         * annulus (0.0935 R to 0.582 R), so the window is never wrong about what it is
         * admitting -- it is only more conservative on the rig, where it comes out at
         * 0.366 R and still clears #1323's 68 px worst case with room to spare. Taking the
         * larger reading here would put the stop at 0.925 R on the mocks, which is inside
         * the TREBLE ring and would admit treble fragments to a window that bypasses every
         * other filter.
         */
        double bullsEyeOfBoardSpan = 0.582; // innerTripleRadius / outerDoubleRadius

        /**
         * The centrality window: is this LARGE blob -- a third of the largest, or more --
         * part of the board?
         *
         * The last coloured thing on a dartboard is the outer wire of the doubles ring at
         * `outerDoubleRadius`, so a coloured component that belongs to a board has its
         * centroid inside 1.0 board radii and one that does not, does not. The stop is the
         * board's own coloured edge, which is why this one is 1.0 rather than a number.
         */
        double centralityOfBoardRadius = 1.0; // outerDoubleRadius / outerDoubleRadius

        /**
         * The outer cutoff: how far a MEDIUM blob joined to a neighbour may sit and still
         * be board.
         *
         * Wider than `centralityOfBoardRadius` for the reason the two clauses differ: this
         * one admits a fragment, and a fragment of the doubles ring has been through the
         * blur, dilation and closing above, which add pixels to a radius. The stop is the
         * board's RIM -- `roi_processing::ROIParams` quotes it as 225.5 mm against a
         * `outerDoubleRadius` of 170, the same two figures `roiRadiusOfBoardRadius` uses --
         * because the rim is where a dartboard stops being a dartboard. Between the doubles
         * and the rim lie the number ring and the wire ends, which is exactly the clutter
         * `textMaxArea`, `edgeTextThreshold` and the two positional filters were written
         * for; this window is not asked to do their job and is not sized to.
         */
        double maxDistanceOfBoardRadius = 1.326; // 225.5 mm rim / outerDoubleRadius

        /**
         * The connectivity window: how close another substantial blob has to be before
         * this one counts as joined to it.
         *
         * The thing it is really measuring is the gap between two adjacent fragments of
         * one ring, and the widest-spaced fragments on a board are the doubles': 20
         * segments on an annulus whose middle is (`innerDoubleRadius` 162 +
         * `outerDoubleRadius` 170)/2 = 166 mm, so two adjacent centroids are
         * 2 * 166 * sin(pi/20) = 51.9 mm apart, 0.306 of the board radius. Anything
         * narrower would leave a broken doubles ring reading as twenty unconnected blobs,
         * which is the case SECTION 6.5 exists for.
         */
        double connectivityOfBoardRadius = 0.306; // 2*(162+170)/2*sin(pi/20) / 170
        int minConnectedArea = 80;            // Minimum area to check for connectivity
        int minConnectedNeighborArea = 100;   // Minimum neighbor area for connectivity
        int largestAreaDivisor = 10;          // Divisor for largest area comparison (area > largest/10)
        int largestAreaRatio = 3;             // Ratio for large area comparison (area > largest/3)

        // #1323: how much of the frame the largest coloured region has to enclose before
        // its middle is taken for the middle of the board. Below it there is no board in
        // this frame to be off the middle of: the rule says so and falls back to the
        // middle of the frame, which is what shipped before #1323.
        //
        // It WAS bull_processing's minBoardAreaPercent: the same 4% of the frame asked
        // of the same quantity -- the area enclosed by the largest outermost contour --
        // one stage earlier, so that the two stages could not disagree about whether
        // there is a board in the picture. #1340 took that floor out of bull_processing,
        // because the area enclosed by that boundary collapses when the doubles ring
        // breaks into arcs, so what it measured was how well the ring closed rather than
        // how big the board is. The evidence is two numbers already in this comment: the
        // 2.45% and the 7.61% below are one board, one mounting, one distance.
        //
        // It is deliberately left standing HERE and the two stages now part company on
        // purpose. What this floor decides is which point the centrality windows are
        // drawn around, and its failure is a fallback to the middle of the frame -- the
        // behaviour that shipped before #1323, on a camera whose bull is 60 px from that
        // middle and well inside a 128 px window. What it also decides is which blobs
        // reach bull_processing at all, and #1323 measured that a different rule here
        // drops the bull on three of the six cameras that calibrate today. So it is not
        // a number to move without the rig in front of you, and moving it is its own
        // issue rather than a line in #1340's. Both rigs, measured:
        // the board encloses 20.23%, 19.42% and 20.59% of the frame on mocks/cam_*.mp4,
        // and 2.45%, 7.61% and 7.41% on mocks/rig-20260918, whose board sits smaller in
        // frame. So this floor is under five of the six by a factor of 1.9 to 5.1, and
        // over the sixth: on that rig's camera 1 the mask this stage holds is broken
        // into arcs rather than closed into one boundary, the board is NOT measured
        // here, and the rule falls back to the frame and says so. That camera keeps the
        // behaviour it had before #1323 -- its bull is 60 px from the middle of the
        // frame, well inside the window, and it was never the camera this issue is
        // about -- and the honest way to read that is #1320's lesson again: the rig with
        // the smaller board is where a constant runs out first, and here it already has.
        //
        // If it is ever changed, it is changed against the rig and not against this
        // comment. bull_processing no longer holds a twin of it to keep in step.
        double minBoardAreaPercent = 0.04; // Area enclosed by the board, as a share of the frame

        // Specific text filtering (targeting known problem areas)
        double edgeTextThreshold = 0.1;   // Distance from edge to classify as edge text (10% of image)
        int edgeTextMaxArea = 2000;       // Maximum area for edge text classification
        double bottomLeftTextX = 0.3;     // X threshold for bottom-left text detection (30% of width)
        double bottomLeftTextY = 0.7;     // Y threshold for bottom-left text detection (70% of height)
        double topRightTextX = 0.7;       // X threshold for top-right text detection (70% of width)
        double topRightTextY = 0.3;       // Y threshold for top-right text detection (30% of height)
        int positionalTextMaxArea = 3000; // Maximum area for positional text classification

        // Visualization parameters
        int nearbyColorCheckDistance = 2; // Pixel distance to check for nearby colors in visualization
    };

    // Input: roiFrame → Output: redGreenFrame (with red=bull, green=rings)
    Mat processColors(const Mat &roiFrame, int camera_idx = 0, bool debug_mode = false, const ColorParams &params = ColorParams());

    // Helper for when individual masks are needed
    pair<Mat, Mat> getIndividualColorMasks(const Mat &roiFrame, const ColorParams &params = ColorParams());
}
