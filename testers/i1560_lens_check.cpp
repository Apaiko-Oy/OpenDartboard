// #1560: the lens-census math, held to the synthetic control and the mutation.
//
// lens_census.hpp is the model the k1 census fitted to the rig fixtures --
// pixel-space Brown-Conrady kappa = k1/f^2 -- and this check is the half of the
// census that can run forever without footage: the model's own claims, the trap as
// an executable statement, and the control/mutation pair the issue's checklist
// demands, PREDICTIONS FIRST:
//
//   control:  footage-shaped wire chords distorted at k1(424) = -0.20 with 0.2 px of
//             gaussian bow noise recover k1(424) within +/- 0.05;
//   mutation: the same chords undistorted (kappa = 0) recover |k1(424)| <= 0.05;
//   trap:     a chord THROUGH the distortion centre reads zero bow at any kappa, so
//             the centred-board case bounds nothing and must say so (gain = 0).
//
// "Footage-shaped" means the real census's geometry, not a friendly one: board
// radius ~250 px, twenty wedge-boundary chords concurring ~60-130 px off the
// principal point (the range the two rig fixtures actually sit at), spans 0.18-0.90
// of the board radius with the treble band cut out. The full instrument
// (testers/i1560_k1_census.py) runs the same control through its complete
// Gauss-Newton fit; this file pins the linear-ensemble core of it into the build.
//
//   testers/unit_check.sh 1560
//
// The noise is a fixed-seed LCG rather than <random>, so the check is byte-stable
// across libstdc++ versions (#1551's determinism rule, held here at the source).

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "detector/geometry/calibration/lens_census.hpp"

using lens_census::distortPx;
using lens_census::fitKappaFromBows;
using lens_census::k1At;
using lens_census::KappaFit;
using lens_census::sagittaPx;
using lens_census::undistortPx;

static int failures = 0;

static void say(bool ok, const std::string &what)
{
    std::printf("%s %s\n", ok ? "OK  " : "FAIL", what.c_str());
    if (!ok)
    {
        failures++;
    }
}

namespace
{
    // Deterministic gaussian-ish noise: sum of 12 LCG uniforms, centred.
    struct Lcg
    {
        unsigned long long s;
        explicit Lcg(unsigned long long seed) : s(seed) {}
        double uniform()
        {
            s = s * 6364136223846793005ULL + 1442695040888963407ULL;
            return static_cast<double>((s >> 11) & 0x1FFFFFFFFFFFFFULL) / 9007199254740992.0;
        }
        double gauss(double sigma)
        {
            double a = 0.0;
            for (int i = 0; i < 12; i++)
            {
                a += uniform();
            }
            return (a - 6.0) * sigma;
        }
    };

    const double CX = 640.0, CY = 360.0;
    const double F_NOMINAL = 424.0; // perspective_processing.cpp's 120-degree guess

    /** One rig-shaped wire chord: boundary k of 20, board centre at (bx, by). */
    void wireChord(int k, double bx, double by, double &x0, double &y0,
                   double &x1, double &y1)
    {
        const double boardR = 250.0;
        const double kPi = 3.14159265358979323846;
        const double ang = (9.0 + 18.0 * k) * kPi / 180.0;
        x0 = bx + 0.18 * boardR * std::cos(ang);
        y0 = by + 0.18 * boardR * 0.72 * std::sin(ang); // the rig's ~44deg obliquity
        x1 = bx + 0.90 * boardR * std::cos(ang);
        y1 = by + 0.90 * boardR * 0.72 * std::sin(ang);
    }

    /** The census's ensemble recovery on rig-shaped chords at kappa_true. */
    KappaFit recover(double kappa_true, double bx, double by, double noise_px,
                     unsigned long long seed)
    {
        Lcg rng(seed);
        std::vector<double> bows, slopes;
        const double kref = 1e-7; // linear-regime reference for the unit slope
        for (int k = 0; k < 20; k++)
        {
            double x0, y0, x1, y1;
            wireChord(k, bx, by, x0, y0, x1, y1);
            bows.push_back(sagittaPx(CX, CY, kappa_true, x0, y0, x1, y1)
                           + rng.gauss(noise_px));
            slopes.push_back(sagittaPx(CX, CY, kref, x0, y0, x1, y1) / kref);
        }
        return fitKappaFromBows(bows.data(), slopes.data(), bows.size());
    }
}

