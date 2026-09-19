#pragma once

#include <opencv2/opencv.hpp>

using namespace cv;
using namespace std;

namespace mask_processing
{
    // Parameters for mask processing
    struct MaskParams
    {
        int binaryThreshold = 1; // Threshold for binary mask creation

        // Preprocessing parameters (moved from ellipse_processing)
        int maskCloseKernelSize = 7;  // Kernel size for morphological closing
        int maskOpenKernelSize = 3;   // Kernel size for morphological opening
        int maskDilateKernelSize = 5; // Kernel size for dilation

        // #1393: how far from the bull centre a red pixel is taken to be the bull's.
        //
        // This was `min(cols, rows) / 15` -- a fixed 48 px at 720p on every camera, every
        // rig and every mounting -- and what it decides is not cosmetic. Everything red
        // within it becomes `bullMask`, and `bullMask` is carved out of `fullMask` at
        // processMask's Step 3, so the doubles, triples and outer-bull masks the ellipse
        // fit at STEP 6 runs on are ALL downstream of this number.
        //
        // The board's own millimetres give the fraction, and both figures are already
        // written down in perspective_processing::DartboardSpec: the 50-point bull is
        // `bullRadius` 6.35 mm and the doubles ring reaches `outerDoubleRadius` 170 mm,
        // so a 50-point bull is 6.35/170 = 0.0374 of the board radius. That is the
        // TARGET, and a search radius is not a measurement. What arrives here has been
        // through color_processing's blur, bull's-eye dilation and multi-scale closing,
        // which add pixels to a radius rather than a fraction to a ratio --
        // bull_processing's own band is 0.5x to 3.0x of its ideal for exactly that
        // reason, and its file measures the bull at 0.100 to 0.155 of the span it is
        // sized against where the geometry says 0.0935.
        //
        // So the geometry supplies a STOP rather than the target. DartboardSpec's
        // `bull25Radius` is 15.9 mm, the outer edge of the 25-ring: 15.9/170 = 0.0935 of
        // the board radius, and that is the furthest a search for RED can reach and
        // still be certain every red pixel it finds belongs to the bull. The annulus
        // between 6.35 mm and 15.9 mm is green on every board, and the first red outside
        // it is a single segment -- which is what the old carve was eating. 0.0935 is
        // 2.5x the bull it has to contain, it is the same ratio
        // bull_processing::BullParams::bullRadiusOfBoardRadius already carries, and it is
        // derived from the same two millimetre figures. Nothing here is fitted.
        //
        // The denominator is the board `bull_processing` measured, and NOT the bull it
        // measured, although `BullSighting::radius` is a per-camera reading of the very
        // ring this stops at. That radius is accepted anywhere in a 0.5x-3.0x band, so
        // taking it neat would re-admit a carve three times oversized on the camera where
        // the band was widest. The board radius is the quantity with a floor under it
        // (BullParams::minBoardRadius(), 64.2 px).
        double bullCarveOfBoardRadius = 0.0935;
    };

    // Bundle of all masks using proper dartboard terminology
    struct MaskBundle
    {
        Mat fullMask;         // Raw mask before any processing/carving
        Mat doublesMask;      // Doubles ring area (preprocessed for ellipse detection)
        Mat triplesMask;      // Triples ring area
        Mat outerBullMask;    // 25-point ring (single bull)
        Mat bullMask;         // 50-point bullseye (double bull) - carved red area
        bool isValid = false; // Flag if mask generation succeeded
    };

    /**
     * #1393: `boardRadius` is the board this camera measured, in pixels -- the
     * `BullSighting::boardRadius` the caller is already holding when it calls this. It
     * is what the bull carve is a fraction of. A board of no radius, which this stage
     * cannot be reached with today because the bull is refused first, falls back to the
     * frame rule and says so rather than carving nothing.
     */
    MaskBundle processMask(
        const Mat &redGreenFrame,
        Point bullCenter,
        double boardRadius,
        int camera_idx,
        bool debug_mode = false,
        const MaskParams &params = MaskParams());

} // namespace mask_processing
