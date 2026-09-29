// #1681: the redundancy number of each line in the entry solve, and the tip it asks for
// where the lines cannot check themselves -- held with geometry whose truth is KNOWN.
//
// The truth helpers below are i1512_intersect_check.cpp's (themselves i1510's), copied on
// purpose: a tester must not borrow the code it checks.
//
// PREDICTIONS, STATED FIRST:
//   1. three well-conditioned lines (10/70/130 deg) through one point: every redundancy
//      number >= 0.1, so the solve is CONTROLLED and publishes with no tip at all;
//   2. rig-20260929 window 22's shape: two lines crossing each other at 10 deg through
//      the true entry, the third on ANOTHER dart displaced 40 mm along their shared band.
//      The chi-square cannot see it (the solve is SOLVED, not INCONSISTENT) -- that is
//      the fault -- and the third line's redundancy reads under 0.1, so with no placed
//      tip the verdict is controlRefused;
//   3. the same with a tip at the TRUE entry (40 mm from the solve): still refused --
//      a tip corroborates the POSITION, and nothing sits at this one;
//   4. the same with a tip at the solved point: corroborated, not refused;
//   5. a two-line solve is uncontrolled by construction: refused without a tip, not
//      refused with one at the entry;
//   6. mutation: Params::minRedundancy = 0 refuses nothing, so the bound is what acts.
//
//   compiled by unit_check.sh (row 1681) with wire_model.cpp, like row 1512.

#include <cmath>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "detector/geometry/detection/entry_intersection.hpp"

using namespace board_model;
using namespace entry_intersection;

static int failures = 0;

static void say(bool ok, const std::string &what)
{
    std::cout << (ok ? "OK   " : "FAIL ") << what << std::endl;
    if (!ok)
    {
        failures++;
    }
}

static std::string fmt1(double v)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "%.2f", v);
    return buf;
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

/** A whole synthetic calibration from the truth: every observation, none of the map.
 *  `mirrored` reverses the endpoint store's walk, which is how a handedness-flipping
 *  camera arrives at anchorOnBoard in the real pipeline. */
static DartboardCalibration calibrationSeenThrough(const Truth &truth, double comb, bool mirrored)
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
        const double theta = mirrored ? comb - k * wire_model::kSector : comb + k * wire_model::kSector;
        calib.wires.wireEndpoints.add(imageOf(truth, 1.0, theta));
    }
    calib.wires.wiresDetected = 20;
    calib.wires.isValid = true;

    calib.orientation.anchored = true;
    calib.orientation.wedge20WireIndex = 3;
    calib.orientation.wedgeNumber = 6;
    calib.orientation.cameraPosition = orientation_processing::CameraPosition::CONFIGURED;
    return calib;
}

// The board's sequence, restated so the expectation is not read off the code under test.
static const int sequence[20] = {20, 1, 18, 4, 13, 6, 10, 15, 2, 17, 3, 19, 7, 16, 8, 11, 14, 9, 12, 5};

// ---- one planted camera, fitted and anchored as the pipeline would ---------------------

struct Camera
{
    Truth truth;
    DartboardCalibration calib;
    BoardFit fit;
    ModelAnchor anchor;
};

static Camera makeCamera(double cx, double cy, double semiA, double semiB, double phiDeg,
                         double tiltR, double tiltArg, double comb, bool mirrored)
{
    Camera cam;
    cam.truth = makeTruth(cx, cy, semiA, semiB, phiDeg, tiltR, tiltArg);
    cam.calib = calibrationSeenThrough(cam.truth, comb, mirrored);
    const BoardProfile profile = profileFromSpec(perspective_processing::DartboardSpec());
    cam.fit = fitBoardToCamera(profile, cam.calib);
    std::vector<cv::Point2f> endpoints(cam.calib.wires.wireEndpoints.begin(),
                                       cam.calib.wires.wireEndpoints.end());
    cam.anchor = anchorOnBoard(cam.fit, endpoints, cam.calib.orientation.wedge20WireIndex);
    return cam;
}

/** A planted board line through canonical point C at canonical tangent psi, offered as
 *  this camera's axis observation. `lateralMm` shifts the line off C -- the planted
 *  displaced-shadow / wrong-line fault. Optional image-space noise on top. */
