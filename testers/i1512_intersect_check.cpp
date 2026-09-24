// #1512: the entry-point intersection, held with geometry whose truth is KNOWN.
//
// The truth helpers are i1510_board_check.cpp's, copied on purpose (its own stated
// reason: the tester must not borrow the code it is checking). Three planted cameras
// with different centres, tilts and rotations -- one of them MIRRORED, because the
// canonical-frame unification is the thing under test and handedness is where it
// breaks silently -- each fitted by fitBoardToCamera and anchored by anchorOnBoard,
// exactly as the pipeline fits and anchors real ones.
//
// Every planted dart is a BOARD LINE through a known canonical entry point: two
// canonical points on the line, mapped through the fit into the image, and the image
// line between them handed to the solver as an axis observation -- the same class of
// object a real shaft projects to. The solver must transport it back, place it in the
// shared numbered frame, and intersect.
//
// The issue's own required mutations, PREDICTIONS STATED FIRST:
//   - wrong correspondences (two cameras' lines swapped) must be DETECTABLE: with a
//     third honest camera, the consistency check refuses or excludes; with two cameras
//     and honest tips, corroboration reads 0 and the nearest tip is far.
//   - poor calibration (a ring scaled 6%) must be DETECTABLE: the fit itself rejects
//     on its held-out bands, the camera is excluded by name, and the solve refuses
//     with too few constraints.
//   - strings agreeing must move NOTHING: two tips that each read T20 in their own
//     cameras, planted against geometry that says S5, change neither the entry nor
//     the sigma by one byte relative to the same solve with no tips at all.
//
//   compiled by unit_check.sh (row 1512) with wire_model.cpp, like rows 1510/1510p2.

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

