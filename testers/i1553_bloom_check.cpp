// #1553: the treble band's bloom-corrected scoring boundary, held with geometry whose
// truth -- and whose BLOOM -- is planted.
//
// The fixtures measured it (91 shadow lines, 3 ring disagreements, all in the treble
// band, each inside its camera's own measured edge); this check holds the mechanism
// with planted numbers, because a correction proved only on the fixtures that
// motivated it is proved against paint measured by the machinery under test. The
// truth helpers are copied from i1510p2_model_check.cpp rather than shared, for that
// file's own stated reason: the tester must not borrow the code it is checking.
//
// What is asserted, and why each half exists:
//   - a control with NO planted bloom applies a 0.0 correction, so the spec edges
//     hold exactly as i1510p2_model_check already asserts them;
//   - planted bloom (inner ring seen 4 mm inward, outer 2.5 mm outward) is measured
//     by the fit and APPLIED: a dart 2 mm inside the spec inner edge reads T, one
//     1.5 mm past the spec outer edge reads T, and one past the corrected outer edge
//     reads S -- the exact shapes of the census's three disagreements;
//   - boundaryMm stays honest: distances are to the CORRECTED edges, from inside the
//     band and from outside it;
//   - the correction only ever WIDENS: planted anti-bloom (a narrower observed band)
//     is refused and the spec edges hold;
//   - the correction is BOUNDED: a planted 12 mm inner bloom, still inside its #1485
//     band, is clamped to the band's own 8 mm width;
//   - no measurement means no correction: an unobserved treble ring and an
//     out-of-band one each apply 0.0 on their edge, independently of the other edge;
//   - the required mutation, prediction stated first: zeroing the measured residuals
//     zeroes the correction and restores the disagreement shapes.
//
//   compiled by unit_check.sh (row 1553) with wire_model.cpp, like row 1510p2.

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
    const Truth truth = makeTruth(640.0, 360.0, 300.0, 240.0, 25.0, 0.18, 0.7);
    const double theta20 = comb + 3 * wire_model::kSector;
    // Every dart is planted mid-wedge, 1.5 sectors past the 20's start: wedge 1, so a
    // numbered call is S1/T1/D1 and the wedge boundary is far from every assertion.
    const double midWedge = theta20 + 1.5 * wire_model::kSector;
    auto scoreAt = [&](const Scene &s, double mm)
    {
        return scoreFromModel(profile, s.fit, s.anchor, imageOf(truth, mm / 170.0, midWedge));
    };

    // ---- control: no planted bloom, no correction, the spec edges hold -----------------
    {
        const Scene s = sceneOf(profile, truth, comb, 99.0, 107.0);
        say(s.fit.accepted && s.anchor.resolved, "control: the unbloomed board is ACCEPTED and anchored");
        const double innerAdj = trebleBloomAdjustMm(profile, s.fit, ellipse_processing::kInnerTriple);
        const double outerAdj = trebleBloomAdjustMm(profile, s.fit, ellipse_processing::kOuterTriple);
        say(std::fabs(innerAdj) < 0.3 && std::fabs(outerAdj) < 0.3,
            "control: corrections read " + board_model::detail::fmt("%.2f", innerAdj) + " / " +
                board_model::detail::fmt("%.2f", outerAdj) + " mm, wanted ~0 both");
        say(scoreAt(s, 97.0).score == "S1" && scoreAt(s, 100.0).score == "T1" &&
                scoreAt(s, 106.0).score == "T1" && scoreAt(s, 108.5).score == "S1",
            "control: 97/100/106/108.5 mm read S1/T1/T1/S1 at the spec edges");
    }

    // ---- planted bloom is measured, applied, and keeps boundaryMm honest ---------------
    {
        // The rigs' shape: inner edge segmented 4 mm inward (they measured -3.4..-7.6),
        // outer 2.5 mm outward (+1.2..+3.4). Corrected band: 95.0 .. 109.5 mm.
        const Scene s = sceneOf(profile, truth, comb, 95.0, 109.5);
        say(s.fit.accepted, "bloomed: the fit is still ACCEPTED (both edges inside their #1485 bands)");
        const RingResidual &ri = s.fit.rings[ellipse_processing::kInnerTriple];
        const RingResidual &ro = s.fit.rings[ellipse_processing::kOuterTriple];
        say(ri.rays == kResidualRays && std::fabs(ri.signedMedianMm - (-4.0)) < 0.4,
            "bloomed: the fit measures the inner edge at " +
                board_model::detail::fmt("%+.2f", ri.signedMedianMm) + " mm over " +
                std::to_string(ri.rays) + " rays, planted -4.0");
        say(ro.rays == kResidualRays && std::fabs(ro.signedMedianMm - 2.5) < 0.4,
            "bloomed: the fit measures the outer edge at " +
                board_model::detail::fmt("%+.2f", ro.signedMedianMm) + " mm over " +
                std::to_string(ro.rays) + " rays, planted +2.5");
        const double innerAdj = trebleBloomAdjustMm(profile, s.fit, ellipse_processing::kInnerTriple);
        const double outerAdj = trebleBloomAdjustMm(profile, s.fit, ellipse_processing::kOuterTriple);
        say(innerAdj == ri.signedMedianMm && outerAdj == ro.signedMedianMm,
            "bloomed: the correction IS the measurement (under the 8 mm bound, nothing is shaved)");

        // The census's three disagreement shapes, resolved.
        say(scoreAt(s, 97.0).score == "T1",
            "2 mm inside the spec inner edge reads T (the r=96.6 T14 shape; got " +
                scoreAt(s, 97.0).score + ")");
        say(scoreAt(s, 108.5).score == "T1",
            "1.5 mm past the spec outer edge reads T (the r=108.0 T14 shape; got " +
                scoreAt(s, 108.5).score + ")");
        say(scoreAt(s, 110.5).score == "S1",
            "1 mm past the CORRECTED outer edge still reads S -- the correction is the "
            "measured edge, not a giveaway (got " + scoreAt(s, 110.5).score + ")");
        say(scoreAt(s, 94.5).score == "S1",
            "0.5 mm inside the corrected inner edge reads S (got " + scoreAt(s, 94.5).score + ")");

        // boundaryMm is the distance to the corrected edges, both sides of them.
        const ModelScore mid = scoreAt(s, 103.0);
        say(std::fabs(mid.ringBoundaryMm - 6.5) < 1.0 && mid.boundaryMm == mid.ringBoundaryMm,
            "mid-band 103 mm reads its ring boundary as " +
                board_model::detail::fmt("%.2f", mid.ringBoundaryMm) +
                " mm, wanted 6.5 to the corrected outer edge (spec arithmetic says 4)");
        const ModelScore past = scoreAt(s, 110.5);
        say(std::fabs(past.ringBoundaryMm - 1.0) < 0.7,
            "110.5 mm reads " + board_model::detail::fmt("%.2f", past.ringBoundaryMm) +
                " mm to the corrected outer edge, wanted 1.0 (spec arithmetic says 3.5)");

        // ---- the required mutation, prediction first --------------------------------
        std::cout << "MUTATION PREDICTION: zeroing the two measured treble residuals zeroes the\n"
                     "  correction, so 97 mm reads S1 again, 108.5 mm reads S1 again, and 110.5 mm's\n"
                     "  ring boundary returns to ~3.5 mm -- the census's disagreement shapes restored."
                  << std::endl;
        BoardFit zeroed = s.fit;
        zeroed.rings[ellipse_processing::kInnerTriple].signedMedianMm = 0.0;
        zeroed.rings[ellipse_processing::kOuterTriple].signedMedianMm = 0.0;
        const ModelScore z97 = scoreFromModel(profile, zeroed, s.anchor, imageOf(truth, 97.0 / 170.0, midWedge));
        const ModelScore z108 = scoreFromModel(profile, zeroed, s.anchor, imageOf(truth, 108.5 / 170.0, midWedge));
        const ModelScore z110 = scoreFromModel(profile, zeroed, s.anchor, imageOf(truth, 110.5 / 170.0, midWedge));
        say(z97.score == "S1" && z108.score == "S1" && std::fabs(z110.ringBoundaryMm - 3.5) < 0.7,
            "MUTATION: zeroed, they read " + z97.score + "/" + z108.score + " with 110.5 mm at " +
                board_model::detail::fmt("%.2f", z110.ringBoundaryMm) + " mm -- as predicted");
    }

    // ---- anti-bloom is refused: the correction only ever widens the band ---------------
    {
        // A NARROWER observed band (inner +1.5, outer -1.5) is not bloom; the spec holds.
        const Scene s = sceneOf(profile, truth, comb, 100.5, 105.5);
        const double innerAdj = trebleBloomAdjustMm(profile, s.fit, ellipse_processing::kInnerTriple);
        const double outerAdj = trebleBloomAdjustMm(profile, s.fit, ellipse_processing::kOuterTriple);
        say(innerAdj == 0.0 && outerAdj == 0.0,
            "anti-bloom: a narrower observed band applies 0.0 / 0.0 (got " +
                board_model::detail::fmt("%.2f", innerAdj) + " / " +
                board_model::detail::fmt("%.2f", outerAdj) + ")");
        say(scoreAt(s, 100.0).score == "T1" && scoreAt(s, 106.0).score == "T1",
            "anti-bloom: 100 and 106 mm stay T at the SPEC edges -- the band never narrows");
    }

    // ---- the bound: a 12 mm 'bloom', in-band by #1485, is clamped to the band's 8 -----
    {
        // sqrt(15.9*99) ~ 39.7 mm: the inner treble's #1485 band reaches far enough down
        // that the band gate alone would believe this. The clamp is what refuses it.
        const Scene s = sceneOf(profile, truth, comb, 87.0, 107.0);
        say(s.fit.accepted, "bound: the 87 mm inner ring is inside its #1485 band, fit ACCEPTED");
        const double innerAdj = trebleBloomAdjustMm(profile, s.fit, ellipse_processing::kInnerTriple);
        say(innerAdj == -(double)(profile.outerTripleRadiusMm - profile.innerTripleRadiusMm),
            "bound: a 12 mm bloom is clamped to the band's own width, -8 mm (got " +
                board_model::detail::fmt("%.2f", innerAdj) + ")");
        say(scoreAt(s, 90.0).score == "S1" && scoreAt(s, 92.0).score == "T1",
            "bound: 90 mm reads S and 92 mm reads T across the clamped 91 mm edge");
    }

    // ---- no measurement, no correction, per edge independently ------------------------
    {
        // The inner ring was never traced: its edge stays spec while the outer edge,
        // measured, is still corrected. The denominator gates one edge at a time.
        const Scene s = sceneOf(profile, truth, comb, 0.0, 109.5);
        say(trebleBloomAdjustMm(profile, s.fit, ellipse_processing::kInnerTriple) == 0.0,
            "unobserved: an untraced inner ring applies 0.0");
        say(scoreAt(s, 97.0).score == "S1" && scoreAt(s, 108.5).score == "T1",
            "unobserved: 97 mm holds the spec inner edge while 108.5 mm gets the measured outer");
    }
    {
        // An inner ring OUTSIDE its #1485 band (30 mm) is a mis-identified ring: the fit
        // refuses it by name, and the correction refuses it too rather than moving a
        // scoring edge 69 mm on the fit's own rejected evidence.
        const Scene s = sceneOf(profile, truth, comb, 30.0, 107.0);
        say(!s.fit.geometryAccepted, "out-of-band: the 30 mm 'inner treble' is refused by the fit");
        say(trebleBloomAdjustMm(profile, s.fit, ellipse_processing::kInnerTriple) == 0.0,
            "out-of-band: and the correction applies 0.0, never a -69");
        say(scoreAt(s, 97.0).score == "S1", "out-of-band: 97 mm reads S1 at the spec edge");
    }

    std::cout << (failures == 0 ? "ALL OK" : "FAILURES: " + std::to_string(failures)) << std::endl;
    return failures == 0 ? 0 : 1;
}