static CameraEvidence lineEvidence(int index, const Camera &cam, cv::Point2f C, double psiDeg,
                                   double lateralMm = 0.0, double noiseDeg = 0.0,
                                   double noisePx = 0.0)
{
    const double psi = psiDeg * CV_PI / 180.0;
    const cv::Point2f t((float)std::cos(psi), (float)std::sin(psi));
    const cv::Point2f n((float)-std::sin(psi), (float)std::cos(psi));
    const cv::Point2f base(C.x + n.x * (float)lateralMm, C.y + n.y * (float)lateralMm);
    const cv::Point2f A(base.x - t.x * 60.f, base.y - t.y * 60.f);
    const cv::Point2f B(base.x + t.x * 60.f, base.y + t.y * 60.f);
    const cv::Point2f imgA = entry_intersection::detail::imageOfCanonical(cam.fit, cam.anchor, A);
    const cv::Point2f imgB = entry_intersection::detail::imageOfCanonical(cam.fit, cam.anchor, B);
    cv::Point2f d = imgB - imgA;
    const float len = (float)std::sqrt((double)d.x * d.x + (double)d.y * d.y);
    d *= 1.0f / len;
    if (noiseDeg != 0.0)
    {
        const double a = noiseDeg * CV_PI / 180.0;
        d = cv::Point2f((float)(d.x * std::cos(a) - d.y * std::sin(a)),
                        (float)(d.x * std::sin(a) + d.y * std::cos(a)));
    }
    CameraEvidence ev;
    ev.camera = index;
    ev.fit = &cam.fit;
    ev.anchor = cam.anchor;
    ev.axisValid = true;
    // The support centroid sits up the shaft, not at the entry: 40 mm along the line.
    const cv::Point2f up(base.x + t.x * 40.f, base.y + t.y * 40.f);
    ev.axisPoint = entry_intersection::detail::imageOfCanonical(cam.fit, cam.anchor, up);
    ev.axisPoint += cv::Point2f((float)(noisePx * d.y), (float)(-noisePx * d.x)); // lateral px shove
    ev.axisDir = d;
    ev.axisSigmaDeg = 0.5; // deliberately optimistic: the floor must take over
    return ev;
}

static void plantTip(CameraEvidence &ev, const Camera &cam, cv::Point2f canonicalMm)
{
    ev.tipFound = true;
    ev.tipImage = entry_intersection::detail::imageOfCanonical(cam.fit, cam.anchor, canonicalMm);
}

static double distMm(cv::Point2f a, cv::Point2f b)
{
    return std::sqrt((double)(a.x - b.x) * (a.x - b.x) + (double)(a.y - b.y) * (a.y - b.y));
}

