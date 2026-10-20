// turnaus#1905: OD_UNDISTORT, held pure. A straight dart near the frame edge, imaged through
// a known radial term, is a CURVE in the camera's pixels; straightened through
// lens_census::undistortPx before the gate and the line, it is straight again, and the line
// handed to the board fit is the straight dart's own chord in the pixels calibration lives in.
//
// The planted rod is drawn the way a lens draws it: every pixel of the frame is undistorted
// and kept where it lands within half a width of the true (undistorted) segment. Its kappa
// is NOT the rig's -2.45e-7: at that size the bow is a fraction of the column quantisation
// and would hold nothing up, which is the issue's own expectation and is printed below as
// a figure, not asserted. The plant is the exaggerated kappa a check can see.
//
// Each assertion's label is what i1905_check.sh's mutations predict red:
//
//   bent:      off, the planted curve scatters over 1.0 px, twice the worst clean rod's 0.49
//              (AxisParams' measurement), and a 1.0 px gate refuses it: the plant is a test
//   straight:  on, the same pixels scatter under 0.6 px and pass the 1.0 px gate they failed
//
// The plant's 4.1 px bow is lens_census.hpp's own worst corner (kappa -1.1e-6, |kappa| r^2
// 0.149); a stronger kappa thins the drawn rod under two pixels before it bends it past the
// 2.5 px gate (measured at -1.8e-6..-3e-6: 540-880 px of fragments, 59-70 px rms either way),
// which is why the gate the plant crosses is 1.0 px and said so, not 2.5.
//   transport: the returned line is the chord of the distorted true ends, to 0.3 deg / 0.5 px
//   off:       kappa 0 (or no centre) answers the default observation field for field
//   parse:     OD_UNDISTORT's words -- unset / off / junk are 0, on is MEASURED_KAPPA
//   census:    the I1911 REFUSALS slot carries `,raw=` only for a straightened line
//
//   compiled by i1905_check.sh against the tree and against mutated scratch copies.

#include <cmath>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

#include "detector/geometry/detection/shaft_axis.hpp"

using namespace shaft_axis;

static int failures = 0;

static void say(bool ok, const std::string &what)
{
    std::cout << (ok ? "OK   " : "FAIL ") << what << std::endl;
    if (!ok)
    {
        failures++;
    }
}

static std::string f3(double v)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "%.3f", v);
    return buf;
}

static const int kW = 640, kH = 480;
static const double kCx = kW * 0.5, kCy = kH * 0.5;

// The true rod, undistorted pixels: near the right edge of the frame, where the bow is.
struct Rod
{
    double x0, y0, x1, y1, halfWidth;
};

static std::vector<cv::Point> drawRod(const Rod &rod, double kappa)
{
    std::vector<cv::Point> px;
    const double dx = rod.x1 - rod.x0, dy = rod.y1 - rod.y0;
    const double len2 = dx * dx + dy * dy;
    for (int y = 0; y < kH; y++)
    {
        for (int x = 0; x < kW; x++)
        {
            double ux = x, uy = y;
            lens_census::undistortPx(kCx, kCy, kappa, ux, uy);
            double t = ((ux - rod.x0) * dx + (uy - rod.y0) * dy) / len2;
            if (t < 0.0 || t > 1.0)
            {
                continue;
            }
            const double ex = rod.x0 + t * dx - ux, ey = rod.y0 + t * dy - uy;
            if (std::sqrt(ex * ex + ey * ey) <= rod.halfWidth)
            {
                px.emplace_back(x, y);
            }
        }
    }
    return px;
}

static double gatePx = 2.5;

static AxisParams paramsAt(double kappa, bool centred)
{
    AxisParams p;
    p.max_centreline_rms_px = gatePx; // never OD_STRAIGHT_PX's
    p.undistort_kappa = kappa;
    if (centred)
    {
        p.undistort_centre = cv::Point2d(kCx, kCy);
    }
    return p;
}

static bool sameObservation(const AxisObservation &a, const AxisObservation &b)
{
    return a.valid == b.valid && a.refusal == b.refusal && a.point == b.point && a.direction == b.direction &&
           a.angleDeg == b.angleDeg && a.extentPx == b.extentPx && a.medianWidthPx == b.medianWidthPx &&
           a.centrelineRmsPx == b.centrelineRmsPx && a.sigmaDeg == b.sigmaDeg && a.columns == b.columns &&
           a.trimmedColumns == b.trimmedColumns && a.undistorted == b.undistorted;
}

