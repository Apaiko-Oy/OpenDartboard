// turnaus#1909: OD_SISAL_MM's slide, held pure. The acceptance's plant: a known axis at 45
// degrees to the board and d = 2 moves the entry 2.0 mm radially, the way the dart travels;
// the sign flipped fails. Held twice -- on sisal_slide's arithmetic, and through solveEntry
// itself on three planted PINHOLE cameras (f 800 px, principal point the 1280x720 frame's
// centre) whose boards are fitted and anchored the way the pipeline does it, so the camera
// centres, the axis and the slide are all recovered from what a calibration really holds.
//
// The planted-board helpers are i1911_sigma_check.cpp's (i1512's, copied on purpose: a
// tester must not borrow the code it checks); the truth here is a pinhole's H instead of an
// ellipse's. Each label is what i1909_sisal_check.sh's mutations predict red:
//
//   slide:   sisal_slide::slideOf's arithmetic      centre:  a camera centre from H alone
//   solve:   the slid entry through solveEntry      off:     sisalMm 0 leaves the solve as it was
//
//   compiled by i1909_sisal_check.sh with wire_model.cpp, like row 1911.

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

static std::string fmt2(double v)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "%.3f", v);
    return buf;
}

// ---- the planted truth (i1510_board_check.cpp's, copied on purpose) --------------------

struct Truth
{
    cv::Matx33d H; // board plane (unit circle = 170 mm) -> image
};

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

// ---- one planted camera, fitted and anchored as the pipeline would ---------------------

struct Camera
{
    Truth truth;
    DartboardCalibration calib;
    BoardFit fit;
    ModelAnchor anchor;
};

// ---- the planted pinholes -----------------------------------------------------------------

static constexpr double kF = 800.0, kCx = 640.0, kCy = 360.0;

struct Pinhole
{
    cv::Matx33d R; // rows: the camera's axes in board coordinates
    cv::Vec3d t;
    cv::Vec3d centre;
};

static Pinhole pinholeAt(const cv::Vec3d &centre)
{
    cv::Vec3d z = -centre * (1.0 / cv::norm(centre));
    cv::Vec3d x(z[1], -z[0], 0.0);
    x *= 1.0 / cv::norm(x);
    const cv::Vec3d y = z.cross(x);
    Pinhole p;
    p.R = cv::Matx33d(x[0], x[1], x[2], y[0], y[1], y[2], z[0], z[1], z[2]);
    p.t = -(p.R * centre);
    p.centre = centre;
    return p;
}

static cv::Point2f project(const Pinhole &p, const cv::Vec3d &X)
{
    const cv::Vec3d c = p.R * X + p.t;
    return cv::Point2f((float)(kF * c[0] / c[2] + kCx), (float)(kF * c[1] / c[2] + kCy));
}

/** board plane, unit circle = 170 mm -> image: K [r1 r2 t] diag(170, 170, 1). */
static Truth truthOf(const Pinhole &p)
{
    const cv::Matx33d K(kF, 0, kCx, 0, kF, kCy, 0, 0, 1);
    const cv::Matx33d M(p.R(0, 0) * 170.0, p.R(0, 1) * 170.0, p.t[0],
                        p.R(1, 0) * 170.0, p.R(1, 1) * 170.0, p.t[1],
                        p.R(2, 0) * 170.0, p.R(2, 1) * 170.0, p.t[2]);
    Truth truth;
    truth.H = K * M;
    return truth;
}

/** The canonical point of a board-frame point, read the way the pipeline reads one. */
static cv::Point2f canonicalOf(const Camera &cam, const Pinhole &p, const cv::Vec3d &X)
{
    return entry_intersection::detail::canonOf(cam.anchor, boardPointOf(cam.fit, project(p, X)));
}

static CameraEvidence shaftEvidence(int index, const Camera &cam, const Pinhole &p, const cv::Vec3d &entry,
                                    const cv::Vec3d &travel)
{
    const cv::Point2f a = project(p, entry - travel * 20.0), b = project(p, entry - travel * 70.0);
    cv::Point2f d = b - a;
    d *= 1.0f / (float)std::sqrt((double)d.x * d.x + (double)d.y * d.y);
    CameraEvidence ev;
    ev.camera = index;
    ev.fit = &cam.fit;
    ev.anchor = cam.anchor;
    ev.axisValid = true;
    ev.axisPoint = project(p, entry - travel * 45.0);
    ev.axisDir = d;
    ev.axisSigmaDeg = 0.5;
    ev.imageSize = cv::Size(1280, 720);
    return ev;
}

static double radialAt(cv::Point2f move, cv::Point2f at)
{
    const double r = std::sqrt((double)at.x * at.x + (double)at.y * at.y);
    return (move.x * at.x + move.y * at.y) / r;
}

