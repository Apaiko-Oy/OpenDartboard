/*
 * turnaus#1907: OD_SHAFT_ONLY, held pure. A synthetic dart -- a thin shaft from the tip and a
 * wide flight that sits off the shaft's line and tilts away from it -- of KNOWN cut point
 * (the flight begins 120 px from the tip), so the whole figure's centreline bends where the
 * flight begins and the 2.5 px gate refuses it, while the shaft alone is one straight line.
 *
 *   parse:    unset / "off" / junk read 0, "on" 1, "refused" 2; the process default is 0
 *   premise:  the whole bent figure is refused "not straight" (else nothing here is tested)
 *   cut:      the cut lands within 2 px of 120 from the tip -- with the pipeline's tip as the
 *             hint, with no hint (the narrower end), and on the same dart drawn reversed
 *   straight: the kept piece's fit is admitted and scatters under 1 px
 *   angle:    the kept piece's line is the shaft's to 0.5 deg
 *   ratio:    the cut-off piece is several times the shaft's width (> 3)
 *   rod:      a plain rod is never cut, and its answer is the off answer field for field
 *   mode:     "refused" leaves an admitted figure whole; "on" cuts it
 *   off:      the default params answer exactly what shaft_only = 0 answers
 *   census:   the I1911 line carries ,cut=,ratio=,whole= only where the pin looked
 *
 * The mutation testers/i1907_check.sh makes is the issue's: the cut taken from the WRONG
 * END (the flight end read as the tip), which keeps nothing a step can be found from.
 */
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

static std::string f2(double v)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "%.2f", v);
    return buf;
}

static const int kW = 640, kH = 480;

struct Dart
{
    cv::Point2d tip;  // the tip, image px
    double angleDeg;  // the shaft's direction from the tip toward the flight
    double shaftLen;  // the known cut point: the flight begins this far from the tip
    double shaftHalf; // the shaft's half width
    double flightLen;
    double flightU0, flightU1; // the flight's lateral extent at its root, px across the shaft
    double flightTilt;         // px of lateral drift per px along the flight (the bend)
};

static std::vector<cv::Point> drawDart(const Dart &d, bool withFlight = true)
{
    std::vector<cv::Point> px;
    const double a = d.angleDeg * CV_PI / 180.0;
    const cv::Point2d D(std::cos(a), std::sin(a)), N(-D.y, D.x);
    for (int y = 0; y < kH; y++)
    {
        for (int x = 0; x < kW; x++)
        {
            const double dx = x - d.tip.x, dy = y - d.tip.y;
            const double t = dx * D.x + dy * D.y, u = dx * N.x + dy * N.y;
            bool in = false;
            if (t >= 0.0 && t < d.shaftLen)
            {
                in = std::fabs(u) <= d.shaftHalf;
            }
            else if (withFlight && t >= d.shaftLen && t < d.shaftLen + d.flightLen)
            {
                const double drift = d.flightTilt * (t - d.shaftLen);
                in = u >= d.flightU0 + drift && u <= d.flightU1 + drift;
            }
            if (in)
            {
                px.push_back(cv::Point(x, y));
            }
        }
    }
    return px;
}

static double lineAngleErrDeg(const AxisObservation &ax, double angleDeg)
{
    double e = std::fabs(ax.angleDeg - angleDeg);
    while (e >= 180.0)
    {
        e -= 180.0;
    }
    return std::min(e, 180.0 - e);
}

static AxisParams paramsOf(int mode, const cv::Point2d &hint)
{
    AxisParams p;
    p.shaft_only = mode;
    p.tip_hint = hint;
    return p;
}

static bool sameAnswer(const AxisObservation &a, const AxisObservation &b)
{
    return a.valid == b.valid && a.refusal == b.refusal && a.point == b.point && a.direction == b.direction &&
           a.angleDeg == b.angleDeg && a.extentPx == b.extentPx && a.medianWidthPx == b.medianWidthPx &&
           a.centrelineRmsPx == b.centrelineRmsPx && a.sigmaDeg == b.sigmaDeg && a.supportPixels == b.supportPixels &&
           a.columns == b.columns && a.trimmedColumns == b.trimmedColumns;
}

static void cutCase(const std::string &name, const Dart &d, const cv::Point2d &hint)
{
    const std::vector<cv::Point> px = drawDart(d);
    const AxisObservation ax = observeShaftAxis(px, paramsOf(1, hint));
    std::cout << "     " << name << ": cut=" << ax.shaftCut << " at " << f2(ax.shaftCutPx) << " px (truth "
              << f2(d.shaftLen) << "), ratio " << f2(ax.shaftWidthRatio) << ", whole rms " << f2(ax.shaftWholeRmsPx)
              << " -> " << f2(ax.centrelineRmsPx) << ", angle " << f2(ax.angleDeg) << " (truth " << f2(d.angleDeg)
              << ")" << (ax.shaftNote.empty() ? "" : ", note: " + ax.shaftNote) << std::endl;
    say(ax.shaftCut && std::fabs(ax.shaftCutPx - d.shaftLen) <= 2.0,
        "cut: " + name + " is cut within 2 px of the flight's root (" + f2(ax.shaftCutPx) + " vs " +
            f2(d.shaftLen) + ")");
    say(ax.valid && ax.centrelineRmsPx < 1.0,
        "straight: " + name + "'s kept piece is admitted and scatters " + f2(ax.centrelineRmsPx) + " px");
    say(ax.valid && lineAngleErrDeg(ax, d.angleDeg) < 0.5,
        "angle: " + name + "'s line is the shaft's to " + f2(lineAngleErrDeg(ax, d.angleDeg)) + " deg");
    say(ax.shaftWidthRatio > 3.0, "ratio: " + name + "'s cut-off piece is " + f2(ax.shaftWidthRatio) +
                                      "x the shaft's width");
}

