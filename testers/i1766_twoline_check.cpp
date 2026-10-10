// #1766: what a TWO-LINE solve claims across the wire it is nearest to, where the claim
// comes from, and what the publication says about it -- held with geometry whose truth
// is KNOWN. The live shape (2026-10-08, line 2367: "S3 clears its nearest wedge wire --
// 17.9 mm away across a 16.5 mm one-sigma", a two-line solve of the log's cameras 2 and
// 3) is rebuilt on planted boards and the rules that decided it are asserted by name.
//
// The truth helpers below are i1681_control_check.cpp's (themselves i1512's and i1510's),
// copied on purpose: a tester must not borrow the code it checks.
//
// PREDICTIONS, STATED FIRST:
//   1. THE SIGMA IS THE CROSSING. The same dart, the same two cameras, the same line
//      sigmas: lines crossing at 27 deg give a major-axis sigma 2.4x the one lines
//      crossing at 70 deg give, and in both cases the claimed major axis is what the two
//      lines' own sigmaPerps and the crossing angle compute (A^-1 diag(s^2) A^-T) to 2%.
//      Nothing about the dart is in that number.
//   2. THE LIVE SHAPE. Lines crossing at 27 deg, tangential to the board at radius 126 mm,
//      so the ellipse's major axis lies ACROSS the wedge wires: the across-wire sigma
//      reads 12-20 mm (live: 16.5). A dart placed 1.08 of that sigma from the nearest
//      wedge wire is SOLVED, not flagged, under Params::crossingSigmas = 1.0 -- that is
//      the figure the live sentence was read against, and it is asserted to be 1.0 by
//      name -- and reads WIRE-UNCERTAIN under 1.25, so the threshold is where the line is
//      and the sigma is what it measures. The sibling at 0.3 sigma is flagged.
//   3. THE SENTENCE SAYS WHOSE SIGMA IT IS (#1766's one change). `decideBoundaryCall`
//      given `sigmaProvenance(sol)` appends "a two-line solve of cameras N and M crossing
//      at P deg" to the clear sentence AND the flagged one; a controlled three-line solve
//      hands it an empty provenance and the sentences are byte-for-byte #1556's.
//   4. OD_SOLVE_CONTROL's verdict on it: two lines are uncontrolled by construction
//      (redundancy 0 on both), refused with no placed tip, corroborated with one at the
//      entry -- which is why the switch moved nothing on the fixtures (30 of 30 two-line
//      solves tip-corroborated on the bakeoff at 8406446) and cannot be shown to have moved the live eight.
//   5. The census line a replay prints for it says usable=2 uncontrolled=1.
//
//   compiled by unit_check.sh (row 1766) with wire_model.cpp, like rows 1512 and 1681.

#include <cmath>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "detector/geometry/detection/entry_intersection.hpp"
#include "detector/geometry/detection/score_processing.hpp"

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

static std::string fmt2(double v)
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
 *  this camera's axis observation (i1681's, unchanged). */
static CameraEvidence lineEvidence(int index, const Camera &cam, cv::Point2f C, double psiDeg)
{
    const double psi = psiDeg * CV_PI / 180.0;
    const cv::Point2f t((float)std::cos(psi), (float)std::sin(psi));
    const cv::Point2f A(C.x - t.x * 60.f, C.y - t.y * 60.f);
    const cv::Point2f B(C.x + t.x * 60.f, C.y + t.y * 60.f);
    const cv::Point2f imgA = entry_intersection::detail::imageOfCanonical(cam.fit, cam.anchor, A);
    const cv::Point2f imgB = entry_intersection::detail::imageOfCanonical(cam.fit, cam.anchor, B);
    cv::Point2f d = imgB - imgA;
    const float len = (float)std::sqrt((double)d.x * d.x + (double)d.y * d.y);
    d *= 1.0f / len;
    CameraEvidence ev;
    ev.camera = index;
    ev.fit = &cam.fit;
    ev.anchor = cam.anchor;
    ev.axisValid = true;
    const cv::Point2f up(C.x + t.x * 40.f, C.y + t.y * 40.f);
    ev.axisPoint = entry_intersection::detail::imageOfCanonical(cam.fit, cam.anchor, up);
    ev.axisDir = d;
    ev.axisSigmaDeg = 0.5; // deliberately optimistic: the floor must take over
    return ev;
}

static void plantTip(CameraEvidence &ev, const Camera &cam, cv::Point2f canonicalMm)
{
    ev.tipFound = true;
    ev.tipImage = entry_intersection::detail::imageOfCanonical(cam.fit, cam.anchor, canonicalMm);
}

