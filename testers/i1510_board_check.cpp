// #1510: the one-board fit, held with geometry whose truth is KNOWN.
//
// Every fixture measurement of the board fit is judged against paint that was itself
// measured by the machinery under test, so this check plants the one thing a fixture
// cannot supply: a ground-truth projective map. It builds its own homography H -- an
// affine carrying the unit circle to a chosen ellipse, composed with the Klein-disk
// boost that moves the origin to a chosen tilt point, the same family wire_model.hpp
// derives but written HERE so the tester does not borrow the code it is checking --
// generates every observation from it (ring ellipses via cv::fitEllipse over sampled
// circle images, the bull as H(0,0), twenty sector boundaries at a chosen comb), and
// then asks board_model to recover it from the observations alone.
//
// The falsification half is the issue's own list, one plant apiece: a rotation nothing
// anchored, the treble pair standing where the doubles pair should (wrong ring
// identity), every held-out ring removed (partial visibility that constrains nothing),
// support below the trace's floor, a barrel distortion no flat board explains, and a
// sector ring with no twenty-fold structure. Each must be REJECTED, by name, because a
// fit that cannot fail these is #1322's overfit one abstraction up.
//
//   g++ -std=c++17 -I src -I src/utils -o board_check testers/i1510_board_check.cpp
//       with src/detector/geometry/calibration/wire_model.cpp and $(pkg-config opencv4)

#include <cmath>
#include <iostream>
#include <string>
#include <vector>

#include "detector/geometry/calibration/board_model.hpp"

using namespace board_model;

static int failures = 0;

static void say(bool ok, const std::string &what)
{
    std::cout << (ok ? "OK   " : "FAIL ") << what << std::endl;
    if (!ok)
    {
        failures++;
    }
}

// ---- the planted truth -----------------------------------------------------------------

struct Truth
{
    cv::Matx33d H; // board plane (unit circle = 170 mm) -> image
};

static Truth makeTruth(double cx, double cy, double semiA, double semiB, double phiDeg,
                       double tiltR, double tiltArg)
{
    const double phi = phiDeg * CV_PI / 180.0;
    const double c = std::cos(phi), s = std::sin(phi);
    const cv::Matx33d A(semiA * c, -semiB * s, cx,
                        semiA * s, semiB * c, cy,
                        0.0, 0.0, 1.0);
    const double t = std::atanh(tiltR);
    const double ch = std::cosh(t), sh = std::sinh(t);
    const double cp = std::cos(tiltArg), sp = std::sin(tiltArg);
    const cv::Matx33d rot(cp, -sp, 0, sp, cp, 0, 0, 0, 1);
    const cv::Matx33d rotT(cp, sp, 0, -sp, cp, 0, 0, 0, 1);
    const cv::Matx33d boost(ch, 0, sh, 0, 1, 0, sh, 0, ch);
    Truth truth;
    truth.H = A * (rot * boost * rotT);
    return truth;
}

static cv::Point2f imageOf(const Truth &truth, double unitRadius, double theta)
{
    const cv::Vec3d q = truth.H * cv::Vec3d(unitRadius * std::cos(theta), unitRadius * std::sin(theta), 1.0);
    return cv::Point2f((float)(q[0] / q[2]), (float)(q[1] / q[2]));
}

/** The observed ellipse of one physical circle: 90 sampled image points, fitted. */
static cv::RotatedRect ringSeenAt(const Truth &truth, double radiusMm, double distortK = 0.0)
{
    std::vector<cv::Point2f> points;
    const double u0 = radiusMm / 170.0;
    const double u = u0 * (1.0 + distortK * u0 * u0); // the barrel plant, 0 for a flat board
    for (int i = 0; i < 90; i++)
    {
        points.push_back(imageOf(truth, u, 2.0 * CV_PI * i / 90.0));
    }
    return cv::fitEllipse(points);
}