static CameraEvidence refusedEvidence(int index, const Camera &cam, const std::string &why)
{
    CameraEvidence ev;
    ev.camera = index;
    ev.fit = &cam.fit;
    ev.anchor = cam.anchor;
    ev.axisValid = false;
    ev.axisRefusal = why;
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

    // Three cameras, three geometries; camera 3 mirrored. Combs and anchors differ so
    // nothing agrees by coincidence of frames.
    const Camera cam1 = makeCamera(640, 360, 300, 240, 25.0, 0.18, 0.7, 0.13, false);
    const Camera cam2 = makeCamera(590, 410, 270, 250, -40.0, 0.12, 2.1, 0.47, false);
    const Camera cam3 = makeCamera(700, 330, 280, 230, 110.0, 0.20, -1.2, 0.90, true);
    say(cam1.fit.accepted && cam2.fit.accepted && cam3.fit.accepted,
        "all three planted boards are ACCEPTED (the fit half is i1510_board_check's subject)");
    say(cam1.anchor.resolved && cam2.anchor.resolved && cam3.anchor.resolved,
        "all three anchors resolve");
    say(cam3.anchor.advance < 0.0, "camera 3's store advances mirrored (advance -1)");

    // ---- 1. exact recovery, three cameras, the mirrored one among them ----------------
    {
        // Entry mid-single of the wedge one past the 20: canonical (80 mm, 1.5 sectors)
        // = S1, and 80 mm sits 19 mm from the treble's inner wire and 12.6 mm of arc
        // from the nearer sector wire, so no honest sigma reaches a boundary here.
        // Line tangents 10 / 70 / 130 degrees: well conditioned.
        const double phi = 1.5 * wire_model::kSector;
        const cv::Point2f C((float)(80.0 * std::cos(phi)), (float)(80.0 * std::sin(phi)));
        std::vector<CameraEvidence> ev = {lineEvidence(0, cam1, C, 10.0),
                                          lineEvidence(1, cam2, C, 70.0),
                                          lineEvidence(2, cam3, C, 130.0)};
        const EntrySolution sol = solveEntry(profile, ev);
        say(sol.outcome == Outcome::Solved && sol.solved,
            std::string("three exact constraints solve (got ") + outcomeWord(sol.outcome) + ")");
        say(distMm(sol.entryMm, C) < 1.5,
            "the entry lands on the plant: " + fmt1(distMm(sol.entryMm, C)) + " mm off (want < 1.5)");
        say(sol.usableConstraints == 3, "all three constraints used");
        say(sol.score.valid && sol.score.score == "S1",
            "scored once through the board profile: " + sol.score.score + " (want S1)");
        say(sol.scoresAgree && sol.scoreByCamera == "S1/S1/S1",
            "every camera's anchor reads the same point the same way: " + sol.scoreByCamera);
        say(sol.constraints[2].usable && std::fabs(sol.constraints[2].residualMm) < 1.0,
            "the mirrored camera's constraint is in the shared frame (residual " +
                fmt1(sol.constraints[2].residualMm) + " mm)");
    }

    // ---- 2. every wedge, pairs drawn across handedness --------------------------------
    {
        bool allWedges = true;
        for (int i = 0; i < 20; i++)
        {
            const double phi = (i + 0.5) * wire_model::kSector;
            const cv::Point2f C((float)(130.0 * std::cos(phi)), (float)(130.0 * std::sin(phi)));
            std::vector<CameraEvidence> ev = {lineEvidence(0, cam1, C, 20.0 + i * 7.0),
                                              lineEvidence(2, cam3, C, 80.0 + i * 7.0)};
            const EntrySolution sol = solveEntry(profile, ev);
            const std::string want = "S" + std::to_string(sequence[i]);
            if (!(sol.solved && sol.score.score == want && distMm(sol.entryMm, C) < 1.5))
            {
                allWedges = false;
                say(false, "wedge " + std::to_string(i) + " read " + sol.score.score +
                               " at " + fmt1(distMm(sol.entryMm, C)) + " mm, wanted " + want);
            }
        }
        say(allWedges, "all twenty wedges solve and score across a mirrored pair");
    }

    // ---- 3. oblique lines and image noise ---------------------------------------------
    {
        // 2 degrees of direction noise and 3 px of lateral shove per camera, oblique
        // tangents. The floors say a few millimetres of error is expected; 10 mm is
        // the assertion, and sigma must confess at least a millimetre.
        const cv::Point2f C(40.f, 85.f);
        std::vector<CameraEvidence> ev = {lineEvidence(0, cam1, C, 155.0, 0.0, 2.0, 3.0),
                                          lineEvidence(1, cam2, C, 35.0, 0.0, -2.0, -3.0),
                                          lineEvidence(2, cam3, C, 95.0, 0.0, 1.5, 2.0)};
        const EntrySolution sol = solveEntry(profile, ev);
        say(sol.solved && distMm(sol.entryMm, C) < 10.0,
            "noisy oblique constraints still land within 10 mm (" +
                fmt1(distMm(sol.entryMm, C)) + " mm, " + outcomeWord(sol.outcome) + ")");
        say(sol.sigmaMajorMm > 0.5 && sol.sigmaMajorMm < 25.0,
            "and the claimed sigma is a real number, not bravado: " + fmt1(sol.sigmaMajorMm) + " mm");
    }

    // ---- 4. named refusals: too few, near-parallel ------------------------------------
    {
        const cv::Point2f C(40.f, 85.f);
        std::vector<CameraEvidence> ev = {lineEvidence(0, cam1, C, 10.0),
                                          refusedEvidence(1, cam2, "not straight: planted"),
                                          refusedEvidence(2, cam3, "support floor: planted")};
        const EntrySolution sol = solveEntry(profile, ev);
        say(sol.outcome == Outcome::TooFewConstraints && !sol.solved,
            std::string("one usable constraint refuses by name (got ") + outcomeWord(sol.outcome) + ")");
        say(sol.story.find("not straight: planted") != std::string::npos &&
                sol.story.find("support floor: planted") != std::string::npos,
            "and the story carries each camera's own exclusion");
    }
    {
        const cv::Point2f C(-60.f, 30.f);
        std::vector<CameraEvidence> ev = {lineEvidence(0, cam1, C, 40.0),
                                          lineEvidence(1, cam2, C, 44.0)};
        const EntrySolution sol = solveEntry(profile, ev);
        say(sol.outcome == Outcome::NearParallel && !sol.solved,
            std::string("a 4-degree crossing refuses as near-parallel (got ") + outcomeWord(sol.outcome) + ")");
        say(sol.story.find("4.0 deg") != std::string::npos ||
                sol.story.find("3.9 deg") != std::string::npos ||
                sol.story.find("4.1 deg") != std::string::npos,
            "and the story states the measured crossing angle: " + sol.story);
    }

    // ---- 5. the displaced line (finding one's shadow) is excluded BY NAME -------------
    {
        // PREDICTION, stated before the run: camera 2's line is planted 30 mm off the
        // entry laterally -- the shadow-displaced axis #1511 measured -- and cameras
        // 1 and 3 carry honest tips at the entry. The chi-square must refuse the
        // joint solve, the leave-one-out corroborated by those tips must name camera
        // 2 uniquely, and the remaining pair must solve within 2 mm.
        const cv::Point2f C(70.f, -40.f);
        CameraEvidence a = lineEvidence(0, cam1, C, 5.0);
        CameraEvidence b = lineEvidence(1, cam2, C, 65.0, 30.0);
        CameraEvidence c = lineEvidence(2, cam3, C, 125.0);
        plantTip(a, cam1, C);
        plantTip(c, cam3, C);
        std::vector<CameraEvidence> ev = {a, b, c};
        const EntrySolution sol = solveEntry(profile, ev);
        say(sol.solved && sol.constraints[1].excluded,
            std::string("the displaced line is excluded (") + outcomeWord(sol.outcome) + ")");
        say(sol.constraints[1].exclusion.find("inconsistent") != std::string::npos,
            "with the disagreement named: " + sol.constraints[1].exclusion);
        say(std::fabs(std::fabs(sol.constraints[1].residualMm) - 30.0) < 3.0,
            "and the residual is the planted 30 mm (read " +
                fmt1(std::fabs(sol.constraints[1].residualMm)) + ")");
        say(distMm(sol.entryMm, C) < 2.0,
            "the two honest cameras solve the entry: " + fmt1(distMm(sol.entryMm, C)) + " mm off");

        // The SAME plant with no tips anywhere: three symmetric lines cannot name a
        // liar (each vertex of the triangle is equally far from the opposite line),
        // so the honest verdict is INCONSISTENT rather than a coin-flip exclusion.
        std::vector<CameraEvidence> bare = {lineEvidence(0, cam1, C, 5.0),
                                            lineEvidence(1, cam2, C, 65.0, 30.0),
                                            lineEvidence(2, cam3, C, 125.0)};
        const EntrySolution sol2 = solveEntry(profile, bare);
        say(sol2.outcome == Outcome::Inconsistent && !sol2.solved,
            std::string("without tip evidence the same plant refuses as inconsistent (got ") +
                outcomeWord(sol2.outcome) + ")");
    }

    // ---- 6. three-way disagreement is INCONSISTENT, not a compromise ------------------
    {
        // PREDICTION: three lines displaced +20, -20, +20 mm agree pairwise nowhere,
        // so at least two residuals exceed their sigmas at the joint solve and the
        // verdict is INCONSISTENT with no entry asserted.
        const cv::Point2f C(10.f, 90.f);
        std::vector<CameraEvidence> ev = {lineEvidence(0, cam1, C, 0.0, 20.0),
                                          lineEvidence(1, cam2, C, 60.0, -20.0),
                                          lineEvidence(2, cam3, C, 120.0, 20.0)};
        const EntrySolution sol = solveEntry(profile, ev);
        say(sol.outcome == Outcome::Inconsistent && !sol.solved,
            std::string("mutual disagreement refuses as inconsistent (got ") + outcomeWord(sol.outcome) + ")");
    }

    // ---- 7. the wire band is named, never averaged over --------------------------------
    {
        // 1.5 mm inside the treble's inner wire, mid-wedge of the 20: S20 with
        // ringBoundary 1.5 mm. The claimed sigma at clean plants runs ~2.4 mm
        // (measured by this check), so the boundary sits INSIDE the sigma and the
        // outcome must say WIRE-UNCERTAIN while still stating the score and both
        // numbers -- phase 2's T14 band, reached from the model side.
        const double phi = 0.5 * wire_model::kSector;
        const cv::Point2f C((float)(97.5 * std::cos(phi)), (float)(97.5 * std::sin(phi)));
        std::vector<CameraEvidence> ev = {lineEvidence(0, cam1, C, 15.0),
                                          lineEvidence(1, cam2, C, 75.0),
                                          lineEvidence(2, cam3, C, 135.0)};
        const EntrySolution sol = solveEntry(profile, ev);
        say(sol.solved && sol.score.score == "S20",
            "1.5 mm inside the treble reads S20 (got " + sol.score.score + ")");
        say(sol.outcome == Outcome::UncertainAcrossWire,
            std::string("and the outcome names the wire (got ") + outcomeWord(sol.outcome) + ")");
        say(sol.score.boundaryMm < sol.sigmaMajorMm + 0.01,
            "because the boundary (" + fmt1(sol.score.boundaryMm) + " mm) sits inside sigma (" +
                fmt1(sol.sigmaMajorMm) + " mm)");
        // The same entry pushed to the middle of the single band must NOT name a wire.
        const cv::Point2f Cmid((float)(80.0 * std::cos(phi)), (float)(80.0 * std::sin(phi)));
        std::vector<CameraEvidence> ev2 = {lineEvidence(0, cam1, Cmid, 15.0),
                                           lineEvidence(1, cam2, Cmid, 75.0),
                                           lineEvidence(2, cam3, Cmid, 135.0)};
        const EntrySolution sol2 = solveEntry(profile, ev2);
        say(sol2.outcome == Outcome::Solved && sol2.score.score == "S20",
            "mid-band the same wedge is plain SOLVED S20 (got " +
                std::string(outcomeWord(sol2.outcome)) + " " + sol2.score.score + ")");
    }

    // ---- 8. a genuine off-board hit is a MISS by radius, not a failed lookup ----------
    {
        const cv::Point2f C(120.f, 130.f); // r = 176.9 mm: past the doubles wire
        std::vector<CameraEvidence> ev = {lineEvidence(0, cam1, C, 25.0),
                                          lineEvidence(1, cam2, C, 85.0)};
        const EntrySolution sol = solveEntry(profile, ev);
        say(sol.solved && sol.score.valid && sol.score.score == "MISS",
            "an entry at 176.9 mm scores MISS (got " + sol.score.score + ")");
        say(std::fabs(sol.radiusMm - 176.9) < 2.0,
            "with the radius saying how far off the board: " + fmt1(sol.radiusMm) + " mm");
    }

    // ---- 9. MUTATION: wrong correspondences must be detectable ------------------------
    {
        // PREDICTION: cam1 and cam2's image lines swapped between their fits, cam3
        // honest. The swapped lines transport to lines through nothing in particular,
        // so the joint solve cannot explain at least two constraints: INCONSISTENT
        // (or an exclusion) -- never a clean solve on the planted entry.
        const cv::Point2f C(55.f, 55.f);
        CameraEvidence a = lineEvidence(0, cam1, C, 10.0);
        CameraEvidence b = lineEvidence(1, cam2, C, 70.0);
        std::swap(a.axisPoint, b.axisPoint);
        std::swap(a.axisDir, b.axisDir);
        std::vector<CameraEvidence> ev = {a, b, lineEvidence(2, cam3, C, 130.0)};
        const EntrySolution sol = solveEntry(profile, ev);
        const bool cleanOnPlant = sol.outcome == Outcome::Solved && distMm(sol.entryMm, C) < 5.0 &&
                                  sol.usableConstraints == 3;
        say(!cleanOnPlant,
            std::string("swapped correspondences cannot pass as a clean 3-camera solve (got ") +
                outcomeWord(sol.outcome) + ", " + fmt1(sol.solved ? distMm(sol.entryMm, C) : -1.0) +
                " mm off the plant)");

        // Two cameras only, swapped, honest tips at the true entry: the solve lands
        // somewhere, and the tips refuse to corroborate it.
        CameraEvidence a2 = lineEvidence(0, cam1, C, 10.0);
        CameraEvidence b2 = lineEvidence(1, cam2, C, 70.0);
        std::swap(a2.axisPoint, b2.axisPoint);
        std::swap(a2.axisDir, b2.axisDir);
        plantTip(a2, cam1, C);
        plantTip(b2, cam2, C);
        std::vector<CameraEvidence> ev2 = {a2, b2};
        const EntrySolution sol2 = solveEntry(profile, ev2);
        say(!sol2.solved || (sol2.tipWitnesses == 2 && sol2.tipCorroborations == 0),
            "with two cameras the honest tips expose the swap: " +
                std::to_string(sol2.tipCorroborations) + "/" + std::to_string(sol2.tipWitnesses) +
                " corroborate, nearest " + fmt1(sol2.nearestTipMm) + " mm");
    }

    // ---- 10. MUTATION: poor calibration must be detectable ----------------------------
    {
        // PREDICTION: camera 2's treble pair scaled by 6% breaks the held-out band
        // check inside fitBoardToCamera, so the camera is excluded by name and a
        // two-camera event on it refuses with too few constraints.
        DartboardCalibration bent = cam2.calib;
        bent.ellipses.outerTripleEllipse.size.width *= 1.06f;
        bent.ellipses.outerTripleEllipse.size.height *= 1.06f;
        bent.ellipses.innerTripleEllipse.size.width *= 1.06f;
        bent.ellipses.innerTripleEllipse.size.height *= 1.06f;
        const BoardFit bentFit = fitBoardToCamera(profile, bent);
        say(!bentFit.accepted, "the 6% treble scaling is REJECTED by the fit's held-out bands");
        Camera bentCam = cam2;
        bentCam.fit = bentFit;
        const cv::Point2f C(55.f, 55.f);
        std::vector<CameraEvidence> ev = {lineEvidence(0, cam1, C, 10.0),
                                          lineEvidence(1, bentCam, C, 70.0)};
        const EntrySolution sol = solveEntry(profile, ev);
        say(sol.outcome == Outcome::TooFewConstraints,
            std::string("and the solve refuses rather than intersecting through it (got ") +
                outcomeWord(sol.outcome) + ")");
        say(sol.story.find("no accepted board fit") != std::string::npos,
            "naming the missing fit: " + sol.story);
    }

    // ---- 11. #1505's control: strings agreeing move NOTHING ---------------------------
    {
        // PREDICTION: two cameras' tips each read T20 in their own frame -- the score
        // strings AGREE -- while both planted lines say the dart entered at S5 on the
        // other side of the board. The solver answers S5 from geometry; and against
        // the identical solve with NO tips at all, neither the entry nor the sigma
        // moves by a byte, because string agreement is not an input the solver has.
        const double phiS5 = 19.5 * wire_model::kSector;
        const cv::Point2f C((float)(130.0 * std::cos(phiS5)), (float)(130.0 * std::sin(phiS5)));
        const double phiT20 = 0.4 * wire_model::kSector;
        const cv::Point2f t20a((float)(103.0 * std::cos(phiT20)), (float)(103.0 * std::sin(phiT20)));
        const double phiT20b = 0.7 * wire_model::kSector;
        const cv::Point2f t20b((float)(103.0 * std::cos(phiT20b)), (float)(103.0 * std::sin(phiT20b)));

        CameraEvidence a = lineEvidence(0, cam1, C, 30.0);
        CameraEvidence b = lineEvidence(1, cam2, C, 100.0);
        plantTip(a, cam1, t20a);
        plantTip(b, cam2, t20b);
        // The premise first: each tip really reads T20 through its own camera.
        const board_model::ModelScore sa = scoreFromModel(profile, cam1.fit, cam1.anchor, a.tipImage);
        const board_model::ModelScore sb = scoreFromModel(profile, cam2.fit, cam2.anchor, b.tipImage);
        say(sa.score == "T20" && sb.score == "T20",
            "the premise holds: both planted tips read T20 in their own cameras (" +
                sa.score + ", " + sb.score + ")");

        std::vector<CameraEvidence> withTips = {a, b};
        const EntrySolution agree = solveEntry(profile, withTips);
        say(agree.solved && agree.score.score == "S5",
            "geometry outvotes the agreeing strings: " + agree.score.score + " (want S5)");
        say(agree.tipWitnesses == 2 && agree.tipCorroborations == 0,
            "and neither T20 tip corroborates the solved entry (" +
                std::to_string(agree.tipCorroborations) + "/2, nearest " +
                fmt1(agree.nearestTipMm) + " mm)");

        CameraEvidence a2 = lineEvidence(0, cam1, C, 30.0);
        CameraEvidence b2 = lineEvidence(1, cam2, C, 100.0);
        std::vector<CameraEvidence> noTips = {a2, b2};
        const EntrySolution bare = solveEntry(profile, noTips);
        say(agree.entryMm == bare.entryMm && agree.sigmaMajorMm == bare.sigmaMajorMm &&
                agree.sigmaMinorMm == bare.sigmaMinorMm &&
                agree.score.score == bare.score.score,
            "agreeing strings changed neither entry nor sigma nor score by a byte");
    }

    // ---- 12. a stale anchor is a named disagreement, not a silent averaging -----------
    {
        // PREDICTION: camera 2's SOLVER anchor is one wire stale while the planted
        // line is the true camera's -- so the transport rotates the real line 18
        // degrees about the board centre, displacing it at the entry by
        // 2*r*sin(9) * sin(tangent - chord) ~ 25 mm at a tangent perpendicular to
        // the chord, the chi-square refuses the joint solve, and the honest tips on
        // cameras 1 and 3 name camera 2. The gate's sensitivity is RECORDED here
        // rather than hidden: at this fixture's sigmas a stale-anchor displacement
        // of ~19 mm dilutes to chi2 ~7.6 against the 9 and rides through -- the
        // chi-square catches the fault at 25 mm, not at every size of it.
        //
        // Two findings from this test's own first drafts, kept so nobody re-walks
        // them: (a) planting AND solving through the same stale anchor cancels
        // exactly, so the plant must use the true anchor; (b) a line whose tangent
        // lies along the rotation's chord at C (~60 deg here) is nearly invariant
        // under the stale rotation -- the first draft's 65-degree tangent moved only
        // 2 mm -- so the planted tangent must stand off the chord direction.
        Camera off = cam2;
        std::vector<cv::Point2f> endpoints(off.calib.wires.wireEndpoints.begin(),
                                           off.calib.wires.wireEndpoints.end());
        off.anchor = anchorOnBoard(off.fit, endpoints, 4); // planted truth says 3
        const cv::Point2f C(70.f, -40.f);
        CameraEvidence a = lineEvidence(0, cam1, C, 30.0);
        CameraEvidence b = lineEvidence(1, cam2, C, 150.0); // the TRUE camera's line...
        b.anchor = off.anchor;                              // ...transported by the stale frame
        CameraEvidence c = lineEvidence(2, cam3, C, 90.0);
        plantTip(a, cam1, C);
        plantTip(c, cam3, C);
        std::vector<CameraEvidence> ev = {a, b, c};
        const EntrySolution sol = solveEntry(profile, ev);
        const bool cleanThrough = sol.outcome == Outcome::Solved && sol.usableConstraints == 3;
        say(!cleanThrough,
            std::string("a one-wire-stale anchor cannot ride a clean 3-camera solve (got ") +
                outcomeWord(sol.outcome) + ", " + std::to_string(sol.usableConstraints) + " used)");
        if (sol.solved)
        {
            say(sol.constraints[1].excluded,
                "the stale camera is the one excluded: " + sol.constraints[1].exclusion);
            say(distMm(sol.entryMm, C) < 2.0,
                "while the honest pair solves the entry: " + fmt1(distMm(sol.entryMm, C)) + " mm off");
        }
    }

    std::cout << (failures == 0 ? "ALL OK" : "FAILURES: " + std::to_string(failures)) << std::endl;
    return failures == 0 ? 0 : 1;
}
