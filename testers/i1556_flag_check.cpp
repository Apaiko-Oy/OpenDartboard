// #1556: A SCORE WHOSE UNCERTAINTY CROSSES A WIRE SAYS SO -- the rule, held with geometry
// whose truth is KNOWN, and the publication contract held with no geometry at all.
//
// Two halves, and they are deliberately different kinds of thing:
//
//   THE MEASUREMENT (entry_intersection.hpp). A planted dart is put a KNOWN number of
//   millimetres from a KNOWN wire, through the same planted homographies
//   i1512_intersect_check.cpp uses -- three cameras with different centres, tilts and
//   rotations, one of them MIRRORED -- each fitted by fitBoardToCamera and anchored by
//   anchorOnBoard exactly as the pipeline fits and anchors real ones. So "3 mm from the
//   sector wire" is a fact about the plant, not a reading taken from the code under test.
//
//   THE PUBLICATION RULE (score_processing.hpp, `decideBoundaryCall`). Pure, over
//   primitives, and asserted with no solve behind it at all -- which is the point: what a
//   board may SAY about a crossing must be holdable without a board.
//
// WHAT IS ASSERTED, in the order it matters:
//
//   1. THE DIRECTION. A ring wire is crossed radially and a sector wire tangentially, so
//      the sigma that spends the demotion is the position sigma resolved along that
//      boundary's normal. `sigmaAlongDeg` is asserted against both axes of a planted
//      ellipse and against the circular case, and every solve is asserted to have picked
//      the boundary that is nearest IN SIGMAS -- not in millimetres, which is the whole
//      difference from the rule this replaces.
//   2. THE FLOOR. #1511's finding is that a claimed sigma understates the error a
//      reference can see, so resolving the ellipse may never claim a precision below the
//      measured floor. Asserted as a floor: no across-boundary sigma in any solve is
//      under it.
//   3. THE TWO CANDIDATES. A flagged dart names the score across the wire, read back
//      through the same fit and anchor -- T1 or S1 at the treble's outer edge, S1 or S20
//      three millimetres off a sector wire, BULL or OUTER inside the bull. And a BULL is
//      never flagged against a SECTOR wire however close one is, because no sector wire
//      can change what a bull says.
//   4. THE CONTRACT. A vote publish carries no millimetre uncertainty; a crossing whose
//      alternative cannot be named publishes UNFLAGGED and says so; the measurement is
//      published whether or not it flagged; and the confidence vocabulary is the one that
//      was already published -- no fourth number (#1489).
//   5. THE PINS. `OD_WIRE_FLAG=sigma-major` restores #1555's major-axis test on the same
//      binary and `OD_ENTRY_SIGMA=zero` is the mutation. Both may move the verdict and
//      NOTHING pure.
//
// Modes: `tree` (no pin) and `crude` (run under OD_WIRE_FLAG=sigma-major). Asking a pinned
// binary the tree's questions is the mutation proof, and testers/i1556_check.sh states its
// predicted failure count -- arithmetic on the `FLAG-SENSITIVE` line this check prints --
// before either mutation runs.
//
//   compiled by testers/i1556_check.sh with wire_model.cpp, for row 1512's reason:
//   entry_intersection.hpp includes board_model.hpp, whose fit calls into wire_model::
//   at link time.

#include <cmath>
#include <iostream>
#include <string>
#include <vector>

#include "detector/geometry/detection/entry_intersection.hpp"
#include "detector/geometry/detection/score_processing.hpp"

using namespace board_model;
using namespace entry_intersection;
using score_processing::BoundaryCall;
using score_processing::decideBoundaryCall;
using score_processing::geometricConfidence;

static int failures = 0;
static int flagSensitive = 0;

static void say(bool ok, const std::string &what)
{
    std::cout << (ok ? "OK   " : "FAIL ") << what << std::endl;
    if (!ok)
    {
        failures++;
    }
}

/** An assertion that a zeroed uncertainty MUST break: the mutation's own needle. */
static void sayFlag(bool ok, const std::string &what)
{
    flagSensitive++;
    say(ok, "flag: " + what);
}

/** An assertion that holds identically under every pin -- a pin may move a verdict and
 *  never a pure function (#1552's rule, and #1555's one file over). */
