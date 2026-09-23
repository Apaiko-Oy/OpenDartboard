// #1510 Phase 2: the model-scoring entry point, held with geometry whose truth is KNOWN.
//
// i1510_board_check.cpp holds the FIT against a planted homography; this holds what
// Phase 2 builds on top of it -- anchorOnBoard and scoreFromModel -- against the same
// planted truth, because a scorer measured only on fixtures is judged against paint
// that was itself measured by the machinery under test. The truth helpers are copied
// from that check rather than shared, for its own stated reason: the tester must not
// borrow the code it is checking.
//
// What is asserted, and why each half exists:
//   - ring classification at planted millimetre radii, BULL through MISS, because the
//     model's rings come from the profile's radii through the fit and not from any
//     observed ellipse;
//   - wedge NAMING in both handednesses: the sequence must advance the way the
//     endpoint store advances, which is the mirror wire_model.hpp names and the thing
//     a scorer that assumed one handedness would get right on every camera it was
//     written against and wrong on the first that flips;
//   - the boundary distances -- #1512's seed -- against arithmetic done here by hand;
//   - that no anchor means NO WEDGE (S?, never an asserted 20), because asserting is
//     the published fallback's behaviour and the model must not inherit it silently.
//
//   compiled by unit_check.sh (row 1510p2) with wire_model.cpp, like row 1510.

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

// ---- the planted truth (i1510_board_check.cpp's, copied on purpose) --------------------

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