/** The major-axis sigma of a solve of exactly two lines whose normals cross at pairDeg,
 *  from A^-1 diag(s1^2, s2^2) A^-T -- the census's own arithmetic, restated here so the
 *  expectation is not read off the solver. */
static double twoLineMajor(double s1, double s2, double pairDeg)
{
    const double t = pairDeg * CV_PI / 180.0;
    const double st = std::sin(t);
    const double a10 = -std::cos(t) / st, a11 = 1.0 / st;
    const double c00 = s1 * s1;
    const double c01 = a10 * s1 * s1;
    const double c11 = a10 * a10 * s1 * s1 + a11 * a11 * s2 * s2;
    const double tr = c00 + c11, det = c00 * c11 - c01 * c01;
    return std::sqrt(tr / 2.0 + std::sqrt(std::max(0.0, tr * tr / 4.0 - det)));
}

/** The two used lines' sigmaPerps, in constraint order. */
static std::vector<double> usedSigmas(const EntrySolution &sol)
{
    std::vector<double> s;
    for (const Constraint &con : sol.constraints)
    {
        if (con.usable && !con.excluded)
        {
            s.push_back(con.sigmaPerpMm);
        }
    }
    return s;
}

int main()
{
    const BoardProfile profile = profileFromSpec(perspective_processing::DartboardSpec());
    const double kSectorDeg = wire_model::kSector * 180.0 / CV_PI; // kSector is radians
    const Camera cam1 = makeCamera(640, 360, 300, 240, 25.0, 0.18, 0.7, 0.13, false);
    const Camera cam2 = makeCamera(590, 410, 270, 250, -40.0, 0.12, 2.1, 0.47, false);
    const Camera cam3 = makeCamera(700, 330, 280, 230, 110.0, 0.20, -1.2, 0.90, true);
    say(cam1.fit.accepted && cam2.fit.accepted && cam3.fit.accepted && cam1.anchor.resolved &&
            cam2.anchor.resolved && cam3.anchor.resolved,
        "three planted boards accepted and anchored");

    // A point in the single area at the live radius (0.741 x 170 = 126 mm), mid-wedge in
    // canonical angle (wedge wires sit at k x 18 deg), with the two lines TANGENTIAL to the
    // board there, so their shared direction -- the ellipse's major axis -- lies across the
    // wedge wires, as the live 2+3 pair's did at the bottom of the board.
    const double R = 126.0;
    auto pointAt = [&](double canonDeg)
    {
        const double a = canonDeg * CV_PI / 180.0;
        return cv::Point2f((float)(R * std::cos(a)), (float)(R * std::sin(a)));
    };
    auto twoLines = [&](double canonDeg, double pairDeg)
    {
        const double band = canonDeg + 90.0; // tangential
        return std::vector<CameraEvidence>{lineEvidence(1, cam2, pointAt(canonDeg), band - pairDeg / 2.0),
                                           lineEvidence(2, cam3, pointAt(canonDeg), band + pairDeg / 2.0)};
    };
    const double mid = 11.0 * kSectorDeg - 9.0; // mid-wedge, 9 deg from each wire

    // ---- 1. the sigma is the crossing ------------------------------------------------
    double sigmaAt27 = 0.0, sigmaAt70 = 0.0;
    {
        const EntrySolution shallow = solveEntry(profile, twoLines(mid, 27.0));
        const EntrySolution wide = solveEntry(profile, twoLines(mid, 70.0));
        say(shallow.solved && wide.solved && shallow.usableConstraints == 2 && wide.usableConstraints == 2,
            "the same dart solves from the same two cameras at 27 and at 70 deg");
        say(std::fabs(shallow.bestPairAngleDeg - 27.0) < 2.0 && std::fabs(wide.bestPairAngleDeg - 70.0) < 2.0,
            "and the solver reads the crossing angles back: " + fmt2(shallow.bestPairAngleDeg) + " and " +
                fmt2(wide.bestPairAngleDeg) + " deg");
        const std::vector<double> sS = usedSigmas(shallow), sW = usedSigmas(wide);
        const double predS = twoLineMajor(sS[0], sS[1], shallow.bestPairAngleDeg);
        const double predW = twoLineMajor(sW[0], sW[1], wide.bestPairAngleDeg);
        say(std::fabs(shallow.sigmaMajorMm - predS) / predS < 0.02,
            "27 deg: the claimed major axis " + fmt2(shallow.sigmaMajorMm) + " mm is the two lines' own (" +
                fmt2(sS[0]) + ", " + fmt2(sS[1]) + " mm) and the crossing: " + fmt2(predS) + " mm");
        say(std::fabs(wide.sigmaMajorMm - predW) / predW < 0.02,
            "70 deg: the claimed major axis " + fmt2(wide.sigmaMajorMm) + " mm is the two lines' own (" +
                fmt2(sW[0]) + ", " + fmt2(sW[1]) + " mm) and the crossing: " + fmt2(predW) + " mm");
        const double ratio = shallow.sigmaMajorMm / wide.sigmaMajorMm;
        // equal sigmas: sin(35)/sin(13.5) = 2.46; the lines' sigmas differ a little
        say(ratio > 2.2 && ratio < 2.8,
            "so the same dart's sigma is " + fmt2(ratio) + "x larger at 27 deg than at 70: the crossing's, not the dart's");
        sigmaAt27 = shallow.sigmaTangentMm;
        sigmaAt70 = wide.sigmaTangentMm;
        say(sigmaAt27 > 9.0 && sigmaAt27 < 20.0,
            "and across the wedge wire it reads " + fmt2(sigmaAt27) + " mm at 27 deg (live: 12.2-16.5 from 5-6 mm lines at 25-37 deg) against " +
                fmt2(sigmaAt70) + " at 70 deg (the fixtures: 5.0-9.3)");
    }

    // ---- 2. the live shape: 1.08 sigma from the wire reads SOLVED under 1.0 ----------
    Params defaults;
    say(std::fabs(defaults.crossingSigmas - 1.0) < 1e-9,
        "Params::crossingSigmas is 1.0: a call clears a wire at one across-wire sigma (#1556), the figure the live sentence was read against");
    const double zLive = 17.9 / 16.5; // line 2367's own numbers
    say(zLive > defaults.crossingSigmas && zLive < 1.25,
        "line 2367's 17.9 mm across 16.5 mm is " + fmt2(zLive) + " sigma: past 1.0 by eight hundredths, under 1.25");
    {
        // The same geometry, the dart moved to 1.08 of the across-wire sigma from the wire
        // at 11 x 18 deg: canonical angle 198 - asin(1.08 sigma / R).
        const double dLive = 1.08 * sigmaAt27;
        const double canon = 11.0 * kSectorDeg - std::asin(dLive / R) * 180.0 / CV_PI;
        const EntrySolution sol = solveEntry(profile, twoLines(canon, 27.0));
        say(sol.solved && sol.boundaryKind == "wedge",
            "the dart planted " + fmt2(dLive) + " mm from a wedge wire at canonical " + fmt2(canon) + " deg solves at phi " + fmt2(sol.phiDeg) + ", r " + fmt2(sol.radiusMm) + " (" + sol.score.score + ", wedge wire " + fmt2(sol.score.wedgeBoundaryMm) + " mm): its nearest call-flipping wire is that wedge wire (" +
                fmt2(sol.boundaryAcrossMm) + " mm across a " + fmt2(sol.sigmaAcrossMm) + " mm sigma)");
        say(sol.crossingSigmas > 1.0 && sol.crossingSigmas < 1.2,
            "at " + fmt2(sol.crossingSigmas) + " sigma");
        say(sol.outcome == Outcome::Solved && !sol.uncertaintyCrossesWire,
            std::string("it reads ") + outcomeWord(sol.outcome) + " under the default: the figure is 1.0 and this is past it");
        say(sol.uncontrolled && sol.minRedundancy < 1e-6,
            "and it is uncontrolled: redundancy " + fmt2(sol.minRedundancy) + " on both lines");
        // the sentence, with #1766's provenance
        const std::string prov = sigmaProvenance(sol);
        say(prov.find("a two-line solve of cameras 2 and 3 crossing at") != std::string::npos &&
                prov.find("#1766") != std::string::npos,
            "sigmaProvenance names the cameras and the crossing: " + prov);
        const score_processing::BoundaryCall clear = score_processing::decideBoundaryCall(
            true, sol.uncertaintyCrossesWire, sol.score.score, sol.alternativeScore, sol.boundaryKind,
            sol.boundaryAcrossMm, sol.sigmaAcrossMm, prov);
        say(!clear.flagged && clear.account.find("clears its nearest wedge wire") != std::string::npos &&
                clear.account.find("(a two-line solve of cameras 2 and 3 crossing at") != std::string::npos,
            "the clear sentence says whose sigma it is: " + clear.account);
        // the figure moved: 1.25 flags the same dart
        Params p;
        p.crossingSigmas = 1.25;
        const EntrySolution at125 = solveEntry(profile, twoLines(canon, 27.0), p);
        say(at125.outcome == Outcome::UncertainAcrossWire && at125.uncertaintyCrossesWire,
            std::string("under crossingSigmas 1.25 the same dart reads ") + outcomeWord(at125.outcome) +
                ": the threshold is where the line is, the sigma is what it measures");
    }
    {
        // the sibling: 0.3 sigma from the wire (line 822: 3.7 across 12.2)
        const double dSib = 0.3 * sigmaAt27;
        const double canon = 11.0 * kSectorDeg - std::asin(dSib / R) * 180.0 / CV_PI;
        const EntrySolution sol = solveEntry(profile, twoLines(canon, 27.0));
        say(sol.solved && sol.outcome == Outcome::UncertainAcrossWire && !sol.alternativeScore.empty(),
            "the sibling at " + fmt2(sol.crossingSigmas) + " sigma is flagged, " + sol.score.score + " or " +
                sol.alternativeScore);
        const score_processing::BoundaryCall flagged = score_processing::decideBoundaryCall(
            true, sol.uncertaintyCrossesWire, sol.score.score, sol.alternativeScore, sol.boundaryKind,
            sol.boundaryAcrossMm, sol.sigmaAcrossMm, sigmaProvenance(sol));
        say(flagged.flagged && flagged.account.find("is flagged; a tap affirms it or appends the other (a two-line solve of cameras 2 and 3 crossing at") != std::string::npos,
            "and its flagged sentence carries the same provenance: " + flagged.account);
    }

    // ---- 3. a controlled solve's sentence is #1556's, byte for byte --------------------
    {
        const cv::Point2f C = pointAt(mid);
        std::vector<CameraEvidence> ev = {lineEvidence(0, cam1, C, mid + 10.0),
                                          lineEvidence(1, cam2, C, mid + 70.0),
                                          lineEvidence(2, cam3, C, mid + 130.0)};
        const EntrySolution three = solveEntry(profile, ev);
        say(three.solved && three.usableConstraints == 3 && !three.uncontrolled,
            "three well-conditioned lines: controlled, min redundancy " + fmt2(three.minRedundancy));
        say(sigmaProvenance(three).empty(), "and sigmaProvenance is empty for it");
        const score_processing::BoundaryCall a = score_processing::decideBoundaryCall(
            true, false, "S3", "", "wedge", 17.9, 5.0, sigmaProvenance(three));
        const score_processing::BoundaryCall b = score_processing::decideBoundaryCall(
            true, false, "S3", "", "wedge", 17.9, 5.0);
        say(a.account == b.account && a.account == "UNCERTAINTY: S3 clears its nearest wedge wire -- 17.9 mm away across a 5.0 mm one-sigma",
            "so its sentence is #1556's unchanged: " + a.account);
        const score_processing::BoundaryCall vote = score_processing::decideBoundaryCall(
            false, true, "S3", "S19", "wedge", 3.7, 12.2, "a two-line solve of cameras 2 and 3 crossing at 27.0 deg");
        say(vote.account.empty() && !vote.flagged, "and a vote publish stays silent whatever provenance it is handed (rule 1)");
    }

    // ---- 4. the control verdict on a two-line solve ------------------------------------
    {
        std::vector<CameraEvidence> ev = twoLines(mid, 27.0);
        const EntrySolution bare = solveEntry(profile, ev);
        say(bare.solved && bare.uncontrolled && bare.controlRefused,
            "two lines and no placed tip: uncontrolled AND refused -- " + bare.controlStory);
        plantTip(ev[0], cam2, pointAt(mid));
        const EntrySolution tipped = solveEntry(profile, ev);
        say(tipped.solved && tipped.uncontrolled && !tipped.controlRefused && tipped.tipCorroborations == 1,
            "two lines and a tip at the entry: uncontrolled, corroborated, NOT refused -- which is every fixture two-line solve (30 of 30 on the bakeoff at 8406446)");
        say(!solveControlIsOn() || std::getenv("OD_SOLVE_CONTROL") != nullptr,
            "OD_SOLVE_CONTROL is opt-in: it reads on only when set");
        // ---- 5. the census line a replay prints ----------------------------------------
        const std::string line = censusControlLine(bare, 2367);
        say(line.find("usable=2") != std::string::npos && line.find("uncontrolled=1 refused=1") != std::string::npos,
            "the census line says usable=2 uncontrolled=1 refused=1: " + line);
    }

    std::cout << (failures == 0 ? "ALL OK" : "FAILURES: " + std::to_string(failures)) << std::endl;
    return failures == 0 ? 0 : 1;
}
