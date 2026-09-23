#pragma once

#include <opencv2/opencv.hpp>
#include <vector>
#include <string>
#include "mask_processing.hpp"        // Include for MaskBundle
#include "perspective_processing.hpp" // #1485: the board's own millimetres
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace ellipse_processing
{

    // Comprehensive parameters for ellipse processing
    struct EllipseParams
    {
        // ===== BASIC PROCESSING PARAMETERS =====
        int minWhitePixelsThreshold = 1000; // Minimum white pixels in mask to attempt ellipse detection

        // ===== DOUBLE RAY TRACE CORE PARAMETERS ===== (Used for doubles ring)
        double angleStepDegrees = 3.0;  // Angular step between rays (120 rays total at 3°)
        int maxRayDistance = 800;       // Maximum pixel distance to cast rays
        int minValidRays = 50;          // Minimum rays needed for reliable ellipse fitting
        int innerRayStartDistance = 10; // Start searching for inner boundary this far from bull center

        // ===== ROLLING BASELINE VALIDATION PARAMETERS =====
        double maxRingWidthJump = 10.0;         // Max pixel jump in ring width between adjacent rays
        double ringWidthOutlierThreshold = 2.0; // Standard deviations for global outlier detection
        int minValidRingMeasurements = 20;      // Minimum valid ring measurements to attempt validation

        // ===== CONTOUR FITTING PARAMETERS ===== (Used for triples & bull)
        double minContourArea = 100.0; // Minimum contour area to consider
        double minAspectRatio = 0.3;   // Minimum aspect ratio for ellipse fitting
        double maxAspectRatio = 3.0;   // Maximum aspect ratio for ellipse fitting
        double minCircularity = 0.2;   // Minimum circularity for ellipse fitting

        // ===== VISUALIZATION PARAMETERS =====
        cv::Scalar rayColor = cv::Scalar(0, 255, 255);           // Yellow color for accepted rays (BGR)
        cv::Scalar boundaryPointColor = cv::Scalar(0, 255, 0);   // Green color for outer boundary points
        cv::Scalar innerPointColor = cv::Scalar(255, 0, 0);      // Blue color for inner boundary points
        cv::Scalar bullCenterColor = cv::Scalar(0, 0, 0);        // Black color for bull center
        cv::Scalar ellipseCenterColor = cv::Scalar(128, 0, 128); // Purple color for ellipse center
        int rayThickness = 1;                                    // Thickness of ray lines in pixels
        int boundaryPointRadius = 4;                             // Radius of boundary point circles
        int centerPointRadius = 4;                               // Radius for center point visualization
        cv::Scalar ellipseColor = cv::Scalar(255, 0, 255);       // Pink color for fitted ellipse (BGR)
        int ellipseThickness = 2;                                // Thickness of ellipse outline
    };

    // Rich boundary data structure with proper dartboard terminology
    struct EllipseBoundaryData
    {
        // RAY TRACING RESULTS (doubles ring - most accurate)
        cv::RotatedRect outerDoubleEllipse; // Dartboard edge (ray traced)
        cv::RotatedRect innerDoubleEllipse; // Double ring inner edge (ray traced)
        bool hasValidDoubles;               // Ray tracing succeeded for doubles
        int validOuterPoints;               // Number of validated outer boundary points
        int validInnerPoints;               // Number of validated inner boundary points

        // CONTOUR FITTING RESULTS (triples & bull rings - efficient)
        cv::RotatedRect outerTripleEllipse; // Triple ring outer edge (contour fitted)
        cv::RotatedRect innerTripleEllipse; // Triple ring inner edge (contour fitted)
        cv::RotatedRect outerBullEllipse;   // 25-point ring (contour fitted)
        cv::RotatedRect innerBullEllipse;   // 50-point bullseye (contour fitted)
        bool hasValidTriples;               // Contour fitting succeeded for triples
        bool hasValidBulls;                 // Contour fitting succeeded for bull rings

        // PERSPECTIVE INFO: For debugging/quality assessment
        double offsetX;         // Horizontal offset between bull and ellipse centers (pixels)
        double offsetY;         // Vertical offset between bull and ellipse centers (pixels)
        double offsetMagnitude; // Total offset distance (pixels)
        double offsetAngle;     // Offset angle in degrees

        // hasDetectedEllipses: Flag to indicate if all ellipses were detected
        bool hasDetectedEllipses;

        // Default constructor for fallback cases. #1330 added hasDetectedEllipses to it:
        // this struct is written to the calibration cache as bytes, and the slot kept for
        // a camera that produced no frame is a default-constructed one, so the flag was
        // an indeterminate byte on its way to disk and to whoever read it back.
        EllipseBoundaryData() : hasValidDoubles(false), validOuterPoints(0), validInnerPoints(0),
                                hasValidTriples(false), hasValidBulls(false),
                                offsetX(0.0), offsetY(0.0), offsetMagnitude(0.0), offsetAngle(0.0),
                                hasDetectedEllipses(false) {}
    };

    // ---- #1485: WHICH RING A FITTED CONTOUR REALLY IS --------------------------------
    //
    // Five of the six ring ellipses above are fitted to a COLOUR CONTOUR and named after
    // the ring they were expected to be, with nothing in between asking whether they are
    // it. `mask_processing::processMask` builds each mask by subtracting the last one
    // from the colour mask and keeping the LARGEST CONNECTED COMPONENT of what is left,
    // so on footage where the subtraction goes wrong once it goes wrong for every ring
    // after it -- and the name stays.
    //
    // Measured on mocks/rig-20260918/ at 77710ca, as a multiple of the ray-traced doubles
    // ring, where the board's own millimetres put the 25 ring at 15.9/170 = 0.0935:
    //
    //     camera 1  outer bull 0.9733      camera 2  0.6112      camera 3  0.3401
    //
    // -- 3.6x to 10.4x the ring it is named after, on all three cameras, while the
    // shipped mocks read 0.0961, 0.0991 and 0.0980. `score_processing::scorePoint` tests
    // the bull ellipses FIRST, so a dart anywhere inside that contour is published
    // `OUTER`, which `turnaus_client::postableSector` posts to Turnaus as a score of 25.
    // Eight of eleven darts on that fixture were published that way (#1485).
    //
    // THE REFERENCE IS THE DOUBLES RING AND NOTHING ELSE IS HELD TO ANYTHING HERE. It is
    // the one ellipse on this calibration that is ray-traced outward from the bull rather
    // than fitted to whatever contour was largest, and three other issues have measured
    // it independently on this very rig -- #1393's ~316 px fit, #1378's 1.63 spans,
    // #1423's reach. So the doubles ring is what the other five are read against, and it
    // is never itself refused by this rule.
    //
    // THE BANDS ARE GEOMETRY AND NOT A FITTED NUMBER, which is the whole of #1322's and
    // #1478's complaint about `horizontalScale = 0.95f  // (was 1.1f - too wide!)`. Each
    // ring's own radius in millimetres, over the doubles ring's, gives the six values
    //
    //     6.35/170=0.0374  15.9/170=0.0935  99/170=0.5824  107/170=0.6294  162/170=0.9529
    //
    // and a ring is refused when it is nearer to a DIFFERENT ring of the same board than
    // to its own -- the boundary being the geometric mean of the two, which is #1423's
    // own band and is derived from the millimetres rather than from any footage. Nothing
    // in here was chosen by looking at what this rig reads: every shipped-mocks ring
    // clears its band with room, and the three refused above miss theirs by 1.5x to 4x.
    //
    // A REFUSED RING IS ZEROED RATHER THAN CORRECTED. #1320's precedent, and the reason
    // is that the thing to put in its place would have to be invented: a ring ellipse
    // derived by scaling the doubles ring is a reconstruction nothing measured, and this
    // repository has paid for one of those already. A zeroed `RotatedRect` is already
    // what every reader downstream treats as absent -- `isPointInEllipse` cannot contain
    // a point and the radial ruler drops the mark -- so a dart in a ring nobody could
    // measure reads as the ring outside it, at the right RADIUS, instead of as a 25.
    enum RingIndex
    {
        kInnerBull = 0,
        kOuterBull,
        kInnerTriple,
        kOuterTriple,
        kInnerDouble,
        kRingCount
    };

    /** Each ring's radius over the doubles ring's, from the board's own millimetres. */
    inline double ringTruth(int ring)
    {
        const perspective_processing::DartboardSpec spec;
        const double board = spec.outerDoubleRadius;
        switch (ring)
        {
        case kInnerBull:
            return spec.bullRadius / board;
        case kOuterBull:
            return spec.bull25Radius / board;
        case kInnerTriple:
            return spec.innerTripleRadius / board;
        case kOuterTriple:
            return spec.outerTripleRadius / board;
        case kInnerDouble:
            return spec.innerDoubleRadius / board;
        default:
            return 1.0; // the doubles ring itself, the reference
        }
    }

    inline const char *ringName(int ring)
    {
        switch (ring)
        {
        case kInnerBull:
            return "the bullseye";
        case kOuterBull:
            return "the 25 ring";
        case kInnerTriple:
            return "the treble ring's inner edge";
        case kOuterTriple:
            return "the treble ring's outer edge";
        case kInnerDouble:
            return "the doubles ring's inner edge";
        default:
            return "the doubles ring";
        }
    }

    /**
     * The band this ring may be in, as multiples of the doubles ring. The upper edge is
     * the geometric mean with the ring outside it and the lower edge the geometric mean
     * with the ring inside it; the bullseye has nothing inside it, so its lower edge is
     * its own upper edge mirrored through it. Both edges come out of `ringTruth`, so a
     * board whose millimetres were ever corrected corrects these with it.
     */
    inline double ringBandHigh(int ring) { return std::sqrt(ringTruth(ring) * ringTruth(ring + 1)); }
    inline double ringBandLow(int ring)
    {
        if (ring == kInnerBull)
        {
            return ringTruth(kInnerBull) * ringTruth(kInnerBull) / ringBandHigh(kInnerBull);
        }
        return std::sqrt(ringTruth(ring - 1) * ringTruth(ring));
    }

    /** A ring ellipse's semi-major axis. The same half-of-the-larger-side every stage uses. */
    inline double ringReach(const cv::RotatedRect &e)
    {
        return 0.5 * std::max(e.size.width, e.size.height);
    }

    /**
     * #1485's falsifier, on the same binary: `OD_RINGS=asfitted` holds no ring to
     * anything and takes every fitted contour for the ring it is named after, which is
     * exactly what every build before this issue did. A switch that only ever refuses
     * would pass any check asking it to refuse.
     */
    inline bool ringsAreTakenAsFitted()
    {
        static const bool v = []
        {
            const char *e = std::getenv("OD_RINGS");
            return e != nullptr && std::string(e) == "asfitted";
        }();
        return v;
    }

    inline cv::RotatedRect &ringEllipse(EllipseBoundaryData &e, int ring)
    {
        switch (ring)
        {
        case kInnerBull:
            return e.innerBullEllipse;
        case kOuterBull:
            return e.outerBullEllipse;
        case kInnerTriple:
            return e.innerTripleEllipse;
        case kOuterTriple:
            return e.outerTripleEllipse;
        default:
            return e.innerDoubleEllipse;
        }
    }

    /**
     * Hold the five fitted rings to the board the doubles ring says this is, and zero the
     * ones that are not where the board puts them. Returns the sentence to print: every
     * ring, its reading and its band, because a refusal nobody can read is the silence
     * #1451 was filed about one field over.
     *
     * Pure and inline on purpose (#1338's reason): a tester holds this decision without
     * building the detector, and the numbers it asserts are read out of the same
     * `DartboardSpec` the scorer divides by.
     */
    inline std::string holdRingsToTheBoard(EllipseBoundaryData &e)
    {
        const double outerReach = ringReach(e.outerDoubleEllipse);
        if (!e.hasValidDoubles || !(outerReach > 0.0))
        {
            return "no ray-traced doubles ring, so there is nothing to read the other "
                   "rings against and none of them was held to anything";
        }

        // #1499: THE BOARD IS TAKEN BETWEEN THE TWO DOUBLES MARKS, NOT OFF THE OUTER ONE.
        //
        // The ray trace reads a COLOUR mask that has been through the colour stage's
        // bilateral filter and closing and preprocessMask's own dilation, so every traced
        // edge sits a few pixels PAST the paint, away from the band's middle: the outer
        // doubles mark reads beyond the 170 mm wire and the inner mark short of the
        // 162 mm one, by the same bloom. A span divided by the outer mark alone therefore
        // under-reads everything, by an amount that is a fact of the footage rather than
        // of any board: measured with i1499_band_census's morphology-free HSV read of the
        // frame's own paint, the treble band sits at 0.553-0.557 / 0.605-0.612 of the
        // outer mark on mocks/rig-20260918 and at 0.561-0.570 / 0.607-0.617 on the
        // upstream Unicorn mocks -- a DIFFERENT board under DIFFERENT cameras, so neither
        // a Blade 6 fact nor a fact of our rig's optics -- where the millimetres say
        // 0.5824 / 0.6294. Rescaled by the same footage's inner-doubles mark, the paint
        // lands on the millimetres (0.580-0.584 / 0.630 on the best cameras of both
        // fixtures). The bias lives in the traced reference, not in the boards.
        //
        // So the two marks are averaged as two estimates of one board: the outer mark
        // over 170/170 and the inner mark over 162/170, each a measurement of the board
        // radius, and their bloom errors point opposite ways and cancel. Both figures are
        // DartboardSpec millimetres; nothing here is fitted to footage. The inner mark is
        // trusted for this only where it sits inside the same band the loop below holds
        // it to -- no new constant -- and a camera whose inner mark is elsewhere keeps
        // the outer mark alone, which is what this function did before #1499.
        //
        // What it buys, measured: on mocks/rig-20260918 the outer treble edge read
        // 0.6067/0.6112/0.6100 of the outer mark against a band low of 0.6054 -- margins
        // of 0.0013 to 0.0058, one lighting change from a correct treble ring being
        // zeroed and every treble becoming a single. mocks/rig-20260922 crossed that
        // line on all three cameras through the C-contour fit repaired beside SECTION 2
        // in the .cpp; the de-biased board is what gives the repaired fit the margin a
        // band check needs (0.62-0.63 against the same 0.6054).
        double board = outerReach;
        const double innerReach = ringReach(e.innerDoubleEllipse);
        if (innerReach > 0.0)
        {
            const double asSeen = innerReach / outerReach;
            if (asSeen >= ringBandLow(kInnerDouble) && asSeen <= ringBandHigh(kInnerDouble))
            {
                board = 0.5 * (outerReach + innerReach / ringTruth(kInnerDouble));
            }
        }

        std::string said;
        int refused = 0;
        for (int ring = 0; ring < kRingCount; ring++)
        {
            cv::RotatedRect &fitted = ringEllipse(e, ring);
            const double reach = ringReach(fitted);
            if (!(reach > 0.0))
            {
                continue; // nothing was fitted for this ring; it is already absent
            }
            const double spans = reach / board;
            const bool where = spans >= ringBandLow(ring) && spans <= ringBandHigh(ring);
            char row[256];
            snprintf(row, sizeof(row), "%s%s %.4f of the board (%.4f..%.4f)%s",
                     said.empty() ? "" : "; ", ringName(ring), spans,
                     ringBandLow(ring), ringBandHigh(ring), where ? "" : " REFUSED");
            said += row;
            if (!where && !ringsAreTakenAsFitted())
            {
                fitted = cv::RotatedRect();
                refused++;
            }
        }

        e.hasValidTriples = (e.outerTripleEllipse.size.area() > 0 && e.innerTripleEllipse.size.area() > 0);
        e.hasValidBulls = (e.outerBullEllipse.size.area() > 0 || e.innerBullEllipse.size.area() > 0);

        if (ringsAreTakenAsFitted())
        {
            return "OD_RINGS=asfitted, so every contour is taken for the ring it is named "
                   "after the way it was before #1485 -- " + said;
        }
        return (refused == 0 ? std::string("every ring is where the board puts it -- ")
                             : std::to_string(refused) + " ring(s) are not where the board puts "
                                                         "them and were dropped -- ") +
               said;
    }

    /**
     * What one run of the ellipse stage produced: the geometry, and -- when the doubles
     * ring was not fitted -- why, in the words #1321 put on the ERROR line.
     *
     * #1330 separated the two, and the separation is the point. `EllipseBoundaryData` is
     * kept: it is carried inside DartboardCalibration, which utils/cache.hpp writes to
     * disk with a raw fwrite of sizeof(DartboardCalibration) bytes, so nothing in it may
     * own memory. `doublesFailure` is a std::string, and a string past its small-string
     * buffer is a pointer into this process's heap; written raw and read in another
     * process it names nothing, while a short reason survives by accident through SSO.
     *
     * It does not belong in the cache on its own terms either. The reason exists to be
     * PRINTED, once, to the operator standing in front of the board, by the one caller
     * that knows which camera this is -- and it is consumed in calibrateSingleCamera four
     * statements after it is produced. A cached calibration is geometry to score with;
     * last week's explanation of a ring that was not fitted is not part of it.
     */
    struct EllipseReport
    {
        EllipseBoundaryData ellipses;
        std::string doublesFailure; // empty when ellipses.hasValidDoubles
    };

    // Takes MaskBundle instead of single mask. Returns the geometry and the reason
    // together (#1330); only the geometry is ever cached.
    EllipseReport processEllipse(
        const cv::Mat &originalFrame,
        const mask_processing::MaskBundle &masks,
        const cv::Point &bullCenter,
        const cv::Point &frameCenter,
        int camera_idx = 0,
        bool debug_mode = false,
        const EllipseParams &params = EllipseParams());

} // namespace ellipse_processing