int main()
{
    // ---- slide: the arithmetic -------------------------------------------------------------
    {
        const double s45 = std::sqrt(0.5);
        const cv::Point2d m = sisal_slide::slideOf(cv::Vec3d(s45, 0.0, -s45), 2.0);
        say(std::fabs(m.x - 2.0) < 1e-9 && std::fabs(m.y) < 1e-9,
            "slide: a 45-degree axis leaning +x and d = 2 slides (" + fmt2(m.x) + ", " + fmt2(m.y) +
                ") mm, 2.0 the way it travels");
        const cv::Point2d sq = sisal_slide::slideOf(cv::Vec3d(0.0, 0.0, -1.0), 3.0);
        say(sq.x == 0.0 && sq.y == 0.0, "slide: an axis square to the board slides nothing");
    }

    // ---- centre: H alone names the camera --------------------------------------------------
    const cv::Vec3d where[3] = {cv::Vec3d(60.0, -420.0, 520.0), cv::Vec3d(-380.0, 160.0, 560.0),
                                cv::Vec3d(400.0, 230.0, 600.0)};
    Pinhole pins[3];
    Camera cams[3];
    for (int i = 0; i < 3; i++)
    {
        pins[i] = pinholeAt(where[i]);
        cams[i].truth = truthOf(pins[i]);
        cams[i].calib = calibrationSeenThrough(cams[i].truth, 0.13, false); // ONE board: one comb
        const BoardProfile profile = profileFromSpec(perspective_processing::DartboardSpec());
        cams[i].fit = fitBoardToCamera(profile, cams[i].calib);
        std::vector<cv::Point2f> endpoints(cams[i].calib.wires.wireEndpoints.begin(),
                                           cams[i].calib.wires.wireEndpoints.end());
        cams[i].anchor = anchorOnBoard(cams[i].fit, endpoints, cams[i].calib.orientation.wedge20WireIndex);
        say(cams[i].fit.accepted && cams[i].anchor.resolved,
            "plant: pinhole " + std::to_string(i + 1) + " fits and anchors");
        cv::Matx33d Hmm = cams[i].fit.plane.H;
        for (int r = 0; r < 3; r++)
        {
            Hmm(r, 0) *= cams[i].fit.unitPerMm;
            Hmm(r, 1) *= cams[i].fit.unitPerMm;
        }
        const sisal_slide::Centre c = sisal_slide::centreOf(Hmm, cv::Point2d(kCx, kCy));
        // The fit's plane frame is the truth's up to a rotation and a mirror, so the camera's
        // height and its distance from the bull are what can be compared directly.
        const double h = c.at[2], rho = std::sqrt(c.at[0] * c.at[0] + c.at[1] * c.at[1]);
        const double hTrue = where[i][2], rhoTrue = std::sqrt(where[i][0] * where[i][0] + where[i][1] * where[i][1]);
        say(c.valid && std::fabs(c.focalPx - kF) < 8.0 && std::fabs(h - hTrue) < 6.0 && std::fabs(rho - rhoTrue) < 6.0,
            "centre: pinhole " + std::to_string(i + 1) + " recovered f " + fmt2(c.focalPx) + " px, height " +
                fmt2(h) + " (" + fmt2(hTrue) + "), reach " + fmt2(rho) + " (" + fmt2(rhoTrue) + ") mm");
    }

    // ---- solve and off: the planted 45-degree dart, through solveEntry ---------------------
    {
        const BoardProfile profile = profileFromSpec(perspective_processing::DartboardSpec());
        const double phi = 1.5 * wire_model::kSector;
        const cv::Vec3d entry(100.0 * std::cos(phi), 100.0 * std::sin(phi), 0.0);
        const double s45 = std::sqrt(0.5);
        // Leaning radially OUTWARD as it goes in: the dart travels away from the bull.
        const cv::Vec3d travel(s45 * std::cos(phi), s45 * std::sin(phi), -s45);
        std::vector<CameraEvidence> evidence;
        for (int i = 0; i < 3; i++)
        {
            evidence.push_back(shaftEvidence(i, cams[i], pins[i], entry, travel));
        }
        const EntrySolution plain = solveEntry(profile, evidence);
        Params zero;
        zero.sisalMm = 0.0;
        const EntrySolution off = solveEntry(profile, evidence, zero);
        Params two;
        two.sisalMm = 2.0;
        const EntrySolution slid = solveEntry(profile, evidence, two);

        const cv::Point2f e0 = canonicalOf(cams[0], pins[0], entry);
        std::cout << "     the unslid solve: " << outcomeWord(plain.outcome) << " -- " << plain.story << std::endl;
        say(plain.solved && std::sqrt((double)(plain.entryMm.x - e0.x) * (plain.entryMm.x - e0.x) +
                                      (double)(plain.entryMm.y - e0.y) * (plain.entryMm.y - e0.y)) < 1.0,
            "plant: the unslid solve finds the planted entry (" + fmt2(plain.entryMm.x) + ", " +
                fmt2(plain.entryMm.y) + ") against (" + fmt2(e0.x) + ", " + fmt2(e0.y) + ")");
        say(off.solved && off.entryMm == plain.entryMm && !off.sisalApplied && off.sisalStory.empty() &&
                off.score.score == plain.score.score && off.sigmaMajorMm == plain.sigmaMajorMm,
            "off: sisalMm 0 is the default solve, bit for bit, and says nothing");
        const cv::Point2f move = slid.entryMm - plain.entryMm;
        const double rad = radialAt(move, plain.entryMm);
        const double tan = std::sqrt(std::max(0.0, (double)move.x * move.x + (double)move.y * move.y - rad * rad));
        say(slid.solved && slid.sisalApplied && std::fabs(rad - 2.0) < 0.15 && tan < 0.15,
            "solve: d = 2 at 45 degrees moved the entry " + fmt2(rad) + " mm radially (+ = outward, the way "
            "the dart travels) and " + fmt2(tan) + " mm across");
        say(std::fabs(slid.sisalElevationDeg - 45.0) < 1.5 && std::fabs(slid.sisalShiftMm - 2.0) < 0.15,
            "solve: the slide says " + fmt2(slid.sisalShiftMm) + " mm at " + fmt2(slid.sisalElevationDeg) +
                " degrees (" + slid.sisalStory + ")");
    }

    std::cout << (failures == 0 ? "I1909 SISAL CHECK PASS" : "I1909 SISAL CHECK FAIL") << " failures=" << failures
              << std::endl;
    return failures == 0 ? 0 : 1;
}