/** A whole synthetic calibration from the truth: every observation, none of the map. */
static DartboardCalibration calibrationSeenThrough(const Truth &truth, double comb = 0.13,
                                                   double distortK = 0.0)
{
    DartboardCalibration calib;
    const cv::Point2f bull = imageOf(truth, 0.0, 0.0);
    calib.bullCenter = cv::Point((int)std::lround(bull.x), (int)std::lround(bull.y));
    calib.camera_index = 0;
    calib.sees_board = true;

    calib.ellipses.outerDoubleEllipse = ringSeenAt(truth, 170.0, distortK);
    calib.ellipses.innerDoubleEllipse = ringSeenAt(truth, 162.0, distortK);
    calib.ellipses.outerTripleEllipse = ringSeenAt(truth, 107.0, distortK);
    calib.ellipses.innerTripleEllipse = ringSeenAt(truth, 99.0, distortK);
    calib.ellipses.outerBullEllipse = ringSeenAt(truth, 15.9, distortK);
    calib.ellipses.innerBullEllipse = ringSeenAt(truth, 6.35, distortK);
    calib.ellipses.hasValidDoubles = true;
    calib.ellipses.hasValidTriples = true;
    calib.ellipses.hasValidBulls = true;
    calib.ellipses.validOuterPoints = 108;
    calib.ellipses.validInnerPoints = 100;

    for (int k = 0; k < 20; k++)
    {
        calib.wires.wireEndpoints.add(imageOf(truth, 1.0, comb + k * wire_model::kSector));
    }
    calib.wires.wiresDetected = 20;
    calib.wires.isValid = true;

    calib.orientation.anchored = true;
    calib.orientation.wedge20WireIndex = 3;
    calib.orientation.wedgeNumber = 6;
    calib.orientation.cameraPosition = orientation_processing::CameraPosition::CONFIGURED;
    return calib;
}