/** A whole synthetic calibration from the truth: every observation, none of the map. */
static DartboardCalibration calibrationSeenThrough(const Truth &truth, double comb)
{
    DartboardCalibration calib;
    const cv::Point2f bull = imageOf(truth, 0.0, 0.0);
    calib.bullCenter = cv::Point((int)std::lround(bull.x), (int)std::lround(bull.y));
    calib.camera_index = 0;
    calib.sees_board = true;

    calib.ellipses.outerDoubleEllipse = ringSeenAt(truth, 170.0);
    calib.ellipses.innerDoubleEllipse = ringSeenAt(truth, 162.0);
    calib.ellipses.outerTripleEllipse = ringSeenAt(truth, 107.0);
    calib.ellipses.innerTripleEllipse = ringSeenAt(truth, 99.0);
    calib.ellipses.outerBullEllipse = ringSeenAt(truth, 15.9);
    calib.ellipses.innerBullEllipse = ringSeenAt(truth, 6.35);
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

// The board's sequence, restated here so the expectation is not read off the code under
// test. Clockwise from the 20, the order findWedgeSlot's dartboard_numbers holds.
static const int sequence[20] = {20, 1, 18, 4, 13, 6, 10, 15, 2, 17, 3, 19, 7, 16, 8, 11, 14, 9, 12, 5};

int main()
{
    const BoardProfile profile = profileFromSpec(perspective_processing::DartboardSpec());
    const double comb = 0.13;
    const Truth truth = makeTruth(640.0, 360.0, 300.0, 240.0, 25.0, 0.18, 0.7);
    const DartboardCalibration calib = calibrationSeenThrough(truth, comb);
    const BoardFit fit = fitBoardToCamera(profile, calib);
    say(fit.accepted, "the synthetic board is ACCEPTED (the fit half is i1510_board_check's subject)");

    std::vector<cv::Point2f> endpoints(calib.wires.wireEndpoints.begin(), calib.wires.wireEndpoints.end());
    const ModelAnchor anchor = anchorOnBoard(fit, endpoints, calib.orientation.wedge20WireIndex);
    say(anchor.resolved, "the anchor resolves from twenty endpoints and a wire index");
    say(anchor.advance > 0.0, "this truth preserves orientation, and the anchor reads advance +1");
    const double theta20 = comb + 3 * wire_model::kSector;
    say(std::fabs(board_model::detail::wrapToPi(anchor.theta20 - theta20)) < 0.02,
        "theta20 is wire 3's comb line (planted " + detail::fmt("%.3f", theta20) +
            ", read " + detail::fmt("%.3f", anchor.theta20) + ")");

    // ---- ring classification at planted radii, through the fit ------------------------
    struct RingCase
    {
        double mm;
        const char *word;
        const char *score; // planted 1.5 sectors past theta20, so numbered rings are wedge 1
    };
    const RingCase ringCases[] = {
        {5.0, "bull", "BULL"}, {10.0, "outer", "OUTER"}, {50.0, "single", "S1"},
        {103.0, "triple", "T1"}, {130.0, "single", "S1"}, {165.0, "double", "D1"},
        {200.0, "", "MISS"}};
    for (const RingCase &c : ringCases)
    {
        // Planted mid-wedge, one wedge past the 20's start: sequence[1] = 1.
        const cv::Point2f p = imageOf(truth, c.mm / 170.0, theta20 + 1.5 * wire_model::kSector);
        const ModelScore m = scoreFromModel(profile, fit, anchor, p);
        say(m.valid && m.ringWord == c.word && m.score == c.score,
            detail::fmt("%.0f", c.mm) + " mm planted reads " + m.score + " (ring '" + m.ringWord +
                "'), wanted " + c.score);
    }
    {
        const ModelScore t = scoreFromModel(profile, fit, anchor,
                                            imageOf(truth, 103.0 / 170.0, theta20 + 0.5 * wire_model::kSector));
        say(t.score == "T20", "mid-treble in the 20's own wedge reads T20 (got " + t.score + ")");
        const ModelScore d = scoreFromModel(profile, fit, anchor,
                                            imageOf(truth, 165.0 / 170.0, theta20 + 19.5 * wire_model::kSector));
        say(d.score == "D5", "mid-double in the last wedge before the 20 reads D5 (got " + d.score + ")");
    }

    // ---- wedge naming follows the store's own advance, both handednesses --------------
    bool forwardHolds = true;
    for (int i = 0; i < 20; i++)
    {
        const cv::Point2f p = imageOf(truth, 100.0 / 170.0, theta20 + (i + 0.5) * wire_model::kSector);
        const ModelScore m = scoreFromModel(profile, fit, anchor, p);
        if (m.segment != sequence[i])
        {
            forwardHolds = false;
            say(false, "forward wedge " + std::to_string(i) + " read " + m.score + ", wanted S" +
                           std::to_string(sequence[i]));
        }
    }
    say(forwardHolds, "all twenty wedges name the board sequence in the store's own direction");

    {
        // The mirrored store: the SAME comb lines fed in the order a mirrored camera
        // sorts them -- board angle FALLING with index. The anchor must read advance -1
        // and the sequence must advance the way the store does, or every wedge but the
        // 20 is wrong on a camera whose homography flips handedness.
        std::vector<cv::Point2f> mirrored;
        for (int k = 0; k < 20; k++)
        {
            mirrored.push_back(imageOf(truth, 1.0, comb - k * wire_model::kSector));
        }
        const ModelAnchor back = anchorOnBoard(fit, mirrored, 3);
        const double thetaBack20 = comb - 3 * wire_model::kSector;
        say(back.resolved && back.advance < 0.0, "a store advancing backwards reads advance -1");
        bool mirroredHolds = true;
        for (int i = 0; i < 20; i++)
        {
            const cv::Point2f p = imageOf(truth, 100.0 / 170.0, thetaBack20 - (i + 0.5) * wire_model::kSector);
            const ModelScore m = scoreFromModel(profile, fit, back, p);
            if (m.segment != sequence[i])
            {
                mirroredHolds = false;
                say(false, "mirrored wedge " + std::to_string(i) + " read " + m.score + ", wanted S" +
                               std::to_string(sequence[i]));
            }
        }
        say(mirroredHolds, "all twenty wedges name the board sequence against a mirrored store");
    }

    // ---- boundary distances, against arithmetic done here -----------------------------
    {
        // 103 mm is 4 mm from both treble edges; dead mid-wedge the sector boundary is
        // 103 * 9 degrees = 16.2 mm away, so the CALL's boundary is the ring's.
        const ModelScore m = scoreFromModel(profile, fit, anchor,
                                            imageOf(truth, 103.0 / 170.0, theta20 + 0.5 * wire_model::kSector));
        say(std::fabs(m.ringBoundaryMm - 4.0) < 1.0,
            "mid-treble ring boundary reads " + detail::fmt("%.2f", m.ringBoundaryMm) + " mm, wanted 4");
        say(std::fabs(m.wedgeBoundaryMm - 103.0 * wire_model::kSector / 2.0) < 1.5,
            "mid-wedge sector boundary reads " + detail::fmt("%.2f", m.wedgeBoundaryMm) + " mm, wanted 16.2");
        say(m.boundaryMm == m.ringBoundaryMm, "and the call's nearest boundary is the ring's");
    }
    {
        // 1.2 degrees short of the next wire at 100 mm: the wedge is the near boundary,
        // 100 * 1.2 degrees = 2.09 mm, against 1 mm... no: 100 mm is 1 mm from the 99 mm
        // treble edge, so the RING is still nearer. Read both, assert both numbers.
        const double off = 1.2 * CV_PI / 180.0;
        const ModelScore m = scoreFromModel(profile, fit, anchor,
                                            imageOf(truth, 100.0 / 170.0, theta20 + wire_model::kSector - off));
        say(std::fabs(m.wedgeBoundaryMm - 100.0 * off) < 0.7,
            "1.2 degrees from a wire at 100 mm reads " + detail::fmt("%.2f", m.wedgeBoundaryMm) +
                " mm of arc, wanted 2.09");
        say(std::fabs(m.ringBoundaryMm - 1.0) < 0.8,
            "100 mm sits " + detail::fmt("%.2f", m.ringBoundaryMm) + " mm from the treble's inner edge, wanted 1");
        say(m.boundaryMm == std::min(m.ringBoundaryMm, m.wedgeBoundaryMm),
            "the call's boundary is the nearer of the two");
    }
    {
        const ModelScore m = scoreFromModel(profile, fit, anchor, imageOf(truth, 5.0 / 170.0, 1.0));
        say(m.score == "BULL" && m.boundaryMm == m.ringBoundaryMm,
            "on the bull the call's boundary is the ring's alone -- no wedge can change a BULL");
        const ModelScore miss = scoreFromModel(profile, fit, anchor, imageOf(truth, 180.0 / 170.0, 1.0));
        say(miss.score == "MISS" && std::fabs(miss.ringBoundaryMm - 10.0) < 2.0,
            "10 mm past the scoring edge reads MISS with the edge " +
                detail::fmt("%.2f", miss.ringBoundaryMm) + " mm away");
    }

    // ---- no anchor means no wedge ------------------------------------------------------
    {
        const ModelAnchor none = anchorOnBoard(fit, endpoints, -1);
        say(!none.resolved, "a negative wire index resolves nothing");
        const ModelScore m = scoreFromModel(profile, fit, none,
                                            imageOf(truth, 100.0 / 170.0, theta20 + 0.5 * wire_model::kSector));
        say(m.score == "S?" && m.segment == -1 && !m.wedgeResolved,
            "unanchored, a single is S? and never an asserted 20 (got " + m.score + ")");
        say(m.wedgeBoundaryMm < 0.0 && m.boundaryMm == m.ringBoundaryMm,
            "... its wedge boundary is absent and the call's boundary is the ring's");
    }
    {
        const ModelScore m = scoreFromModel(profile, BoardFit(), ModelAnchor(), cv::Point2f(1.f, 1.f));
        say(!m.valid && m.score == "MISS", "an unbuilt fit answers invalid, not a guess");
    }

    std::cout << (failures == 0 ? "ALL OK" : "FAILURES: " + std::to_string(failures)) << std::endl;
    return failures == 0 ? 0 : 1;
}