int main()
{
    char buf[160];

    // The model's own claims first.
    {
        double x = 900.0, y = 620.0;
        distortPx(CX, CY, -1.1e-6, x, y);
        undistortPx(CX, CY, -1.1e-6, x, y);
        say(std::fabs(x - 900.0) < 1e-6 && std::fabs(y - 620.0) < 1e-6,
            "distort/undistort round-trips to under 1e-6 px at rig magnitudes");
    }
    {
        // THE TRAP: a chord whose line passes through the distortion centre cannot
        // bend, however hard the lens distorts.
        const double k = -2.0e-6;
        double s = sagittaPx(CX, CY, k, CX - 200.0, CY - 150.0, CX + 240.0, CY + 180.0);
        say(std::fabs(s) < 1e-9, "a chord through the distortion centre reads zero bow at any kappa");
        // and the ensemble on a CENTRED board says 'no signal', not 'kappa = 0'
        KappaFit centred = recover(k, CX, CY, 0.0, 42);
        std::snprintf(buf, sizeof buf,
                      "a centred board carries no signal (gain=%.3g): the census must say so, not report kappa",
                      centred.gain);
        say(centred.gain < 1e-6, buf);
    }
    {
        // Bow grows with the chord's offset from the centre -- the sensitivity the
        // census reports per camera is real, monotone leverage.
        const double k = -1.1e-6;
        double prev = 0.0;
        bool monotone = true;
        for (int off = 0; off <= 120; off += 40)
        {
            double s = std::fabs(sagittaPx(CX, CY, k, CX + off - 100.0, CY + off - 180.0,
                                           CX + off + 120.0, CY + off + 160.0));
            if (off > 0 && s <= prev)
            {
                monotone = false;
            }
            prev = s;
        }
        say(monotone, "bow grows with the chord's offset from the distortion centre");
    }
    {
        // #1513's simulation, cross-checked: k1 = -0.35 with the board 150 px
        // off-axis draws a maximum wire bow of ~5.4 px. Same model, same ballpark.
        const double kappa = -0.35 / (F_NOMINAL * F_NOMINAL);
        double worst = 0.0;
        for (int k = 0; k < 20; k++)
        {
            double x0, y0, x1, y1;
            wireChord(k, CX + 106.0, CY + 106.0, x0, y0, x1, y1); // 150 px off, diagonal
            worst = std::max(worst, std::fabs(sagittaPx(CX, CY, kappa, x0, y0, x1, y1)));
        }
        std::snprintf(buf, sizeof buf,
                      "k1=-0.35 at 150 px off-axis bows %.1f px (the #1513 table says ~5.4)", worst);
        say(worst > 2.5 && worst < 11.0, buf);
    }

    // CONTROL, predicted above: recovered k1(424) within +/- 0.05 of -0.20.
    {
        const double k1_true = -0.20;
        KappaFit fit = recover(k1_true / (F_NOMINAL * F_NOMINAL), CX + 30.0, CY - 90.0,
                               0.2, 20260924);
        const double k1_rec = k1At(F_NOMINAL, fit.kappa);
        std::snprintf(buf, sizeof buf,
                      "control: k1(424)=-0.20 distorted the chords; the fit reads %.4f", k1_rec);
        say(std::fabs(k1_rec - k1_true) <= 0.05, buf);
    }

    // MUTATION, predicted above: undistorted chords read |k1(424)| <= 0.05.
    {
        KappaFit fit = recover(0.0, CX + 30.0, CY - 90.0, 0.2, 20260924);
        const double k1_rec = k1At(F_NOMINAL, fit.kappa);
        std::snprintf(buf, sizeof buf,
                      "mutation: undistorted chords read k1(424)=%.4f", k1_rec);
        say(std::fabs(k1_rec) <= 0.05, buf);
    }

    std::printf("%s: %d failure(s)\n", failures ? "I1560CHECK FAIL" : "I1560CHECK PASS", failures);
    return failures ? 1 : 0;
}
