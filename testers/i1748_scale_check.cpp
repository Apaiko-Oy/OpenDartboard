// #1748: the fit refuses a scale its own fitted ring contradicts -- held on the live
// log's figures, with geometry whose truth is KNOWN.
//
// Live on 2026-10-08 camera 1's traced conic was the doubles ring and the plane was
// built at 1.589 of it, and the fit said so in its own residuals -- the fitted doubles
// ring at -65.3 mm, the inner mark at -66.4, the held-out trebles at -42 -- and accepted
// it, because its only refusal was a per-ring band test that a uniform shrink passes.
// This check rebuilds that camera's calibration from the ring fractions its own
// ELLIPSE_PROCESSING line printed, reads it through the fit at the conic factor the
// pipeline used (1.589) and asserts the fit is REJECTED by name; then at 1.0, where it
// is accepted. Cameras 2 and 3 of the same session and the three cameras of
// mocks/rig-20260929 are the controls: their logged fractions, at 1.0, accepted, with
// the margin to the bound printed.
//
// The synthetic map is i1510_board_check's: an affine carrying the unit circle to a
// chosen ellipse composed with a Klein boost for the tilt, written here so this file
// borrows none of the code it checks. The outer doubles trace is the unit circle; the
// ELLIPSE_PROCESSING line states each ring over the DE-BIASED board (holdRingsToTheBoard
// divides by deBiasedBoardReach), and that board is 0.5 * (outer + inner / 0.9529), so
// outer = board * (2 - f_inner / 0.9529) and every ring sits at f * board / outer of the
// trace. Nothing here is a fitted number: the fractions are the log's, the 0.9529 is
// 162/170.
//
//   g++ -std=c++17 -I src -I src/utils -o scale_check testers/i1748_scale_check.cpp
//       with src/detector/geometry/calibration/wire_model.cpp and $(pkg-config opencv4)

#include <cmath>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

#include "detector/geometry/calibration/board_model.hpp"
#include "detector/geometry/calibration/ring_identity.hpp"

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

static std::string fmt(const char *pattern, double v)
{
    char buf[64];
    snprintf(buf, sizeof(buf), pattern, v);
    return buf;
}

// ---- the planted truth (i1510_board_check's) ---------------------------------------------

