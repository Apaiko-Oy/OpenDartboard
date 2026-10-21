// turnaus#1911: OD_STRAIGHT_PX's widened sigma, and the census line's words, held pure.
//
// A line admitted above the 2.5 px straightness gate (only possible with OD_STRAIGHT_PX
// raised) carries a perpendicular sigma widened by rms / 2.5: a 5 px line's sigma is twice
// a 2.5 px line's, and at or under 2.5 the factor is exactly 1.0 -- which is why the default
// solve cannot move. It stays ONE constraint (#1505's phantom and #1535's re-report are
// guarded by TOO-FEW-CONSTRAINTS, and a weight must never become a count).
//
// The planted cameras are i1512_intersect_check.cpp's, copied on purpose (that file's own
// reason: a tester must not borrow the code it is checking). Each assertion's label is what
// i1911_check.sh's mutations predict red, one label per mutation's subject:
//
//   scale:   straightSigmaScale's arithmetic       weight:  a widened liar pulls less
//   sigma:   the solve's sigmaPerp doubles at 5 px  count:   a widened line is one constraint
//   gate:    OD_STRAIGHT_PX's parse, unset is 2.5   census:  the I1911 REFUSALS line's words
//
//   compiled by i1911_check.sh with wire_model.cpp, like row 1512.

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


