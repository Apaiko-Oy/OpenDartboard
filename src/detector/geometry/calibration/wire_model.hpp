#pragma once

#include <opencv2/opencv.hpp>
#include <vector>

/**
 * #1467: A BOARD HAS TWENTY WIRES, SO FIT TWENTY RATHER THAN COUNT TO TWENTY.
 *
 * WHAT WAS WRONG WITH COUNTING. `findWiresByColorTransitions` promotes the angular
 * extremes of every contour in the mask, unconditionally, and `groupWiresByAngle` merges
 * whatever lands within nine degrees of a group's first member. The result is a NUMBER,
 * and #1442 made that number decide: twenty is a board and anything else is a refused
 * camera. #1466 then measured what the number is made of, and the finding is that it
 * cannot be repaired by tightening the grouping. The twenty-first boundary is not a wire
 * split in two -- it is an INTRUDER, mostly bull residue at r = 5-14 px whose angles are
 * quantised to integer pixels, sitting 7.93 to 13.12 degrees from the nearest real wire.
 * The tightest genuinely adjacent real pair on this footage is 9.03 degrees, INSIDE that
 * range. So there is no angular tolerance in image space that keeps every real wire and
 * drops every intruder, and the best repair simulated on 84 frames still picks the wrong
 * twenty on four of the eighteen over-counting frames.
 *
 * An intruder does not have to be told from a wire. It simply has to fail a model.
 *
 * THE MODEL, AND WHY IT COSTS NO POSE. A dartboard's wires are twenty rays of one plane,
 * eighteen degrees apart, about the bull. What a camera shows is that plane under a
 * projective map, and the two things this pipeline has already measured pin the map
 * exactly:
 *
 *   H(the unit circle) = the fitted doubles ellipse      (STEP 6)
 *   H(the origin)      = the detected bull centre        (STEP 4)
 *
 * The projective maps of the plane that fix a conic and a point inside it are the O(2)
 * that rotates about that point -- so those two measurements leave ONE free parameter,
 * the rotation offset, and it is fitted from the candidates themselves. There is no
 * `solvePnP` here, no intrinsics, no focal length and no two-fold pose ambiguity: that
 * ambiguity is an artefact of recovering a pose from a conic plus intrinsics, and a
 * bull collapses it. What survives is a MIRROR, which leaves every eighteen-degree
 * residual identical and is resolved downstream by ring nesting exactly as before.
 *
 * The construction is three matrices:
 *
 *   A   the affine map carrying the unit circle to the fitted ellipse. A(u) = C + R(phi)
 *       diag(a, b) u, read straight off the RotatedRect.
 *   p   = A^-1(bull), a point of the open unit disk. THIS IS THE ENTIRE TILT SIGNAL: it
 *       is where the board's centre sits relative to the ring around it, which on a
 *       square-on board is the middle and on a tilted one is not.
 *   M   the projective self-map of the unit disk carrying the origin to p. In the Klein
 *       model of the hyperbolic plane the disk's projective automorphisms are SO(2,1),
 *       and the one that moves the origin to p is a boost of rapidity atanh|p| along
 *       arg p. It preserves the unit circle, which is why A M still sends the circle to
 *       the ellipse.
 *
 *   H = A M,  and a board angle is atan2 of H^-1 applied to an image point.
 *
 * WHICH RING THE CONIC IS, AND WHY THIS MODULE ASKS #1423 RATHER THAN ASSUMING.
 * `|p|` is measured in units of the conic's own radius. A conic that is really the
 * TREBLE ring is 107/170 of the doubles ring, so the same bull sits proportionally
 * further from its centre, `|p|` reads 1.589 times too large and the recovered tilt is
 * wrong in a way that still looks plausible. `conicOfDoubles` is that factor and
 * #1423's `ring_identity::Sighting::boardRadiusOfSpan()` is where a caller gets it --
 * not re-derived here, because the decision of what an unreadable ring falls back to
 * belongs in one place and that place is #1423's module.
 *
 * WHAT THIS MODULE IS NOT. It is not a detector. `findWiresByColorTransitions` and the
 * Hough pass are unchanged and still supply every candidate; what changes is that their
 * PRECISION stops mattering, because a candidate now has to agree with a twenty-fold
 * model to be kept and the twenty boundaries are generated from the model where no
 * candidate agrees. That is why this is additive rather than a rewrite.
 */
namespace wire_model
{
    // MSVC does not define M_PI, and #1355 is why this is a header-local constant rather
    // than a define in CMakeLists.txt.
    inline constexpr double kPi = 3.14159265358979323846;

    /** How many wires a board has. Stated here too because this module is pure. */
    inline constexpr int kFold = 20;

    /** One sector, in radians: 18 degrees. */
    inline constexpr double kSector = 2.0 * kPi / kFold;

    /**
     * The board-space residual beyond which a candidate is not a wire.
     *
     * Measured on the fixtures rather than chosen: see `testers/i1467_run.sh` section 3,
     * which prints the residual distribution of every candidate on every fixture frame
     * and the gap between the two populations. #1467's own investigation put real wires
     * at a 0.98 degree mean with a 5.29 degree 99th percentile and intruders at 6.57
     * degrees with a 3.9 degree floor, so three degrees sits in clear air between them.
     */
    inline constexpr double kResidualCutDeg = 3.0;