struct Truth
{
    cv::Matx33d H; // board plane (unit circle = the outer doubles trace) -> image
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

static cv::RotatedRect ringSeenAt(const Truth &truth, double unitRadius)
{
    std::vector<cv::Point2f> points;
    for (int i = 0; i < 90; i++)
    {
        points.push_back(imageOf(truth, unitRadius, 2.0 * CV_PI * i / 90.0));
    }
    return cv::fitEllipse(points);
}

/** One camera's ELLIPSE_PROCESSING line: five rings over the de-biased board. */
struct LoggedRings
{
    const char *name;
    double bull, ring25, innerTreble, outerTreble, innerDouble;
};

/** The calibration that line describes, seen through `truth` with the outer trace as unit 1. */
static DartboardCalibration calibrationFromLog(const Truth &truth, const LoggedRings &rings)
{
    const double innerOverOuterTruth = 162.0 / 170.0;
    const double outerOfBoard = 2.0 - rings.innerDouble / innerOverOuterTruth; // outer trace, in boards
    const double boardOfOuter = 1.0 / outerOfBoard;                             // the board, in traces

    DartboardCalibration calib;
    const cv::Point2f bull = imageOf(truth, 0.0, 0.0);
    calib.bullCenter = cv::Point((int)std::lround(bull.x), (int)std::lround(bull.y));
    calib.camera_index = 0;
    calib.sees_board = true;

    calib.ellipses.outerDoubleEllipse = ringSeenAt(truth, 1.0);
    calib.ellipses.innerDoubleEllipse = ringSeenAt(truth, rings.innerDouble * boardOfOuter);
    calib.ellipses.outerTripleEllipse = ringSeenAt(truth, rings.outerTreble * boardOfOuter);
    calib.ellipses.innerTripleEllipse = ringSeenAt(truth, rings.innerTreble * boardOfOuter);
    calib.ellipses.outerBullEllipse = ringSeenAt(truth, rings.ring25 * boardOfOuter);
    calib.ellipses.innerBullEllipse = ringSeenAt(truth, rings.bull * boardOfOuter);
    calib.ellipses.hasValidDoubles = true;
    calib.ellipses.hasValidTriples = true;
    calib.ellipses.hasValidBulls = true;
    calib.ellipses.validOuterPoints = 108;
    calib.ellipses.validInnerPoints = 100;

    for (int k = 0; k < 20; k++)
    {
        calib.wires.wireEndpoints.add(imageOf(truth, 1.0, 0.13 + k * wire_model::kSector));
    }
    calib.wires.wiresDetected = 20;
    calib.wires.isValid = true;

    calib.orientation.anchored = true;
    calib.orientation.wedge20WireIndex = 3;
    calib.orientation.wedgeNumber = 6;
    calib.orientation.cameraPosition = orientation_processing::CameraPosition::CONFIGURED;
    return calib;
}

static std::string residualsOf(const BoardFit &fit)
{
    std::string rows;
    for (int i = 0; i <= ellipse_processing::kRingCount; i++)
    {
        const RingResidual &r = fit.rings[i];
        if (!r.observed)
        {
            continue;
        }
        rows += (rows.empty() ? "" : ", ") + fmt("%+.1f", r.signedMedianMm) + (r.heldOut ? "" : "*");
    }
    return rows + " mm (* fitted)";
}

static double worstFittedMm(const BoardFit &fit)
{
    double worst = 0.0;
    for (int i = 0; i <= ellipse_processing::kRingCount; i++)
    {
        const RingResidual &r = fit.rings[i];
        if (r.observed && !r.heldOut)
        {
            worst = std::max(worst, std::fabs(r.signedMedianMm));
        }
    }
    return worst;
}

int main()
{
    const BoardProfile profile = profileFromSpec(perspective_processing::DartboardSpec());
    const double bound = fittedRingToleranceMm(profile);
    const double trebleOfDoubles = ring_identity::Spec().boardRadiusOfTrebleSpan(); // 1.589

    say(std::fabs(bound - (170.0 - 162.0)) < 1e-9,
        "the bound a fitted ring is held to is the width of its own ring, 170 - 162 = " + fmt("%.1f", bound) +
            " mm, read from the profile rather than chosen");

    // ---- live 2026-10-08, camera 1: the fault -----------------------------------------
    //
    // Its own lines: bull at (646,232), span 334.9 px, the traced conic 330 px, tilt
    // 0.168; "every ring is where the board puts it -- the bullseye 0.0379 ... the 25 ring
    // 0.0920 ... treble inner 0.5467 ... treble outer 0.6173 ... doubles inner 0.9311".
    const LoggedRings live1 = {"live 2026-10-08 camera 1", 0.0379, 0.0920, 0.5467, 0.6173, 0.9311};
    const Truth cam1 = makeTruth(646.0, 232.0, 330.0, 300.0, 10.0, 0.168, 0.5);
    const DartboardCalibration c1 = calibrationFromLog(cam1, live1);
    {
        // Read at the factor the pipeline used: 1.589, the ring identity's TREBLE.
        const BoardFit f = fitBoardToCamera(profile, c1, trebleOfDoubles);
        std::cout << "  " << live1.name << " at x" << fmt("%.3f", trebleOfDoubles) << ": residuals " << residualsOf(f)
                  << std::endl;
        say(f.planeBuilt, "the plane builds (as it did live)");
        say(worstFittedMm(f) > 50.0,
            "and its fitted ring sits " + fmt("%.1f", worstFittedMm(f)) +
                " mm from the wire it was taken for -- the live log's -65.3/-66.4 mm, rebuilt");
        say(!f.geometryAccepted && !f.accepted,
            "the fit is NOT geometryAccepted: a scale the fitted ring contradicts by 65 mm is refused");
        say(f.story.find("REJECTED") != std::string::npos &&
                f.story.find("further than that ring is wide") != std::string::npos,
            "... by name: the story says REJECTED and that the fitted edge sits further from its wire than the ring is wide");
        say(f.story.find("#1748") != std::string::npos, "... and cites this issue, so a reader can find the rule");
        std::cout << "  story: " << f.story.substr(f.story.find("REJECTED")) << std::endl;
    }
    {
        // The same calibration at 1.0 -- the traced conic taken for the doubles ring it
        // is -- is the camera rig-20260929 measured at +-3.5 mm.
        const BoardFit f = fitBoardToCamera(profile, c1, 1.0);
        std::cout << "  " << live1.name << " at x1.000: residuals " << residualsOf(f) << std::endl;
        say(f.geometryAccepted,
            "read at x1.0 the same rings are a board: geometryAccepted, worst fitted " +
                fmt("%.1f", worstFittedMm(f)) + " mm against the " + fmt("%.1f", bound) + " mm bound");
    }

    // ---- the controls: honest cameras, at the factor the pipeline used for them (1.0) ----
    const LoggedRings controls[] = {
        {"live 2026-10-08 camera 2 (look 1)", 0.0423, 0.0940, 0.5484, 0.6167, 0.9361},
        {"live 2026-10-08 camera 3 (look 1)", 0.0483, 0.0934, 0.5508, 0.6201, 0.9305},
        {"live 2026-10-08 camera 3 (look 4)", 0.0465, 0.0935, 0.5518, 0.6226, 0.9314},
        {"rig-20260929 camera 1", 0.0462, 0.0922, 0.5465, 0.6158, 0.9369},
        {"rig-20260929 camera 2", 0.0393, 0.0934, 0.5530, 0.6161, 0.9360},
        {"rig-20260929 camera 3", 0.0451, 0.1170, 0.5527, 0.6232, 0.9320},
    };
    const Truth cams[] = {
        makeTruth(650.0, 302.0, 316.0, 270.0, 20.0, 0.300, 0.9),
        makeTruth(698.0, 284.0, 283.0, 245.0, -15.0, 0.289, 2.2),
        makeTruth(698.0, 284.0, 283.0, 245.0, -15.0, 0.282, 2.2),
        makeTruth(648.0, 300.0, 316.0, 270.0, 20.0, 0.297, 0.9),
        makeTruth(646.0, 232.0, 330.0, 300.0, 10.0, 0.279, 0.5),
        makeTruth(710.0, 287.0, 285.0, 245.0, -15.0, 0.285, 2.2),
    };
    double worstControl = 0.0;
    for (size_t i = 0; i < sizeof(controls) / sizeof(controls[0]); i++)
    {
        const DartboardCalibration c = calibrationFromLog(cams[i], controls[i]);
        const BoardFit f = fitBoardToCamera(profile, c, 1.0);
        const double worst = worstFittedMm(f);
        worstControl = std::max(worstControl, worst);
        std::cout << "  " << controls[i].name << " at x1.000: residuals " << residualsOf(f) << std::endl;
        say(f.geometryAccepted, std::string(controls[i].name) + " is accepted, worst fitted " + fmt("%.1f", worst) +
                                    " mm (bound " + fmt("%.1f", bound) + ")");
    }
    say(worstControl < bound,
        "the worst fitted residual over the six honest cameras is " + fmt("%.1f", worstControl) +
            " mm, which leaves " + fmt("%.1f", bound - worstControl) + " mm to the bound");

    // ---- the bound is on the FITTED rings; the held-out bands are #1485's and unchanged --
    {
        // The treble pair standing as the doubles reference at x1.0 (i1510's own plant)
        // is still refused by the held-out bands, so #1510's check does not move.
        DartboardCalibration swapped = c1;
        swapped.ellipses.outerDoubleEllipse = c1.ellipses.outerTripleEllipse;
        swapped.ellipses.innerDoubleEllipse = c1.ellipses.innerTripleEllipse;
        swapped.ellipses.outerTripleEllipse = c1.ellipses.outerDoubleEllipse;
        swapped.ellipses.innerTripleEllipse = c1.ellipses.innerDoubleEllipse;
        const BoardFit f = fitBoardToCamera(profile, swapped, 1.0);
        say(!f.geometryAccepted && f.story.find("wrong ring identity") != std::string::npos,
            "the treble pair standing as the doubles reference is still refused by #1485's bands, by that name");
    }

    std::cout << (failures == 0 ? "i1748 check: PASS" : "i1748 check: FAIL") << std::endl;
    return failures == 0 ? 0 : 1;
}