static CameraEvidence withRms(CameraEvidence ev, double rmsPx)
{
    ev.axisRmsPx = rmsPx;
    return ev;
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
        "plant: three planted boards fit and anchor (i1512's subject, the precondition here)");

    // ---- scale: the arithmetic ---------------------------------------------------------
    {
        const double s25 = shaft_axis::straightSigmaScale(2.5), s50 = shaft_axis::straightSigmaScale(5.0);
        say(std::fabs(s50 - 2.0 * s25) < 1e-12 && s25 == 1.0,
            "scale: a 5 px line's factor is twice a 2.5 px line's (" + fmt2(s50) + " vs " + fmt2(s25) + ")");
        say(shaft_axis::straightSigmaScale(0.3) == 1.0 && shaft_axis::straightSigmaScale(2.49) == 1.0 &&
                shaft_axis::straightSigmaScale(0.0) == 1.0,
            "base: under the 2.5 px base the factor is exactly 1.0, so the default solve cannot move");
    }

    // ---- sigma: through the solve ------------------------------------------------------
    {
        const double phi = 1.5 * wire_model::kSector;
        const cv::Point2f C((float)(80.0 * std::cos(phi)), (float)(80.0 * std::sin(phi)));
        const EntrySolution plain = solveEntry(profile, {withRms(lineEvidence(0, cam1, C, 10.0), 2.5),
                                                         withRms(lineEvidence(1, cam2, C, 70.0), 1.0)});
        const EntrySolution wide = solveEntry(profile, {withRms(lineEvidence(0, cam1, C, 10.0), 5.0),
                                                        withRms(lineEvidence(1, cam2, C, 70.0), 1.0)});
        say(plain.solved && wide.solved, std::string("solve: both two-line plants solve (") +
                                             outcomeWord(plain.outcome) + ", " + outcomeWord(wide.outcome) + ")");
        const double a = plain.constraints[0].sigmaPerpMm, b = wide.constraints[0].sigmaPerpMm;
        say(a > 0.0 && std::fabs(b / a - 2.0) < 1e-3,
            "sigma: the 5 px line's sigmaPerp is twice the 2.5 px line's at the solve (" + fmt2(b) + " / " +
                fmt2(a) + " mm)");
        say(std::fabs(plain.constraints[1].sigmaPerpMm - wide.constraints[1].sigmaPerpMm) < 1e-6,
            "sigma: and the other camera's straight line carries the same sigma in both (" +
                fmt2(plain.constraints[1].sigmaPerpMm) + " mm)");
        say(wide.sigmaMajorMm > plain.sigmaMajorMm,
            "sigma: the solved ellipse confesses the wider line (" + fmt2(wide.sigmaMajorMm) + " > " +
                fmt2(plain.sigmaMajorMm) + " mm)");
    }

    // ---- weight: a widened line displaced among two honest ones pulls the entry less ---
    {
        const cv::Point2f C(40.f, 85.f);
        auto solveWith = [&](double liarRms)
        {
            return solveEntry(profile, {lineEvidence(0, cam1, C, 5.0),
                                        withRms(lineEvidence(1, cam2, C, 65.0, 4.0), liarRms),
                                        lineEvidence(2, cam3, C, 125.0)});
        };
        const EntrySolution tight = solveWith(2.5), loose = solveWith(5.0);
        const double et = distMm(tight.entryMm, C), el = distMm(loose.entryMm, C);
        say(tight.solved && loose.solved && el < et,
            "weight: a 4 mm-displaced line pulls the entry less at 5 px than at 2.5 px (" + fmt2(el) + " < " +
                fmt2(et) + " mm)");
    }

    // ---- count: one line, however wide, is one constraint ------------------------------
    {
        const cv::Point2f C(40.f, 85.f);
        const EntrySolution sol =
            solveEntry(profile, {withRms(lineEvidence(0, cam1, C, 10.0), 4.0),
                                 refusedEvidence(1, cam2, "not straight: planted"),
                                 refusedEvidence(2, cam3, "rim only: planted")});
        say(sol.outcome == Outcome::TooFewConstraints && !sol.solved &&
                sol.story.find("no entry: 1 usable constraint(s) of 3 cameras") != std::string::npos,
            std::string("count: a widened line alone is TOO-FEW-CONSTRAINTS, one constraint and never two (") +
                outcomeWord(sol.outcome) + ")");
    }

    // ---- gate: the experiment's switch -------------------------------------------------
    {
        say(shaft_axis::straightGateFrom(nullptr) == 2.5 && shaft_axis::straightGateFrom("") == 2.5 &&
                shaft_axis::straightGateFrom("wide") == 2.5 && shaft_axis::straightGateFrom("-1") == 2.5 &&
                shaft_axis::straightGateFrom("0") == 2.5 && shaft_axis::straightGateFrom("3x") == 2.5,
            "gate: unset, empty, unparsable or not above zero is the 2.5 px gate");
        say(shaft_axis::straightGateFrom("4.0") == 4.0 && shaft_axis::straightGateFrom("3") == 3.0,
            "gate: OD_STRAIGHT_PX=4.0 is a 4.0 px gate");
        say(shaft_axis::AxisParams().max_centreline_rms_px == 2.5,
            "gate: AxisParams' default is 2.5 with OD_STRAIGHT_PX unset (this container sets none)");
    }

    // ---- census: the line's words ------------------------------------------------------
    {
        std::vector<shaft_axis::RefusalSlot> slots(3);
        slots[0].frameAvailable = true;
        slots[0].axis.valid = true;
        slots[0].axis.centrelineRmsPx = 3.75;
        slots[0].axis.extentPx = 120.0;
        slots[0].axis.medianWidthPx = 10.0;
        slots[1].frameAvailable = true;
        slots[1].axis.refusal = "not a shaft: extent 20 px ...; not straight: the kept centreline scatters 4.10 px";
        slots[1].axis.centrelineRmsPx = 4.1;
        slots[2].frameAvailable = true;
        slots[2].rimOnly = true;
        slots[2].axis.refusal = "rim only: this camera's fresh change clears the floor";
        const std::string line = shaft_axis::refusalCensusLine(17, "vote", "D16", true, 0.9615, 4.0, slots);
        const std::string want = "I1911 REFUSALS window=17 path=vote score=D16 r=0.9615 gate=4.00 "
                                 "cam1=ok,rms=3.75,elong=12.00,scale=1.50 "
                                 "cam2=not-a-shaft+not-straight,rms=4.10,elong=0.00,scale=1.00 "
                                 "cam3=rim-only,rms=0.00,elong=0.00,scale=1.00";
        say(line == want, "census: the line names each camera's reason and figures\n     got  " + line +
                              "\n     want " + want);
        shaft_axis::RefusalSlot none;
        none.frameAvailable = false;
        say(shaft_axis::refusalReasonOf(none.axis, false, false) == "no-frame" &&
                shaft_axis::refusalReasonOf(shaft_axis::AxisObservation(), true, false) == "no-figure",
            "census: no frame and no figure are named apart");
    }

    std::cout << "FAILURES: " << failures << std::endl;
    return failures == 0 ? 0 : 1;
}