    /**
     * How near a generated boundary has to be to a candidate before it gives way to it.
     *
     * The model is not more accurate than the detector on a wire the detector really
     * saw -- it is more accurate on the ones it did not. So a generated boundary with an
     * inlier beside it is replaced by that inlier's own angle, and the model supplies
     * only the rest. Two degrees is inside the residual cut by construction, so a
     * candidate near enough to snap to is an inlier by definition.
     */
    inline constexpr double kSnapDeg = 2.0;

    /**
     * The board plane, as a map both ways, plus the tilt it was built from.
     *
     * Not cached and not written anywhere: `DartboardCalibration` is fwritten to disk
     * byte for byte, and what goes on it is the two SCALARS this fit produces, not the
     * matrices.
     */
    struct Plane
    {
        bool built = false;
        cv::Matx33d H;       // board plane -> image
        cv::Matx33d Hinv;    // image -> board plane
        double tilt = 0.0;      // |p|, the whole tilt signal
        double tiltAngle = 0.0; // arg p, radians, board space
        double conicRadius = 1.0; // the board radius the FITTED conic sits at
    };

    /**
     * Build the plane from the fitted conic and the detected bull.
     *
     * `conicOfDoubles` is what the conic's radius must be multiplied by to be the
     * doubles ring -- 1.0 when the fit really is the doubles ring, 170/107 when it is
     * the treble ring. Callers take it from #1423 rather than deciding it here.
     */
    Plane planeOf(const cv::RotatedRect &conic, const cv::Point2f &bull, double conicOfDoubles = 1.0);

    /** Where an image point sits in board space, as an angle in radians. */
    double boardAngleOf(const Plane &plane, const cv::Point2f &image);

    /** Where a board angle at a board radius lands in the image. */
    cv::Point2f imageOfBoardAngle(const Plane &plane, double theta, double radius);

    /** What one frame's candidates say about the twenty-fold offset. */
    struct Fit
    {
        bool built = false;
        double offset = 0.0;         // theta0, radians, in (-9, +9] degrees
        double coherence = 0.0;      // R = |sum e^{i 20 theta}| / n
        double inlierFraction = 0.0; // share of candidates inside kResidualCutDeg
        double rmsResidualDeg = 0.0; // over every candidate, not only the inliers
        int candidates = 0;
        int inliers = 0;
    };

    /**
     * Fit the one free parameter, robustly.
     *
     * The unweighted circular mean of `e^{i 20 theta}` is the starting point and is also
     * where `coherence` is read, because R has to be a statement about what the frame
     * really contains rather than about what the reweighting decided to believe. Six
     * IRLS passes with Cauchy weights then move the offset off the outliers; a hard
     * inlier cut inside the loop would let one frame's intruders choose the cut's own
     * membership, which is the failure the reweighting exists to avoid.
     */
    Fit fitTwentyFold(const Plane &plane, const std::vector<cv::Point2f> &candidates);

    /**
     * The same coherence asked of a different periodicity -- the control that says the
     * fit did not manufacture its own structure. If it had, every fold would score
     * alike. #1467 measured 0.865 at twenty against 0.06 to 0.23 at sixteen, eighteen,
     * nineteen, twenty-one, twenty-two and twenty-four.
     */
    double coherenceAtFold(const Plane &plane, const std::vector<cv::Point2f> &candidates, int fold);

    /** Every candidate's residual to the nearest sector, in degrees, signed. */
    std::vector<double> residualsOf(const Plane &plane, const std::vector<cv::Point2f> &candidates, double offset);

    /** The twenty, and how many of them a candidate rather than the model placed. */
    struct Ring
    {
        std::vector<cv::Point2f> endpoints; // sorted by image angle about the bull
        int snapped = 0;                    // how many gave way to an inlier candidate
    };

    /**
     * Generate the twenty boundaries and let the candidates correct them.
     *
     * The order is the image-angle order every reader of `wireEndpoints` already
     * assumes, so nothing downstream moves.
     */
    Ring ringFrom(const Plane &plane, const Fit &fit, const std::vector<cv::Point2f> &candidates,
                  const cv::Point2f &bull);

    /**
     * The coherence below which this fit is not trusted, and the camera is refused in
     * those words.
     *
     * THE RISK THIS NUMBER IS, SAID WHERE IT IS PAID. The model rides entirely on the
     * bull centre: the conic fixes four of the map's degrees of freedom and the bull
     * fixes the rest, so a bull that is wrong tilts the whole board. #1467 measured the
     * cost -- about three pixels is free, five degrades, past twelve it breaks -- and
     * measured that R FALLS WITH IT, r = -0.863 against bull displacement over 84
     * frames. That correlation is the only reason this failure can be made loud, and
     * `testers/i1467_run.sh` section 5 is where it is proved rather than asserted: it
     * displaces the bull by a stated number of pixels in eight directions and reads R
     * and the worst boundary error back.
     *
     * `OD_WIRE_FIT_MIN=<x>` moves it on one binary, which is how the sweep behind the
     * number was taken.
     */
    double minimumCoherence();

    /**
     * `OD_WIRE_MODEL=count` puts the wire stage back on the counting path this issue
     * replaced -- group by angle, average each group, return however many groups there
     * were -- on the SAME binary, so "a different build" is never a confound. The
     * convention is `OD_RING=span`'s and `OD_WIRE_REGION=doubles`'s: anything but that
     * exact word is ignored rather than obeyed, so a typo reads in the behaviour this
     * stage is measured in rather than silently in the one it was broken in.
     */
    bool modelNotAsked();
}
