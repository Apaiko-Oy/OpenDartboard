/*
 * turnaus#1910: OD_SETTLE_FRAMES, held pure. Three synthetic pictures of one dart, the way
 * a window sees a dart still swinging: the first two are the shaft bent by its vibration
 * (the centreline bows 14 and 11 px over its length, refused "not straight" by the 2.5 px
 * gate), the third is the dart at rest, one straight rod. Each is fitted by the real
 * shaft_axis::observeShaftAxis, then chosen among by settle_frames::chooseSettledFit.
 *
 *   parse:    unset / empty / "0" / "-2" / junk read 1, "3" 3, "5" 5, "9" the cap 5; the
 *             process default (OD_SETTLE_FRAMES unset) is 1
 *   premise:  pictures 1 and 2 are refused not straight, picture 3 is admitted (else nothing
 *             here is tested)
 *   n1:       at n = 1 only the first picture is considered: the camera's line is refused
 *   n3:       at n = 3 the third picture wins, admitted, its residual under 1 px, and it is
 *             the only one of three admitted
 *   spread:   the residual spread is the three fits' min..max
 *   admitted: a refused fit never wins on a lower residual (a short blob's 0.10 px beside an
 *             admitted 1.90 px), and a tie keeps the earlier picture
 *   line:     the I1910 SETTLE line names the winner and the spread
 *   census:   the I1911 slot carries ,settle=2/3,spread= only where more than one picture
 *             was fitted
 *
 * testers/i1910_check.sh mutates a scratch copy: the issue's mutation (the first picture
 * always wins), the default moved, and refused fits allowed to win.
 */
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "detector/geometry/detection/settle_frames.hpp"

using namespace shaft_axis;
using namespace settle_frames;

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

// A rod 300 px long, 6 px wide, at 30 deg from (100,100), its centreline bowed by `bowPx`
// at the middle (a parabola, zero at both ends): a shaft caught mid-swing.
static std::vector<cv::Point> drawShaft(double bowPx)
{
    std::vector<cv::Point> px;
    const double a = 30.0 * CV_PI / 180.0, len = 300.0, half = 3.0;
    const cv::Point2d o(100, 100), D(std::cos(a), std::sin(a)), N(-D.y, D.x);
    for (int y = 0; y < 480; y++)
    {
        for (int x = 0; x < 640; x++)
        {
            const double dx = x - o.x, dy = y - o.y;
            const double t = dx * D.x + dy * D.y, u = dx * N.x + dy * N.y;
            if (t < 0.0 || t >= len)
            {
                continue;
            }
            const double s = t / len;
            const double centre = bowPx * 4.0 * s * (1.0 - s);
            if (std::fabs(u - centre) <= half)
            {
                px.push_back(cv::Point(x, y));
            }
        }
    }
    return px;
}

static AxisObservation made(bool valid, double rms)
{
    AxisObservation o;
    o.valid = valid;
    o.centrelineRmsPx = rms;
    o.refusal = valid ? "" : "not a shaft: a blob";
    return o;
}