int main()
{
    const BoardProfile profile = profileFromSpec(perspective_processing::DartboardSpec());
    const Camera cam1 = makeCamera(640, 360, 300, 240, 25.0, 0.18, 0.7, 0.13, false);
    const Camera cam2 = makeCamera(590, 410, 270, 250, -40.0, 0.12, 2.1, 0.47, false);
    const Camera cam3 = makeCamera(700, 330, 280, 230, 110.0, 0.20, -1.2, 0.90, true);
    say(cam1.fit.accepted && cam2.fit.accepted && cam3.fit.accepted && cam1.anchor.resolved &&
            cam2.anchor.resolved && cam3.anchor.resolved,
        "three planted boards accepted and anchored");

    // ---- 1. controlled: three well-conditioned lines, no tips ------------------------
    {
        const double phi = 1.5 * wire_model::kSector;
        const cv::Point2f C((float)(80.0 * std::cos(phi)), (float)(80.0 * std::sin(phi)));
        std::vector<CameraEvidence> ev = {lineEvidence(0, cam1, C, 10.0),
                                          lineEvidence(1, cam2, C, 70.0),
                                          lineEvidence(2, cam3, C, 130.0)};
        const EntrySolution sol = solveEntry(profile, ev);
        say(sol.solved && sol.usableConstraints == 3, "well-conditioned three lines solve");
        say(sol.minRedundancy >= 0.1 && !sol.uncontrolled,
            "every line is checked by the others: min redundancy " + fmt1(sol.minRedundancy) +
                " (want >= 0.10)");
        double sum = 0.0;
        for (const Constraint &con : sol.constraints)
        {
            sum += con.redundancy;
        }
        say(std::fabs(sum - 1.0) < 0.02,
            "the redundancy numbers sum to the solve's one degree of freedom: " + fmt1(sum));
        say(!sol.controlRefused && sol.tipWitnesses == 0,
            "and with no tip at all it is NOT refused: a controlled solve needs no corroboration");
        say(censusControlLine(sol, 1).find("uncontrolled=0 refused=0") != std::string::npos,
            "the census line says so: " + censusControlLine(sol, 1));
    }

    // ---- 2-4. window 22's shape: two shallow lines on one dart, a third on another ----
    const double phi5 = 19.5 * wire_model::kSector; // mid-wedge, canonical
    const cv::Point2f C((float)(120.0 * std::cos(phi5)), (float)(120.0 * std::sin(phi5)));
    const double band = 45.0; // the two shallow lines' mean tangent
    const cv::Point2f along((float)std::cos(band * CV_PI / 180.0), (float)std::sin(band * CV_PI / 180.0));
    const cv::Point2f other(C.x + 40.f * along.x, C.y + 40.f * along.y); // the other dart
    auto w22 = [&]()
    {
        return std::vector<CameraEvidence>{lineEvidence(0, cam1, C, band - 5.0),
                                           lineEvidence(1, cam2, C, band + 5.0),
                                           lineEvidence(2, cam3, other, band + 90.0)};
    };
    cv::Point2f solved;
    {
        const EntrySolution sol = solveEntry(profile, w22());
        solved = sol.entryMm;
        say(sol.solved && sol.outcome != Outcome::Inconsistent && sol.usableConstraints == 3,
            std::string("THE FAULT: the chi-square passes a line on another dart (") +
                outcomeWord(sol.outcome) + ", entry " + fmt1(distMm(sol.entryMm, C)) +
                " mm from the true one)");
        say(distMm(sol.entryMm, C) > 25.0, "and the solve followed the third line off the dart");
        say(sol.uncontrolled && sol.leastControlledCamera == 3 && sol.constraints[2].redundancy < 0.1,
            "camera 3 is the uncontrolled line: redundancy " + fmt1(sol.constraints[2].redundancy));
        say(sol.controlRefused, "no placed tip, so the solve is refused: " + sol.controlStory);
        say(censusControlLine(sol, 22).find("cam=3") != std::string::npos &&
                censusControlLine(sol, 22).find("refused=1") != std::string::npos,
            "the census line names it: " + censusControlLine(sol, 22));
    }
    {
        std::vector<CameraEvidence> ev = w22();
        plantTip(ev[0], cam1, C);
        plantTip(ev[1], cam2, C);
        const EntrySolution sol = solveEntry(profile, ev);
        say(sol.tipWitnesses == 2 && sol.tipCorroborations == 0 && sol.controlRefused,
            "tips at the TRUE entry, " + fmt1(sol.nearestTipMm) +
                " mm from the solve, do not corroborate it: still refused");
    }
    {
        std::vector<CameraEvidence> ev = w22();
        plantTip(ev[2], cam3, solved);
        const EntrySolution sol = solveEntry(profile, ev);
        say(sol.uncontrolled && sol.tipCorroborations == 1 && !sol.controlRefused,
            "a tip at the solved point corroborates it: uncontrolled but NOT refused");
    }

    // ---- 5. a two-line solve is uncontrolled by construction -------------------------
    {
        std::vector<CameraEvidence> ev = {lineEvidence(0, cam1, C, 20.0),
                                          lineEvidence(2, cam3, C, 100.0)};
        const EntrySolution bare = solveEntry(profile, ev);
        say(bare.solved && bare.uncontrolled && bare.minRedundancy < 1e-6 && bare.controlRefused,
            "two lines: redundancy " + fmt1(bare.minRedundancy) + ", refused with no tip");
        plantTip(ev[0], cam1, C);
        const EntrySolution tipped = solveEntry(profile, ev);
        say(tipped.solved && tipped.uncontrolled && !tipped.controlRefused,
            "two lines and a tip at the entry: corroborated, not refused");
    }

    // ---- 6. mutation: the bound at zero refuses nothing -----------------------------
    {
        Params p;
        p.minRedundancy = 0.0;
        const EntrySolution sol = solveEntry(profile, w22(), p);
        say(!sol.uncontrolled && !sol.controlRefused,
            "MUTATION minRedundancy=0: window 22's shape is no longer refused, so the bound acts");
    }

    // ---- the switch is off unless asked for -----------------------------------------
    {
        const char *e = std::getenv("OD_SOLVE_CONTROL");
        const bool want = e != nullptr && std::string(e) == "on";
        say(solveControlIsOn() == want, std::string("OD_SOLVE_CONTROL reads ") +
                                            (want ? "on" : "off") + " as set");
    }

    std::cout << (failures == 0 ? "ALL OK" : "FAILURES: " + std::to_string(failures)) << std::endl;
    return failures == 0 ? 0 : 1;
}