int main()
{
    // ---- the plant: a 230 px rod at the right edge, 7 px wide -------------------------------
    const double kappa = -1.1e-6; // lens_census.hpp's worst corner; the rig's is printed below
    const Rod rod{560.0, 40.0, 600.0, 270.0, 3.5};
    const std::vector<cv::Point> bent = drawRod(rod, kappa);
    double sag = lens_census::sagittaPx(kCx, kCy, kappa, rod.x0, rod.y0, rod.x1, rod.y1);
    std::cout << "plant: " << bent.size() << " px, kappa " << kappa << ", bow " << f3(sag) << " px" << std::endl;

    gatePx = 1.0;
    const AxisObservation raw = observeShaftAxis(bent, paramsAt(0.0, true));
    const AxisObservation on = observeShaftAxis(bent, paramsAt(kappa, true));
    gatePx = 2.5;
    std::cout << "raw:  valid=" << raw.valid << " rms " << f3(raw.centrelineRmsPx) << " refusal '"
              << raw.refusal << "'" << std::endl;
    std::cout << "on:   valid=" << on.valid << " rms " << f3(on.centrelineRmsPx) << " raw "
              << f3(on.rawCentrelineRmsPx) << " sigma " << f3(on.sigmaDeg) << " refusal '" << on.refusal << "'"
              << std::endl;

    say(raw.centrelineRmsPx > 1.0 && !raw.valid && raw.refusal.find("not straight") != std::string::npos,
        "bent: off, the planted curve scatters " + f3(raw.centrelineRmsPx) + " px and is refused not straight");
    say(on.undistorted && on.centrelineRmsPx < 0.6,
        "straight: on, the same pixels scatter " + f3(on.centrelineRmsPx) + " px (under 0.6)");
    say(on.valid, "straight: on, the curve the 1.0 px gate refused is an axis");
    say(on.undistorted && std::fabs(on.rawCentrelineRmsPx - raw.centrelineRmsPx) < 1e-9,
        "straight: on reports the raw residual beside it (" + f3(on.rawCentrelineRmsPx) + " = " +
            f3(raw.centrelineRmsPx) + ")");

    // ---- transport: the chord of the distorted true ends -----------------------------------
    {
        double ax = rod.x0, ay = rod.y0, bx = rod.x1, by = rod.y1;
        lens_census::distortPx(kCx, kCy, kappa, ax, ay);
        lens_census::distortPx(kCx, kCy, kappa, bx, by);
        cv::Point2d ch(bx - ax, by - ay);
        ch *= 1.0 / std::sqrt(ch.x * ch.x + ch.y * ch.y);
        if (ch.x < 0.0)
        {
            ch = -ch;
        }
        const double dot = std::min(1.0, std::fabs(ch.x * on.direction.x + ch.y * on.direction.y));
        const double angle = std::acos(dot) * 180.0 / CV_PI;
        const double off = std::fabs((on.point.x - ax) * (-ch.y) + (on.point.y - ay) * ch.x);
        say(angle < 0.3, "transport: the line is the distorted chord's direction to " + f3(angle) + " deg");
        say(off < 0.5, "transport: its point lies " + f3(off) + " px off the distorted chord");
    }

    // ---- off: byte-identical ---------------------------------------------------------------
    {
        AxisParams def;
        def.max_centreline_rms_px = 2.5;
        def.undistort_kappa = 0.0;
        const AxisObservation base = observeShaftAxis(bent, def);
        const AxisObservation zero = observeShaftAxis(bent, paramsAt(0.0, true));
        const AxisObservation uncentred = observeShaftAxis(bent, paramsAt(kappa, false));
        say(sameObservation(base, zero) && !zero.undistorted && zero.rawCentrelineRmsPx == zero.centrelineRmsPx,
            "off: kappa 0 answers the default observation field for field");
        say(sameObservation(base, uncentred), "off: a kappa with no centre answers the default observation");
    }

    // ---- the rig's own kappa on the same rod: a figure, not an assertion --------------------
    {
        const double rig = lens_census::MEASURED_KAPPA;
        const std::vector<cv::Point> rigRod = drawRod(rod, rig);
        const AxisObservation r0 = observeShaftAxis(rigRod, paramsAt(0.0, true));
        const AxisObservation r1 = observeShaftAxis(rigRod, paramsAt(rig, true));
        std::cout << "rig kappa " << rig << ": bow "
                  << f3(lens_census::sagittaPx(kCx, kCy, rig, rod.x0, rod.y0, rod.x1, rod.y1)) << " px, rms raw "
                  << f3(r0.centrelineRmsPx) << " -> undistorted " << f3(r1.centrelineRmsPx) << std::endl;
    }

    // ---- parse ---------------------------------------------------------------------------
    say(undistortKappaFrom(nullptr) == 0.0 && undistortKappaFrom("") == 0.0 && undistortKappaFrom("off") == 0.0 &&
            undistortKappaFrom("yes") == 0.0 && undistortKappaFrom("-1e-6x") == 0.0,
        "parse: unset, empty, off and junk are 0");
    say(undistortKappaFrom("on") == lens_census::MEASURED_KAPPA, "parse: on is MEASURED_KAPPA");
    say(undistortKappaFrom("-1.1e-6") == -1.1e-6, "parse: a number is that kappa");
    say(AxisParams().undistort_kappa == 0.0 || std::getenv("OD_UNDISTORT") != nullptr,
        "parse: unset OD_UNDISTORT leaves AxisParams off");

    // ---- census --------------------------------------------------------------------------
    {
        RefusalSlot a, b;
        a.frameAvailable = b.frameAvailable = true;
        a.axis = on;
        b.axis = raw;
        const std::string line = refusalCensusLine(3, "geometry", "S20", true, 0.97, 2.5, {a, b});
        std::cout << line << std::endl;
        const size_t cam2 = line.find(" cam2=");
        say(line.find("cam1=ok,") != std::string::npos && line.find(",raw=") < cam2,
            "census: a straightened line carries raw=");
        say(cam2 != std::string::npos && line.find("raw=", cam2) == std::string::npos, "census: an unstraightened one does not");
    }

    std::cout << "FAILURES: " << failures << std::endl;
    return failures == 0 ? 0 : 1;
}