int main()
{
    // ---- parse -----------------------------------------------------------------------
    say(settleFramesFrom(nullptr) == 1, "parse: unset reads 1");
    say(settleFramesFrom("") == 1, "parse: empty reads 1");
    say(settleFramesFrom("0") == 1 && settleFramesFrom("-2") == 1, "parse: 0 and -2 read 1");
    say(settleFramesFrom("three") == 1 && settleFramesFrom("3x") == 1, "parse: junk reads 1");
    say(settleFramesFrom("3") == 3 && settleFramesFrom("5") == 5, "parse: 3 reads 3, 5 reads 5");
    say(settleFramesFrom("9") == kMaxSettleFrames && kMaxSettleFrames == 5, "parse: 9 reads the cap, 5");
    say(std::getenv("OD_SETTLE_FRAMES") == nullptr && settleFrames() == 1,
        "parse: the process default (OD_SETTLE_FRAMES unset) is 1, today's single average");

    // ---- three pictures of one dart --------------------------------------------------
    const double bows[3] = {14.0, 11.0, 0.0};
    std::vector<AxisObservation> fits;
    for (int k = 0; k < 3; k++)
    {
        fits.push_back(observeShaftAxis(drawShaft(bows[k])));
        std::cout << "     picture " << k + 1 << ": bow " << f2(bows[k]) << " px -> valid=" << fits[k].valid
                  << " rms " << f2(fits[k].centrelineRmsPx) << " px"
                  << (fits[k].valid ? std::string() : ", refusal: " + fits[k].refusal) << std::endl;
    }
    say(!fits[0].valid && fits[0].refusal.find("straight") != std::string::npos,
        "premise: picture 1 (bowed 14 px) is refused not straight at " + f2(fits[0].centrelineRmsPx) + " px");
    say(!fits[1].valid && fits[1].refusal.find("straight") != std::string::npos,
        "premise: picture 2 (bowed 11 px) is refused not straight at " + f2(fits[1].centrelineRmsPx) + " px");
    say(fits[2].valid, "premise: picture 3 (at rest) is admitted at " + f2(fits[2].centrelineRmsPx) + " px");

    // ---- n = 1: today ----------------------------------------------------------------
    const SettledChoice one = chooseSettledFit(fits, 1);
    say(one.tried == 1 && one.winner == 0 && !fits[one.winner].valid,
        "n1: at n = 1 only picture 1 is fitted, and the camera's line is refused (winner " +
            std::to_string(one.winner + 1) + ", tried " + std::to_string(one.tried) + ")");

    // ---- n = 3 -----------------------------------------------------------------------
    const SettledChoice three = chooseSettledFit(fits, 3);
    say(three.winner == 2, "n3: at n = 3 picture 3 wins (winner " + std::to_string(three.winner + 1) + ")");
    say(fits[three.winner].valid && fits[three.winner].centrelineRmsPx < 1.0,
        "n3: the winning line is admitted and scatters " + f2(fits[three.winner].centrelineRmsPx) + " px");
    say(three.tried == 3 && three.admitted == 1,
        "n3: three fitted, one admitted (" + std::to_string(three.tried) + ", " + std::to_string(three.admitted) +
            ")");
    say(three.rmsMin == fits[2].centrelineRmsPx && three.rmsMax == fits[0].centrelineRmsPx,
        "spread: " + f2(three.rmsMin) + ".." + f2(three.rmsMax) + " is the three fits' min..max");

    // ---- refused fits never win; ties keep the earlier picture -------------------------
    const std::vector<AxisObservation> blob = {made(false, 3.10), made(false, 0.10), made(true, 1.90)};
    const SettledChoice b = chooseSettledFit(blob, 3);
    say(b.winner == 2, "admitted: a refused blob at 0.10 px loses to the admitted 1.90 px (winner " +
                           std::to_string(b.winner + 1) + ")");
    const std::vector<AxisObservation> tie = {made(true, 1.20), made(true, 1.20), made(true, 1.50)};
    say(chooseSettledFit(tie, 3).winner == 0, "admitted: a tie keeps the earlier picture, the average");

    // ---- the log line and the census slot ---------------------------------------------
    const std::string line = settleLine(7, 1, three, fits);
    std::cout << "     " << line << std::endl;
    say(line.find("won=frame-2") != std::string::npos && line.find("spread=" + f2(three.rmsMin) + "..") != std::string::npos,
        "line: the I1910 SETTLE line names frame-2 the winner and the spread");

    RefusalSlot slot;
    slot.frameAvailable = true;
    slot.axis = fits[2];
    const std::string plain = refusalCensusLine(7, "geometric", "S20", false, 0.0, 2.5, {slot});
    slot.axis.settleTried = three.tried;
    slot.axis.settleWinner = three.winner;
    slot.axis.settleRmsMin = three.rmsMin;
    slot.axis.settleRmsMax = three.rmsMax;
    const std::string settled = refusalCensusLine(7, "geometric", "S20", false, 0.0, 2.5, {slot});
    std::cout << "     " << settled << std::endl;
    say(plain.find(",settle=") == std::string::npos, "census: one picture leaves the I1911 slot as it was");
    say(settled.find(",settle=2/3,spread=") != std::string::npos, "census: three carry ,settle=2/3,spread=");

    std::cout << "FAILURES: " << failures << std::endl;
    return failures == 0 ? 0 : 1;
}
