// turnaus#1936: OD_TREBLE_BORROW -- a camera with no inner treble edge of its own judges at
// the one the other cameras measured -- held with geometry whose truth and bloom are planted.
//
// The synthetic board is the Pi board's shape: camera A (the reference) traces no treble ring
// at all, camera B traces the treble's inner edge 2.5 mm inward (the segmented band, #1553).
// The trebles are planted where B's edge says the treble is -- 97.0, 98.0 and 98.9 mm in the
// model's millimetres -- and read through camera A. With the switch off A judges at the spec
// 99.0 mm and reads all three S1 (the live losses' shape); with it on A borrows B's -2.5 mm
// and reads T1. The truth helpers are i1553_bloom_check.cpp's, copied for that file's reason:
// the tester must not borrow the code it is checking.
//
// Labels, each a FAIL line's prefix so a mutation's prediction can count them:
//   issue:    the three planted trebles, off reads S1 (wrong) and on reads T1 (right)
//   control:  95.5 S1, 100.0 T1, 106.5 T1 through A, on and off alike
//   outer:    the outer edge is never borrowed: B's outer seen at 109.5, A's 108.0 stays S1
//   measured: a camera that measured its own edge borrows nothing, among three
//   refused:  a measured anti-bloom edge is a measurement: it borrows nothing, donates nothing
//   none:     no donor, no borrow -- the spec edge holds
//   median:   two donors at -2.5 and -3.5 give -3.0
//   parse:    unset, "", "ON", "1" are off; exactly "on" is on
//
//   compiled with wire_model.cpp, and mutated three ways, by testers/i1936_check.sh.

#include <cmath>
#include <iostream>
#include <string>
#include <vector>
#include <cstdlib>

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

// ---- the planted truth (i1510p2_model_check.cpp's, copied on purpose) ------------------

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
static cv::RotatedRect ringSeenAt(const Truth &truth, double radiusMm)
{
    std::vector<cv::Point2f> points;
    const double u = radiusMm / 170.0;
    for (int i = 0; i < 90; i++)
    {
        points.push_back(imageOf(truth, u, 2.0 * CV_PI * i / 90.0));
    }
    return cv::fitEllipse(points);
}

/**
 * A whole synthetic calibration from the truth -- with the treble pair seen at STATED
 * radii, which is the one knob this check exists to turn: the board's treble band is
 * at 99..107 mm and the mask's segmentation of it is wherever bloom put it.
 */
