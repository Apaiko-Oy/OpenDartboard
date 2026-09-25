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
// radius ~250 px, twenty wedge-boundary chords concurring 21-102 px off the principal
// point (the range the two rig fixtures really sit at, and the near end of it is why
// one camera-window in the census answers "cannot tell"), spans 0.18-0.90 of the board
// radius. The full instrument (testers/i1560_k1_census.py) runs the same control
// through its complete Gauss-Newton fit; this file pins the linear-ensemble core into
// the build.
//
// It also holds THE RECORDED VERDICT'S OWN ARITHMETIC -- the pooling of the nine
// camera-windows, the conversion of the recorded kappa to a k1 at each of the two
// focal lengths in play, and the subtraction the verdict rests on. Those are not a
// re-measurement: the footage is not here. They are what makes editing a constant in
// lens_census.hpp without editing the sentence beside it a red build.
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
        // the worst corner this rig can produce: a point 368 px out at a kappa four
        // and a half times the census's own, which is |kappa| r^2 = 0.149. The fixed point
        // contracts by about 2|kappa| r^2 a pass, so this is where an iteration count
        // chosen as a round number shows up -- eight passes leave 7.2e-3 px here.
        double x = 900.0, y = 620.0;
        distortPx(CX, CY, -1.1e-6, x, y);
        undistortPx(CX, CY, -1.1e-6, x, y);
        say(std::fabs(x - 900.0) < 1e-6 && std::fabs(y - 620.0) < 1e-6,
            "distort/undistort round-trips to under 1e-6 px at the rig's worst corner");
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

    // ---- THE CENSUS'S OWN CLAIMS, held to the constants the header records ----
    //
    // These are not a re-measurement -- the footage is not here and the instrument is
    // testers/i1560_k1_census.py. They are the arithmetic the verdict rests on, so
    // that editing a constant in the header without editing the sentence beside it
    // is a red build rather than a quiet disagreement.
    {
        // kappa is the measurement and k1 is kappa times a focal length squared, so
        // the header states both and this holds it to both. A k1 quoted without an f
        // beside it is the mistake this pair exists to make impossible.
        const double k1_424 = k1At(F_NOMINAL, lens_census::MEASURED_KAPPA);
        const double k1_690 = k1At(lens_census::MEASURED_F_PX, lens_census::MEASURED_KAPPA);
        std::snprintf(buf, sizeof buf,
                      "the recorded kappa is k1 = %.4f at the hard-coded f=424 and %.4f at "
                      "the measured f=%.0f", k1_424, k1_690, lens_census::MEASURED_F_PX);
        say(std::fabs(k1_424 + 0.044) < 0.002 && std::fabs(k1_690 + 0.117) < 0.002, buf);
    }
    {
        // Pooling, restated: the six independent rows and their 1-sigma, inverse
        // variance weighted, must be the number the header states. One row per camera
        // -- rig-18's three, then rig-22's cam1 (3 s), cam2 (3 s), cam3 (open) --
        // because a camera's two windows are the same static board.
        const double k[6] = {-0.022, -0.021, -0.032, -0.021, -0.060, -0.022};
        const double s[6] = {0.065, 0.057, 0.037, 0.076, 0.020, 0.036};
        double wsum = 0.0, num = 0.0;
        for (int i = 0; i < 6; i++)
        {
            const double w = 1.0 / (s[i] * s[i]);
            wsum += w;
            num += w * k[i];
        }
        const double mean = num / wsum, sig = 1.0 / std::sqrt(wsum);
        double chi2 = 0.0;
        for (int i = 0; i < 6; i++)
        {
            const double d = (k[i] - mean) / s[i];
            chi2 += d * d;
        }
        std::snprintf(buf, sizeof buf,
                      "the six rows pool to k1(424) = %.3f +/- %.3f at chi2 %.2f on 5 dof "
                      "-- one kappa describes all six", mean, sig, chi2);
        say(std::fabs(mean + 0.044) < 0.002 && std::fabs(sig - 0.015) < 0.002 && chi2 < 11.07,
            buf);
    }
    {
        // THE SUBTRACTION, which is the whole verdict: the k1 measured here cannot be
        // what #1467 is seeing, because the bow it draws at these board positions is
        // two orders below #1467's residual. 1.63 deg rms at a 220 px board radius.
        const double residual_px = 220.0 * std::sin(1.63 * 3.14159265358979323846 / 180.0);
        double worst = 0.0;
        for (int k = 0; k < 20; k++)
        {
            double x0, y0, x1, y1;
            wireChord(k, CX + 60.0, CY - 40.0, x0, y0, x1, y1); // a census-shaped offset
            worst = std::max(worst,
                             std::fabs(sagittaPx(CX, CY, lens_census::MEASURED_KAPPA,
                                                 x0, y0, x1, y1)));
        }
        std::snprintf(buf, sizeof buf,
                      "the measured kappa bows a wire by %.3f px where #1467's residual is "
                      "%.1f px -- a factor of %.0f, so it is not the same thing",
                      worst, residual_px, residual_px / worst);
        say(worst * 20.0 < residual_px, buf);
    }
    {
        // ...and the k1 that WOULD draw 6.3 px of bow is outside anything #1513 swept
        // and outside anything a lens on this sensor could be.
        double lo = 0.0, hi = 20.0;
        const double target = 220.0 * std::sin(1.63 * 3.14159265358979323846 / 180.0);
        for (int i = 0; i < 60; i++)
        {
            const double mid = 0.5 * (lo + hi);
            double x0, y0, x1, y1;
            double worst = 0.0;
            for (int k = 0; k < 20; k++)
            {
                // the LARGEST board offset in the census (102 px, rig-22 cam3 at the
                // opening), so this is the weakest form of the claim rather than the
                // most flattering one
                wireChord(k, CX + 95.0, CY + 37.0, x0, y0, x1, y1);
                worst = std::max(worst, std::fabs(sagittaPx(CX, CY,
                                                            -mid / (F_NOMINAL * F_NOMINAL),
                                                            x0, y0, x1, y1)));
            }
            if (worst < target)
            {
                lo = mid;
            }
            else
            {
                hi = mid;
            }
        }
        std::snprintf(buf, sizeof buf,
                      "to bow a wire by #1467's %.1f px would take k1(424) = %.2f even at "
                      "the census's most off-axis board, against -0.45 at the worst end "
                      "of #1513's sweep", target, -0.5 * (lo + hi));
        say(0.5 * (lo + hi) > 0.45, buf);
    }
    {
        // f: the recorded focal length is inside its own recorded profile width, and
        // it is the field of view the header claims rather than the hard-coded one.
        const double fov = 2.0 * std::atan(std::sqrt(1280.0 * 1280.0 + 720.0 * 720.0)
                                           / 2.0 / lens_census::MEASURED_F_PX)
                           * 180.0 / 3.14159265358979323846;
        std::snprintf(buf, sizeof buf,
                      "the recorded f = %.0f px is a %.0f-degree diagonal, not the "
                      "hard-coded 120", lens_census::MEASURED_F_PX, fov);
        say(lens_census::MEASURED_F_PX_LOW < lens_census::MEASURED_F_PX
                && lens_census::MEASURED_F_PX < lens_census::MEASURED_F_PX_HIGH
                && fov > 88.0 && fov < 99.0,
            buf);
    }

    std::printf("%s: %d failure(s)\n", failures ? "I1560CHECK FAIL" : "I1560CHECK PASS", failures);
    return failures ? 1 : 0;
}