int main()
{
    // ---- parse -----------------------------------------------------------------------
    say(shaftOnlyModeFrom(nullptr) == 0 && shaftOnlyModeFrom("") == 0 && shaftOnlyModeFrom("off") == 0 &&
            shaftOnlyModeFrom("1") == 0 && shaftOnlyModeFrom("ON") == 0,
        "parse: unset, empty, off and junk read off");
    say(shaftOnlyModeFrom("on") == 1 && shaftOnlyModeFrom("refused") == 2, "parse: on reads 1, refused 2");
    say(shaftOnlyMode() == 0 && AxisParams().shaft_only == 0, "parse: the process default (unset) is off");

    // ---- the bent dart ---------------------------------------------------------------
    const Dart bent{cv::Point2d(120.0, 380.0), -28.0, 120.0, 2.5, 60.0, -3.0, 21.0, 0.30};
    const std::vector<cv::Point> bentPx = drawDart(bent);
    AxisParams off;
    off.shaft_only = 0;
    const AxisObservation whole = observeShaftAxis(bentPx, off);
    std::cout << "     whole bent figure: valid=" << whole.valid << " rms " << f2(whole.centrelineRmsPx)
              << " refusal: " << whole.refusal << std::endl;
    say(!whole.valid && whole.refusal.rfind("not straight:", 0) == 0,
        "premise: the whole bent figure is refused not straight at " + f2(whole.centrelineRmsPx) + " px");

    cutCase("hinted", bent, bent.tip);
    cutCase("unhinted", bent, cv::Point2d(-1.0, -1.0));
    Dart reversed = bent;
    reversed.tip = cv::Point2d(520.0, 90.0);
    reversed.angleDeg = bent.angleDeg + 180.0;
    cutCase("reversed", reversed, reversed.tip);

    // ---- the plain rod ---------------------------------------------------------------
    const std::vector<cv::Point> rodPx = drawDart(bent, false);
    const AxisObservation rodOff = observeShaftAxis(rodPx, off);
    const AxisObservation rodOn = observeShaftAxis(rodPx, paramsOf(1, bent.tip));
    std::cout << "     plain rod: note: " << rodOn.shaftNote << std::endl;
    say(rodOn.shaftLooked && !rodOn.shaftCut, "rod: a plain rod is never cut");
    say(sameAnswer(rodOff, rodOn), "rod: the uncut rod answers the off answer field for field");

    // ---- refused-only ----------------------------------------------------------------
    Dart centred = bent;
    centred.flightU0 = -12.0;
    centred.flightU1 = 12.0;
    centred.flightTilt = 0.0;
    const std::vector<cv::Point> centredPx = drawDart(centred);
    const AxisObservation cOff = observeShaftAxis(centredPx, off);
    const AxisObservation cRefused = observeShaftAxis(centredPx, paramsOf(2, centred.tip));
    const AxisObservation cOn = observeShaftAxis(centredPx, paramsOf(1, centred.tip));
    std::cout << "     centred flight: whole valid=" << cOff.valid << " rms " << f2(cOff.centrelineRmsPx)
              << "; on cut=" << cOn.shaftCut << " at " << f2(cOn.shaftCutPx) << std::endl;
    say(cOff.valid && !cRefused.shaftCut && sameAnswer(cOff, cRefused),
        "mode: refused leaves an admitted figure whole, field for field");
    say(cOn.shaftCut && std::fabs(cOn.shaftCutPx - centred.shaftLen) <= 2.0, "mode: on cuts the same figure");
    const AxisObservation bRefused = observeShaftAxis(bentPx, paramsOf(2, bent.tip));
    say(bRefused.shaftCut && bRefused.valid, "mode: refused cuts the refused bent figure");

    // ---- off -------------------------------------------------------------------------
    say(sameAnswer(observeShaftAxis(bentPx), whole) && !observeShaftAxis(bentPx).shaftLooked,
        "off: the default params answer the shaft_only = 0 answer, and never look");

    // ---- census ----------------------------------------------------------------------
    RefusalSlot s0, s1;
    s0.frameAvailable = s1.frameAvailable = true;
    s0.axis = whole;
    s1.axis = rodOff;
    const std::string offLine = refusalCensusLine(3, "vote", "S20", true, 0.5, 2.5, {s0, s1});
    s0.axis = observeShaftAxis(bentPx, paramsOf(1, bent.tip));
    s1.axis = rodOn;
    const std::string onLine = refusalCensusLine(3, "vote", "S20", true, 0.5, 2.5, {s0, s1});
    std::cout << "     " << offLine << std::endl << "     " << onLine << std::endl;
    say(offLine.find(",cut=") == std::string::npos, "census: off writes no cut slot");
    char want[96];
    snprintf(want, sizeof(want), "cam1=ok,rms=%.2f,", s0.axis.centrelineRmsPx);
    char cutSlot[48];
    snprintf(cutSlot, sizeof(cutSlot), ",cut=%.1f,ratio=%.2f,", s0.axis.shaftCutPx, s0.axis.shaftWidthRatio);
    say(onLine.find(want) != std::string::npos && onLine.find(cutSlot) != std::string::npos &&
            onLine.find(",whole=" + f2(whole.centrelineRmsPx)) != std::string::npos &&
            onLine.find(",cut=-,ratio=-,whole=") != std::string::npos,
        "census: on writes the cut, the ratio and the whole residual, and - where no cut was taken");

    std::cout << "FAILURES: " << failures << std::endl;
    return failures == 0 ? 0 : 1;
}