static DartboardCalibration calibrationSeenThrough(const Truth &truth, double comb,
                                                   double innerTrebleSeenMm, double outerTrebleSeenMm)
{
    DartboardCalibration calib;
    const cv::Point2f bull = imageOf(truth, 0.0, 0.0);
    calib.bullCenter = cv::Point((int)std::lround(bull.x), (int)std::lround(bull.y));
    calib.camera_index = 0;
    calib.sees_board = true;

    calib.ellipses.outerDoubleEllipse = ringSeenAt(truth, 170.0);
    calib.ellipses.innerDoubleEllipse = ringSeenAt(truth, 162.0);
    if (outerTrebleSeenMm > 0.0)
    {
        calib.ellipses.outerTripleEllipse = ringSeenAt(truth, outerTrebleSeenMm);
    }
    if (innerTrebleSeenMm > 0.0)
    {
        calib.ellipses.innerTripleEllipse = ringSeenAt(truth, innerTrebleSeenMm);
    }
    calib.ellipses.outerBullEllipse = ringSeenAt(truth, 15.9);
    calib.ellipses.innerBullEllipse = ringSeenAt(truth, 6.35);
    calib.ellipses.hasValidDoubles = true;
    calib.ellipses.hasValidTriples = innerTrebleSeenMm > 0.0 && outerTrebleSeenMm > 0.0;
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

struct Scene
{
    BoardFit fit;
    ModelAnchor anchor;
};

static Scene sceneOf(const BoardProfile &profile, const Truth &truth, double comb,
                     double innerTrebleSeenMm, double outerTrebleSeenMm)
{
    const DartboardCalibration calib =
        calibrationSeenThrough(truth, comb, innerTrebleSeenMm, outerTrebleSeenMm);
    Scene s;
    s.fit = fitBoardToCamera(profile, calib);
    std::vector<cv::Point2f> endpoints(calib.wires.wireEndpoints.begin(), calib.wires.wireEndpoints.end());
    s.anchor = anchorOnBoard(s.fit, endpoints, calib.orientation.wedge20WireIndex);
    return s;
}

int main()
{
    const BoardProfile profile = profileFromSpec(perspective_processing::DartboardSpec());
    const double comb = 0.13;
    const Truth truthA = makeTruth(640.0, 360.0, 300.0, 240.0, 25.0, 0.18, 0.7);
    const Truth truthB = makeTruth(620.0, 340.0, 290.0, 250.0, -15.0, 0.22, 2.1);
    const double theta20 = comb + 3 * wire_model::kSector;
    const double midWedge = theta20 + 1.5 * wire_model::kSector; // wedge 1, far from its wires
    const int kIn = ellipse_processing::kInnerTriple;

    const Scene A = sceneOf(profile, truthA, comb, 0.0, 0.0);        // no treble ring traced
    const Scene B = sceneOf(profile, truthB, comb, 96.5, 107.0);     // inner edge 2.5 mm inward
    const Scene B2 = sceneOf(profile, truthB, comb, 96.5, 109.5);    // and its outer blooming
    const Scene D = sceneOf(profile, truthB, comb, 95.5, 107.0);     // inner edge 3.5 mm inward
    const Scene C = sceneOf(profile, truthB, comb, 100.5, 107.0);    // anti-bloom: measured, refused

    say(A.fit.accepted && A.anchor.resolved, "setup: camera A (no treble ring) is ACCEPTED and anchored");
    say(B.fit.accepted && !trebleEdgeMeasured(A.fit, kIn) && trebleEdgeMeasured(B.fit, kIn),
        "setup: B is ACCEPTED and measured its inner edge, A did not");
    say(std::fabs(trebleBloomAdjustMm(profile, B.fit, kIn) - (-2.5)) < 0.4,
        "setup: B's own inner correction reads " +
            board_model::detail::fmt("%+.2f", trebleBloomAdjustMm(profile, B.fit, kIn)) + " mm, planted -2.5");

    // The pair as the solve holds it, with the switch either way.
    auto pair = [&](bool on, const Scene &other)
    {
        std::vector<BoardFit> fits = {A.fit, other.fit};
        if (on)
        {
            borrowInnerTrebleEdge(profile, fits);
        }
        return fits;
    };
    auto through = [&](const BoardFit &fit, const Truth &truth, const ModelAnchor &anchor, double mm)
    { return scoreFromModel(profile, fit, anchor, imageOf(truth, mm / 170.0, midWedge)).score; };

    // ---- issue: the planted trebles ---------------------------------------------------------
    for (bool on : {false, true})
    {
        const std::vector<BoardFit> fits = pair(on, B);
        for (double mm : {97.0, 98.0, 98.9})
        {
            const std::string got = through(fits[0], truthA, A.anchor, mm);
            const std::string want = on ? "T1" : "S1";
            say(got == want, std::string("issue: switch ") + (on ? "on " : "off") + ", a treble at " +
                                 board_model::detail::fmt("%.1f", mm) + " mm through A reads " + got + ", wanted " +
                                 want + (on ? " (right)" : " (the live loss)"));
        }
    }

    // ---- control: away from the borrowed span nothing moves ---------------------------------
    for (bool on : {false, true})
    {
        const std::vector<BoardFit> fits = pair(on, B);
        const std::string a = through(fits[0], truthA, A.anchor, 95.5);
        const std::string b = through(fits[0], truthA, A.anchor, 100.0);
        const std::string c = through(fits[0], truthA, A.anchor, 106.5);
        say(a == "S1" && b == "T1" && c == "T1", std::string("control: switch ") + (on ? "on " : "off") +
                                                     ", 95.5/100.0/106.5 mm read " + a + "/" + b + "/" + c +
                                                     ", wanted S1/T1/T1");
    }

    // ---- outer: never borrowed ----------------------------------------------------------------
    {
        const std::vector<BoardFit> fits = pair(true, B2);
        const std::string got = through(fits[0], truthA, A.anchor, 108.0);
        say(got == "S1" && trebleBloomAdjustMm(profile, fits[0], ellipse_processing::kOuterTriple) == 0.0,
            "outer: with B's outer edge at 109.5, 108.0 mm through A reads " + got + ", wanted S1 (outer not borrowed)");
    }

    // ---- measured / median: three cameras -----------------------------------------------------
    {
        std::vector<BoardFit> fits = {A.fit, B.fit, D.fit};
        const int n = borrowInnerTrebleEdge(profile, fits);
        say(fits[1].borrowedInnerTrebleMm == 0.0 && fits[2].borrowedInnerTrebleMm == 0.0,
            "measured: B and D measured their own edges and borrowed " +
                board_model::detail::fmt("%.2f", fits[1].borrowedInnerTrebleMm) + " / " +
                board_model::detail::fmt("%.2f", fits[2].borrowedInnerTrebleMm) + ", wanted 0 / 0");
        say(n == 1 && std::fabs(fits[0].borrowedInnerTrebleMm - (-3.0)) < 0.4,
            "median: A borrows " + board_model::detail::fmt("%+.2f", fits[0].borrowedInnerTrebleMm) +
                " mm from -2.5 and -3.5, wanted -3.0; " + std::to_string(n) + " fit(s) borrowed, wanted 1");
    }

    // ---- refused: anti-bloom is a measurement --------------------------------------------------
    {
        std::vector<BoardFit> fits = {C.fit, B.fit};
        borrowInnerTrebleEdge(profile, fits);
        say(trebleEdgeMeasured(C.fit, kIn) && fits[0].borrowedInnerTrebleMm == 0.0,
            "refused: C measured an inward-reading band (anti-bloom) and borrows " +
                board_model::detail::fmt("%.2f", fits[0].borrowedInnerTrebleMm) + ", wanted 0");
        std::vector<BoardFit> fits2 = {A.fit, C.fit};
        borrowInnerTrebleEdge(profile, fits2);
        say(fits2[0].borrowedInnerTrebleMm == 0.0,
            "refused: C donates nothing to A (its correction is 0.0); A borrows " +
                board_model::detail::fmt("%.2f", fits2[0].borrowedInnerTrebleMm) + ", wanted 0");
    }

    // ---- none: no donor, no borrow ---------------------------------------------------------------
    {
        std::vector<BoardFit> fits = {A.fit, A.fit};
        const int n = borrowInnerTrebleEdge(profile, fits);
        const std::string got = through(fits[0], truthA, A.anchor, 98.0);
        say(n == 0 && got == "S1", "none: two cameras with no treble edge borrow " + std::to_string(n) +
                                       " and 98.0 mm reads " + got + ", wanted 0 and S1 (the spec edge)");
    }

    // ---- parse: the switch ----------------------------------------------------------------------
    say(!trebleBorrowFrom(nullptr) && !trebleBorrowFrom("") && !trebleBorrowFrom("ON") &&
            !trebleBorrowFrom("1") && trebleBorrowFrom("on"),
        "parse: unset/\"\"/ON/1 read off and \"on\" reads on");
    say(!trebleBorrowFrom(std::getenv("OD_TREBLE_BORROW")),
        "parse: the environment this check runs in (OD_TREBLE_BORROW unset) reads off");

    std::cout << (failures == 0 ? "ALL OK" : "FAILURES: " + std::to_string(failures)) << std::endl;
    return failures == 0 ? 0 : 1;
}