static void sayPure(bool ok, const std::string &what)
{
    say(ok, "pure: " + what);
}

static std::string fmt2(double v)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "%.2f", v);
    return buf;
}

// ---- the planted truth (i1512_intersect_check.cpp's, copied on purpose) ----------------
//
// Copied rather than shared for that file's own stated reason: the tester must not borrow
// the code it is checking, and a helper both checks import is a place one wrong sign
// passes twice.

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
    const cv::Vec3d q = truth.H * cv::Vec3d(unitRadius * std::cos(theta),
                                            unitRadius * std::sin(theta), 1.0);
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
        const double theta = mirrored ? comb - k * wire_model::kSector
                                      : comb + k * wire_model::kSector;
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

/** A canonical point at a stated radius and a stated number of SECTORS round from the
 *  20's first boundary -- the frame `solveEntry` reports in, so a plant is stated in the
 *  same words the verdict is. */
static cv::Point2f canonAt(double radiusMm, double sectors)
{
    const double phi = sectors * wire_model::kSector;
    return cv::Point2f((float)(radiusMm * std::cos(phi)), (float)(radiusMm * std::sin(phi)));
}

int main(int argc, char **argv)
{
    const std::string mode = argc > 1 ? argv[1] : "tree";
    if (mode != "tree" && mode != "crude")
    {
        std::cerr << "usage: i1556_flag_check [tree|crude]" << std::endl;
        return 2;
    }
    const bool expectCrude = mode == "crude";

    const BoardProfile profile = profileFromSpec(perspective_processing::DartboardSpec());
    const Params params;

    const Camera cam1 = makeCamera(640, 360, 300, 240, 25.0, 0.18, 0.7, 0.13, false);
    const Camera cam2 = makeCamera(590, 410, 270, 250, -40.0, 0.12, 2.1, 0.47, false);
    const Camera cam3 = makeCamera(700, 330, 280, 230, 110.0, 0.20, -1.2, 0.90, true);
    say(cam1.fit.accepted && cam2.fit.accepted && cam3.fit.accepted,
        "all three planted boards are ACCEPTED (the fit half is i1510_board_check's subject)");
    say(cam1.anchor.resolved && cam2.anchor.resolved && cam3.anchor.resolved,
        "all three anchors resolve, camera 3 mirrored");

    auto solveAt = [&](const cv::Point2f &C) -> EntrySolution
    {
        std::vector<CameraEvidence> ev = {lineEvidence(0, cam1, C, 10.0),
                                          lineEvidence(1, cam2, C, 70.0),
                                          lineEvidence(2, cam3, C, 130.0)};
        return solveEntry(profile, ev);
    };

    // ---- 1. the direction: the sigma is resolved ALONG the boundary's normal ------------
    {
        // The ellipse's own axes, asserted against the function that resolves it. A
        // 12 x 3 mm ellipse lying at 40 degrees reads 12 along 40, 3 along 130, and the
        // right thing in between; a circular one reads the same in every direction.
        const double along = detail::sigmaAlongDeg(12.0, 3.0, 40.0, 40.0);
        const double across = detail::sigmaAlongDeg(12.0, 3.0, 40.0, 130.0);
        const double mid = detail::sigmaAlongDeg(12.0, 3.0, 40.0, 85.0);
        sayPure(std::fabs(along - 12.0) < 1e-9,
                "the resolved sigma along the major axis IS the major axis (" + fmt2(along) + ")");
        sayPure(std::fabs(across - 3.0) < 1e-9,
                "and across it is the minor axis (" + fmt2(across) + ")");
        sayPure(mid > 3.0 && mid < 12.0,
                "and between them it is between them (" + fmt2(mid) + ")");
        sayPure(std::fabs(detail::sigmaAlongDeg(7.0, 7.0, 40.0, 0.0) - 7.0) < 1e-9 &&
                    std::fabs(detail::sigmaAlongDeg(7.0, 7.0, 40.0, 61.0) - 7.0) < 1e-9,
                "a circular uncertainty reads the same in every direction, which is the "
                "case where this rule and the one it replaces agree");
        sayPure(detail::sigmaAlongDeg(19.3, 6.6, 0.0, 90.0) < 19.3 / 2.0,
                "and a 19.3 x 6.6 mm ellipse -- rig-20260918's own worst solve -- claims "
                "6.6 across its minor axis where the rule this replaces claimed 19.3");
    }

    // ---- 2. a dart comfortably inside its segment is untouched --------------------------
    {
        // 80 mm at 1.5 sectors: 19 mm from the treble's inner wire and 12.6 mm of arc
        // from the nearer sector wire, so no honest sigma reaches either.
        const EntrySolution sol = solveAt(canonAt(80.0, 1.5));
        say(sol.solved && sol.score.score == "S1",
            "a dart mid-segment solves and scores S1 (" + sol.score.score + ")");
        say(sol.outcome == Outcome::Solved && !sol.uncertaintyCrossesWire,
            std::string("and is NOT flagged: ") + outcomeWord(sol.outcome));
        say(sol.alternativeScore.empty(),
            "and names no second candidate, because there is nothing to name");
        sayFlag(sol.boundaryAcrossMm > 5.0 && sol.crossingSigmas > 1.0,
            "its nearest boundary is " + fmt2(sol.boundaryAcrossMm) + " mm away, " +
                fmt2(sol.crossingSigmas) + " sigmas, and the measurement is published "
                                           "anyway -- how close a call was is worth "
                                           "answering when the answer is `not close`");
    }

    // ---- 3. three millimetres off a SECTOR wire: flagged, and the other number named ----
    {
        // 130 mm, 3.0 mm of arc past the wedge-1 boundary. 130 mm is a single, 23 mm from
        // the treble's outer wire and 32 from the double's inner one, so the RING cannot
        // be the near boundary -- which is what makes this a test of the wedge branch and
        // not of whichever branch happened to win.
        const double r = 130.0;
        const EntrySolution sol = solveAt(canonAt(r, 1.0 + 3.0 / (r * wire_model::kSector)));
        const std::string want = "S" + std::to_string(sequence[1]);
        const std::string other = "S" + std::to_string(sequence[0]);
        say(sol.solved && sol.score.score == want,
            "a dart 3 mm off a sector wire scores " + sol.score.score + " (want " + want + ")");
        sayFlag(sol.boundaryKind == "wedge",
                "the near boundary is the SECTOR wire, not the ring 23 mm away (" +
                    (sol.boundaryKind.empty() ? std::string("none") : sol.boundaryKind) + ")");
        sayFlag(std::fabs(sol.boundaryAcrossMm - 3.0) < 0.6,
                "measured at " + fmt2(sol.boundaryAcrossMm) + " mm, which is the plant");
        sayFlag(sol.uncertaintyCrossesWire,
                "and it is flagged (" + fmt2(sol.crossingSigmas) + " sigmas of clearance)");
        sayFlag(sol.alternativeScore == other,
                "naming the number on the other side of that wire: " + sol.alternativeScore +
                    " (want " + other + ")");
    }

    // ---- 4. two millimetres inside the treble's outer wire: the RING branch --------------
    {
        // 105 mm at 1.5 sectors: inside the 99-107 treble band, 2 mm from its outer edge,
        // and 16.5 mm of arc from either sector wire. The alternative is the SAME NUMBER
        // in a different band, which is the error class #1510 Phase 2 measured.
        const EntrySolution sol = solveAt(canonAt(105.0, 1.5));
        const std::string want = "T" + std::to_string(sequence[1]);
        const std::string other = "S" + std::to_string(sequence[1]);
        say(sol.solved && sol.score.score == want,
            "a dart 2 mm inside the treble's outer wire scores " + sol.score.score +
                " (want " + want + ")");
        sayFlag(sol.boundaryKind == "ring",
                "the near boundary is the RING wire (" +
                    (sol.boundaryKind.empty() ? std::string("none") : sol.boundaryKind) + ")");
        sayFlag(sol.uncertaintyCrossesWire && sol.alternativeScore == other,
                "flagged, and the other candidate is the same number one band out: " +
                    sol.alternativeScore + " (want " + other + ")");
    }

    // ---- 4b. THE DART THE TWO RULES DISAGREE ABOUT ----------------------------------------
    //
    // This is the plant the whole issue turns on, and it is the one a rule that could only
    // ever agree with the old one would not produce. TWO constraints crossing at 20
    // degrees make an error ellipse tens of millimetres long ALONG the badly-conditioned
    // direction and a couple of millimetres across it. The dart is placed so that long
    // axis runs down the wedge rather than across it: the sector wire is 8 mm away, well
    // inside the major axis -- so #1555's rule calls it uncertain -- and the uncertainty
    // pointing AT that wire is the floor, so the call clears it by more than a sigma.
    //
    // An uncertainty that does not point at a wire is not uncertainty about that wire.
    const double discR = 130.0;
    const cv::Point2f discAt = canonAt(discR, 1.0 + 8.0 / (discR * wire_model::kSector));
    EntrySolution disc;
    {
        std::vector<CameraEvidence> ev = {lineEvidence(0, cam1, discAt, 10.0),
                                          lineEvidence(1, cam2, discAt, 30.0)};
        disc = solveEntry(profile, ev);
        say(disc.solved && disc.bestPairAngleDeg < 35.0,
            "a two-constraint solve crossing at " + fmt2(disc.bestPairAngleDeg) +
                " deg is an elongated ellipse: " + fmt2(disc.sigmaMajorMm) + " x " +
                fmt2(disc.sigmaMinorMm) + " mm");
        std::cout << "DISCRIMINATOR score=" << disc.score.score
                  << " ring=" << fmt2(disc.score.ringBoundaryMm)
                  << " wedge=" << fmt2(disc.score.wedgeBoundaryMm)
                  << " sigmaMajor=" << fmt2(disc.sigmaMajorMm)
                  << " sigmaRadial=" << fmt2(disc.sigmaRadialMm)
                  << " sigmaTangent=" << fmt2(disc.sigmaTangentMm)
                  << " z=" << fmt2(disc.crossingSigmas)
                  << " crude=" << (disc.crudeWireClose ? 1 : 0)
                  << " across=" << (disc.uncertaintyCrossesWire ? 1 : 0) << std::endl;
        sayFlag(disc.crudeWireClose && !disc.uncertaintyCrossesWire,
                "and the two rules disagree about it: the major axis reaches the wire "
                "(crude=" + std::string(disc.crudeWireClose ? "1" : "0") +
                    ") and the uncertainty pointing at the wire does not (across=" +
                    std::string(disc.uncertaintyCrossesWire ? "1" : "0") + ")");
        sayFlag(disc.sigmaTangentMm < disc.sigmaMajorMm - 1e-9,
                "because the sigma across that wire is " + fmt2(disc.sigmaTangentMm) +
                    " mm where the major axis is " + fmt2(disc.sigmaMajorMm));
    }

    // ---- 5. a BULL is never flagged against a sector wire --------------------------------
    {
        // 3 mm from the centre, and deliberately a hair off a sector wire: the arc to it
        // is under half a millimetre. No sector wire can change what a bull says, so the
        // ring must be the boundary and OUTER must be the other candidate.
        const EntrySolution sol = solveAt(canonAt(3.0, 1.0 + 0.4 / (3.0 * wire_model::kSector)));
        say(sol.solved && sol.score.score == "BULL",
            "a dart 3 mm from the centre scores BULL (" + sol.score.score + ")");
        say(sol.score.wedgeBoundaryMm >= 0.0 && sol.score.wedgeBoundaryMm < 1.0,
            "with a sector wire " + fmt2(sol.score.wedgeBoundaryMm) +
                " mm away, which would be the nearest thing on the board by millimetres");
        sayFlag(sol.boundaryKind == "ring",
                "and the boundary measured to is the RING, because no sector wire can "
                "change what a bull says (" +
                    (sol.boundaryKind.empty() ? std::string("none") : sol.boundaryKind) + ")");
        sayFlag(sol.alternativeScore == "OUTER",
                "so the other candidate is OUTER, not a number: " + sol.alternativeScore);
    }

    // ---- 6. a MISS well past the board is not a close call --------------------------------
    {
        const EntrySolution sol = solveAt(canonAt(200.0, 1.5));
        say(sol.solved && sol.score.score == "MISS",
            "a dart 30 mm past the double's outer edge scores MISS (" + sol.score.score + ")");
        say(!sol.uncertaintyCrossesWire && sol.alternativeScore.empty(),
            "and is not flagged, 30 mm being a long way in sigmas");
    }

    // ---- 7. the invariants every solve must satisfy ----------------------------------------
    {
        // Over a sweep rather than over the five plants above: the rule must hold where
        // nobody wrote an assertion, and a rule that only holds at the points a tester
        // chose is a rule fitted to a tester.
        // TWO constraints crossing at 25 degrees, deliberately: that is an ellipse whose
        // major axis is well above the floor, which is the only geometry in which
        // resolving it can be shown to do anything at all. Three exact constraints make a
        // nearly circular one a couple of millimetres wide, where the floor swallows the
        // whole question and the old rule and the new one cannot differ.
        int solves = 0, floorHeld = 0, pickedBySigma = 0, boundedByMajor = 0, sharper = 0;
        for (int i = 0; i < 20; i++)
        {
            for (int k = 0; k < 6; k++)
            {
                const double r = 20.0 + k * 28.0;
                const cv::Point2f C = canonAt(r, i + 0.11 + 0.13 * k);
                std::vector<CameraEvidence> ev = {lineEvidence(0, cam1, C, 10.0 + i * 3.0),
                                                  lineEvidence(1, cam2, C, 35.0 + i * 3.0)};
                const EntrySolution sol = solveEntry(profile, ev);
                if (!sol.solved || !sol.score.valid)
                {
                    continue;
                }
                solves++;
                // Read off the PIN rather than off the mode: under the mutation the floor
                // really is zero, and an invariant that is true of the mutated binary must
                // not be one of the assertions the mutation is counted by.
                const double floorMm = entrySigmaIsZeroed() ? 0.0 : params.sigmaAcrossFloorMm;
                if (sol.sigmaRadialMm >= floorMm - 1e-9 && sol.sigmaTangentMm >= floorMm - 1e-9)
                {
                    floorHeld++;
                }
                if (sol.sigmaRadialMm <= sol.sigmaMajorMm + 1e-9 ||
                    sol.sigmaRadialMm <= floorMm + 1e-9)
                {
                    boundedByMajor++;
                }
                if (sol.sigmaMajorMm > floorMm && sol.sigmaRadialMm < sol.sigmaMajorMm - 1e-9)
                {
                    sharper++;
                }
                // The crossing is the boundary nearest IN SIGMAS. Recomputed here from
                // the solution's own reported numbers, which is the half a reader of the
                // census has to be able to do.
                const double zRing = sol.sigmaRadialMm > 0.0
                                         ? sol.score.ringBoundaryMm / sol.sigmaRadialMm
                                         : -1.0;
                const bool wedgeFlips = sol.score.segment >= 1 && sol.score.wedgeBoundaryMm >= 0.0;
                const double zWedge = (wedgeFlips && sol.sigmaTangentMm > 0.0)
                                          ? sol.score.wedgeBoundaryMm / sol.sigmaTangentMm
                                          : -1.0;
                std::string want;
                if (zRing >= 0.0 && (zWedge < 0.0 || zRing <= zWedge))
                {
                    want = "ring";
                }
                else if (zWedge >= 0.0)
                {
                    want = "wedge";
                }
                if (sol.boundaryKind == want)
                {
                    pickedBySigma++;
                }
            }
        }
        say(solves >= 100, "the sweep solved " + std::to_string(solves) + " planted darts");
        say(floorHeld == solves,
            "every across-boundary sigma is at or above the measured floor (" +
                std::to_string(floorHeld) + "/" + std::to_string(solves) + ")");
        say(boundedByMajor == solves,
            "and none exceeds the major axis it was resolved from (" +
                std::to_string(boundedByMajor) + "/" + std::to_string(solves) + ")");
        say(pickedBySigma == solves,
            "and the boundary measured to is the one nearest IN SIGMAS on every one (" +
                std::to_string(pickedBySigma) + "/" + std::to_string(solves) + ")");
        sayFlag(sharper >= solves / 4,
                "and on " + std::to_string(sharper) + " of them the resolved sigma is "
                "strictly SHARPER than the major axis -- the repair does something, which "
                "a rule that could only ever agree with the old one would not");
    }

    // ---- 8. the publication contract, with no geometry behind it ---------------------------
    {
        const BoundaryCall vote = decideBoundaryCall(false, true, "S7", "S19", "wedge", 1.2, 6.0);
        sayPure(!vote.flagged && vote.alternative.empty(),
                "a VOTE publish is never flagged: nothing in a string vote measures a "
                "board-millimetre position");
        sayPure(vote.boundaryMm < 0.0f && vote.uncertaintyMm < 0.0f,
                "and it publishes no millimetres either, rather than a made-up zero");
        sayPure(vote.account.empty(),
                "and says nothing about a crossing, because none was measured");
    }
    {
        const BoundaryCall clear = decideBoundaryCall(true, false, "S19", "", "wedge", 12.4, 5.0);
        sayPure(!clear.flagged && clear.alternative.empty(),
                "a geometric publish that clears every wire is not flagged");
        sayPure(clear.boundaryMm > 0.0f && clear.uncertaintyMm > 0.0f,
                "and DOES publish its millimetres: how close a call was is worth answering "
                "when the answer is `not close`");
        sayPure(clear.account.find("clears") != std::string::npos,
                "and the account says so in words");
    }
    {
        const BoundaryCall flagged = decideBoundaryCall(true, true, "S19", "S7", "wedge", 1.2, 6.0);
        sayPure(flagged.flagged && flagged.published == "S19" && flagged.alternative == "S7",
                "a crossing with a nameable second candidate is flagged and names both");
        sayPure(flagged.account.find("S19 or S7") != std::string::npos,
                "and the account names both in the order they publish -- the more probable "
                "one first (#1557: it publishes now, a tap appends the other)");
        sayPure(flagged.account.find("tap") != std::string::npos,
                "and says what a tap does, because publishing immediately is the decision");
    }
    {
        const BoundaryCall unnameable =
            decideBoundaryCall(true, true, "BULL", "", "ring", 0.4, 5.0);
        const BoundaryCall same = decideBoundaryCall(true, true, "BULL", "BULL", "ring", 0.4, 5.0);
        sayPure(!unnameable.flagged && !same.flagged,
                "a crossing whose alternative cannot be named -- or is the published score "
                "again -- publishes UNFLAGGED: a flag with one name in it has nothing for a "
                "consumer to ask about");
        sayPure(unnameable.account.find("no second candidate") != std::string::npos,
                "and the account says the crossing was measured and could not be named, "
                "rather than reading like a comfortable call");
    }
    {
        sayPure(geometricConfidence(true) == 0.7f && geometricConfidence(false) == 0.9f,
                "the confidences are the two that were already published -- no fourth "
                "number was invented to mean `flagged` (#1489)");
        const BoundaryCall a = decideBoundaryCall(true, true, "T20", "S20", "ring", 0.9, 5.0);
        const BoundaryCall b = decideBoundaryCall(true, false, "T20", "", "ring", 9.9, 5.0);
        sayPure(a.flagged && !b.flagged && a.alternative != b.alternative,
                "so the flag is a FIELD and the two readings differ in it, not in a float");
    }

    // ---- 9. the pins, and what they may move ------------------------------------------------
    say(crudeWireTestIsPinned() == expectCrude,
        std::string("the sigma-major pin reads as ") + (expectCrude ? "SET" : "unset") +
            " in this run, which is what this mode was started for");
    say(entrySigmaIsZeroed() == false,
        "the zeroed-uncertainty pin reads as unset in this run, so the numbers above are "
        "about a solve that has an uncertainty to spend");
    {
        // THE DISCRIMINATOR, asked what the published verdict follows. It is the one plant
        // on which the answer differs, which is what makes this assertion a needle: on a
        // dart both rules agree about, a pin that changed nothing would pass.
        const bool published = disc.outcome == Outcome::UncertainAcrossWire;
        say(published == (expectCrude ? disc.crudeWireClose : disc.uncertaintyCrossesWire),
            std::string("the published verdict on the discriminator follows the ") +
                (expectCrude ? "major-axis rule the pin restored" : "across-boundary rule") +
                " (outcome " + outcomeWord(disc.outcome) + ")");
    }

    std::cout << "FLAG-SENSITIVE n=" << flagSensitive << " mode=" << mode << std::endl;
    std::cout << (failures ? "FAILURES: " : "ALL OK: ") << failures << std::endl;
    return failures ? 1 : 0;
}