int main()
{
    const BoardProfile profile = profileFromSpec(perspective_processing::DartboardSpec());

    say(profile.rimRadiusMm > profile.outerDoubleRadiusMm,
        "the rim is outside the last scoring wire, and is a separate fact");
    say(profile.ringRadiusMm(ellipse_processing::kRingCount) == 170.0 &&
            profile.ringRadiusMm(ellipse_processing::kOuterTriple) == 107.0,
        "the profile's scoring radii are DartboardSpec's, never the 450 mm overall figure");

    // A camera 1.4 m out and a little off-axis: semi-axes 300x240 px, rotated 25
    // degrees, the board centre displaced to 0.18 of the ring at 40 degrees -- the same
    // order of tilt the rig fixtures show (wire_model measured |p| there).
    const Truth truth = makeTruth(640.0, 360.0, 300.0, 240.0, 25.0, 0.18, 0.7);
    const DartboardCalibration clean = calibrationSeenThrough(truth);

    // ---- recovery ----------------------------------------------------------------------
    const BoardFit fit = fitBoardToCamera(profile, clean);
    say(fit.planeBuilt, "a plane is built from the synthetic doubles ring and bull");
    say(fit.accepted, "the clean synthetic board is ACCEPTED whole: " + fit.story.substr(0, 60));
    say(fit.rotationResolved, "the configured anchor resolves the twenty-sector rotation");
    say(fit.heldOutObserved == 4 && fit.heldOutInBand == 4,
        "all four held-out rings were observed and all four are in band (got " +
            std::to_string(fit.heldOutInBand) + " of " + std::to_string(fit.heldOutObserved) + ")");

    double worstHeldOutMm = 0.0;
    for (const RingResidual &r : fit.rings)
    {
        if (r.observed && r.heldOut)
        {
            worstHeldOutMm = std::max(worstHeldOutMm, std::fabs(r.signedMedianMm));
        }
    }
    say(worstHeldOutMm < 1.5,
        "held-out residuals on a noise-free board stay under 1.5 mm (worst " +
            board_model::detail::fmt("%.2f", worstHeldOutMm) + " mm; the only planted error is the bull "
            "rounded to a whole pixel)");

    // The KNOWN TRANSFORM RECOVERED: the model's boundary for the held-out 107 mm ring
    // against the truth's own image of it, everywhere on the ring.
    double worstPx = 0.0;
    for (int i = 0; i < 36; i++)
    {
        const double theta = 2.0 * CV_PI * i / 36.0;
        // The fit's board angle theta is its own frame's; compare radially instead:
        // both circles are closed curves, so compare each truth point against the
        // nearest model point along the ray from the recovered bull.
        const cv::Point2f want = imageOf(truth, 107.0 / 170.0, theta);
        const cv::Point2f bull((float)clean.bullCenter.x, (float)clean.bullCenter.y);
        cv::Point2f dir = want - bull;
        const double len = std::sqrt((double)dir.x * dir.x + (double)dir.y * dir.y);
        dir *= (float)(1.0 / len);
        // where the MODEL puts the 107 mm circle on this same ray
        double lo = 0.0, hi = 2.5 * len;
        for (int it = 0; it < 60; it++)
        {
            const double mid = 0.5 * (lo + hi);
            const cv::Point2f p = bull + dir * (float)mid;
            const cv::Point2f board = boardPointOf(fit, p);
            const double mm = std::sqrt((double)board.x * board.x + (double)board.y * board.y);
            (mm < 107.0 ? lo : hi) = mid;
        }
        worstPx = std::max(worstPx, std::fabs(0.5 * (lo + hi) - len));
    }
    say(worstPx < 2.0, "the planted transform is recovered: the model's 107 mm boundary sits "
                       "within 2 px of the truth's everywhere (worst " +
                           board_model::detail::fmt("%.2f", worstPx) + " px)");

    // The millimetre scale both ways: a point planted at 100 mm reads back as 100 mm.
    const cv::Point2f planted = imageOf(truth, 100.0 / 170.0, 1.0);
    const cv::Point2f board = boardPointOf(fit, planted);
    const double mm = std::sqrt((double)board.x * board.x + (double)board.y * board.y);
    say(std::fabs(mm - 100.0) < 1.5, "a point planted 100 mm from centre reads " +
                                         board_model::detail::fmt("%.2f", mm) + " mm through the fit");

    // ---- the falsifiers, one plant apiece ----------------------------------------------
    {
        DartboardCalibration unanchored = clean;
        unanchored.orientation.anchored = false;
        unanchored.orientation.wedge20WireIndex = -1;
        unanchored.orientation.cameraPosition = orientation_processing::CameraPosition::UNKNOWN;
        const BoardFit f = fitBoardToCamera(profile, unanchored);
        say(!f.accepted && !f.rotationResolved && f.geometryAccepted,
            "no anchor: the geometry stands and the fit is still REJECTED");
        say(f.story.find("UNRESOLVED") != std::string::npos &&
                f.story.find("OD_CAMERA_WEDGES") != std::string::npos,
            "... and the rejection names the rotation and the operator's remedy");
    }
    {
        // Wrong ring identity: the treble pair standing where the doubles pair should,
        // which is what an unasked #1423 would let through (rig-20260918's own failure
        // shape, #1378). Every held-out ring then sits far outside its band.
        DartboardCalibration swapped = clean;
        swapped.ellipses.outerDoubleEllipse = clean.ellipses.outerTripleEllipse;
        swapped.ellipses.innerDoubleEllipse = clean.ellipses.innerTripleEllipse;
        swapped.ellipses.outerTripleEllipse = clean.ellipses.outerDoubleEllipse;
        swapped.ellipses.innerTripleEllipse = clean.ellipses.innerDoubleEllipse;
        const BoardFit f = fitBoardToCamera(profile, swapped);
        say(!f.accepted && !f.geometryAccepted,
            "the treble ring standing as the doubles reference is REJECTED");
        say(f.story.find("wrong ring identity") != std::string::npos,
            "... naming wrong ring identity");
    }
    {
        DartboardCalibration bare = clean;
        bare.ellipses.outerTripleEllipse = cv::RotatedRect();
        bare.ellipses.innerTripleEllipse = cv::RotatedRect();
        bare.ellipses.outerBullEllipse = cv::RotatedRect();
        bare.ellipses.innerBullEllipse = cv::RotatedRect();
        const BoardFit f = fitBoardToCamera(profile, bare);
        say(!f.accepted && f.heldOutObserved == 0 &&
                f.story.find("nothing independent") != std::string::npos,
            "a fit nothing independent tests is REJECTED, in those words");
    }
    {
        DartboardCalibration thin = clean;
        thin.ellipses.validOuterPoints = 20;
        const BoardFit f = fitBoardToCamera(profile, thin);
        say(!f.accepted && f.story.find("insufficient coverage") != std::string::npos,
            "20 of 120 supporting rays is REJECTED as insufficient coverage");
    }
    {
        // A 15% barrel at the rim: the doubles pair still agrees with itself, so the
        // scale cannot see it, and the held-out trebles land outside their band -- the
        // residual pattern a flat projective board cannot explain.
        const DartboardCalibration barrel = calibrationSeenThrough(truth, 0.13, 0.15);
        const BoardFit f = fitBoardToCamera(profile, barrel);
        say(!f.accepted && !f.geometryAccepted &&
                f.story.find("distortion") != std::string::npos,
            "a 15% barrel distortion is REJECTED, naming distortion");
    }
    {
        // Twenty boundaries with no twenty-fold structure: no rotation may be indexed
        // on them whatever the anchor claims.
        DartboardCalibration scattered = clean;
        scattered.wires.wireEndpoints = wire_processing::WireEndpoints();
        for (int k = 0; k < 20; k++)
        {
            scattered.wires.wireEndpoints.add(imageOf(truth, 1.0, 0.37 * k * k));
        }
        const BoardFit f = fitBoardToCamera(profile, scattered);
        say(!f.accepted && f.wireCoherence < wire_model::minimumCoherence() &&
                f.story.find("twenty-fold ring") != std::string::npos,
            "a sector ring with no twenty-fold structure is REJECTED (R=" +
                board_model::detail::fmt("%.3f", f.wireCoherence) + ")");
    }

    std::cout << (failures == 0 ? "ALL OK" : "FAILURES: " + std::to_string(failures)) << std::endl;
    return failures == 0 ? 0 : 1;
}
