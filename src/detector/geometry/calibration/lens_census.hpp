#pragma once

/**
 * #1560: the lens, measured from footage -- one radial constant per camera, and the
 * census that measured it. Pure math, no OpenCV, no I/O; i1560_lens_check.cpp holds
 * every function here to the synthetic control and the mutation through unit_check.sh,
 * and testers/i1560_k1_census.py is the instrument that took the measurement (its
 * extraction cannot run in this header; its fit is this same model).
 *
 * THE MODEL. Brown-Conrady's first radial term, in PIXEL space about the principal
 * point: p_d = pp + (p_u - pp) * (1 + kappa * r_px^2). This is exactly the normalised
 * k1 with kappa = k1 / f^2 -- reparameterised deliberately, because the bending of a
 * straight line in the image is a pixel-space fact: a measured bow pins kappa however
 * well or badly f itself resolves, where a normalised k1 would inherit f's
 * uncertainty squared. k1 at any focal length is k1At(f, kappa).
 *
 * THE TRAP, as an executable statement rather than prose (#1513, binding on #1560):
 * radial distortion cannot bend a line through the distortion centre. sagittaPx() of
 * a centred chord is zero at any kappa -- the check asserts it -- so a camera whose
 * board sits on the principal point can bound nothing, and the census below therefore
 * reports each camera's board offset next to its verdict. A centred board answers
 * "cannot tell from this footage", never "kappa = 0".
 *
 * THE CENSUS (2026-09-25, instrument at testers/i1560_k1_census.py; frames are
 * 30-frame averages at the named calibration window, #1551 -- "3 s" is the registry
 * dev window, frames 90/84/79 for cameras 1/2/3, "open" is the clip's opening, which
 * is where rig-20260922 camera 1 is admitted; principal point assumed at 640,360 --
 * OV9732 native 1280x720, no crop, docs/rig.md):
 *
 *   fixture/cam  window  board off pp   n   k1(f=424) +/- 1sig   2sig limit  verdict
 *   -----------  ------  ------------  ---  ------------------   ----------  --------
 *   rig-18 cam1    3 s       62 px      18   -0.022 +/- 0.065      0.130     ok
 *   rig-18 cam2    3 s       74 px      18   -0.021 +/- 0.057      0.114     ok
 *   rig-18 cam3    3 s       73 px      19   -0.032 +/- 0.037      0.073     ok
 *   rig-22 cam1    3 s       21 px      18   -0.021 +/- 0.076      0.151     ok, weakest
 *   rig-22 cam1    open      30 px      18   +0.126 +/- 0.163      0.327     CANNOT TELL
 *   rig-22 cam2    3 s       69 px      17   -0.060 +/- 0.020      0.040     ok
 *   rig-22 cam2    open      69 px      20   -0.058 +/- 0.019      0.037     ok
 *   rig-22 cam3    3 s       87 px      15   -0.005 +/- 0.064      0.128     ok
 *   rig-22 cam3    open     102 px      15   -0.022 +/- 0.036      0.072     ok
 *
 *   Pooled over the six independent camera-fixture pairs (a camera's two windows are
 *   the same static board and are NOT two measurements):
 *
 *     k1(f = 424 px) = -0.044 +/- 0.015,  chi-squared 1.49 on 5 degrees of freedom.
 *
 *   One kappa describes all six. It is small: below the -0.05 row of #1513's sweep,
 *   so under a millimetre of board-plane error even under #1488's board-space
 *   scoring, and consistent with the "no deformity" wide lens docs/rig.md bounds the
 *   rig to.
 *
 * AND IT IS NOT WHAT #1467 IS SEEING. That is this census's answer, and it is a
 * subtraction rather than an argument. Per camera, the rms of the twenty wire bows
 * before and after this one kappa is taken out of every wire:
 *
 *   rig-18 cam1 0.439 -> 0.438 px ( 0%)   rig-22 cam1 3s   0.503 -> 0.502 px ( 0%)
 *   rig-18 cam2 0.472 -> 0.470 px ( 0%)   rig-22 cam1 open 0.741 -> 0.724 px ( 2%)
 *   rig-18 cam3 0.291 -> 0.286 px ( 2%)   rig-22 cam2 3s   0.363 -> 0.293 px (19%)
 *   rig-22 cam3 3s   0.542 -> 0.542 px ( 0%)   rig-22 cam2 open 0.335 -> 0.274 px (18%)
 *   rig-22 cam3 open 0.317 -> 0.312 px ( 1%)
 *
 *   #1467's wire residual is rms 1.63 degrees, which at a 220 px board radius is
 *   6.3 px of tangential deviation and 34 px at its 8.91-degree maximum. The largest
 *   bow ANY of the 158 wires in this census carries is 2.93 px, the rms is 0.29-0.74,
 *   and the part one kappa removes is at most 0.07 px. A lens that bent a wire by
 *   6 px rms at these board positions would need |k1| near 2.4 -- ten times the
 *   worst row of #1513's sweep -- and would have drawn the doubles ring off its conic
 *   by hundreds of pixels, where the best-extracted camera measures 1.7 px.
 *
 *   The ring conics say the same thing from the other side. Radial distortion's
 *   conic residual must GROW with ring radius. On the one camera whose extraction is
 *   clean (rig-22 cam2) it reads 1.02 px at 99 mm, 2.18 at 107, 0.99 at 162 and
 *   1.68 at 170 -- not monotone, and the 162 mm ring is the best-fitted of the four.
 *   What the residual does carry is a TWENTY-FOLD component in angle, 0.04-0.39 px on
 *   that camera and up to 5 px on the noisiest, which a smooth radial function cannot
 *   draw at all: it is paint and bloom varying with the bed under the edge.
 *
 * SO: SYSTEMATIC, SMALL, AND NOT THE THING #1467 MEASURED. There is one real lens
 * constant here and it is worth under a millimetre; #1467's residual is something
 * else -- wire-position or board-geometry error at the 1-2 degree scale, which is
 * what the twenty traces scatter by about the 18-degree grid once a pose is removed.
 *
 * f, AND ONE CAMERA MEASURED IT. The maintainer's inventory comment asked for f
 * alongside k1 (#1513, 2026-09-24). Only rig-20260922 camera 2 extracts its rings
 * well enough to answer, and it answers the same at both windows:
 *
 *     f = 690 px (3 s) / 692 px (open), profile width 616-774 px,
 *     ring residual 1.6 mm against that camera's own 1.1 mm extraction scatter,
 *     implied standoff 388 mm, implied diagonal field of view 93-94 degrees.
 *
 *   The other eight camera-windows say NOT RESOLVED by name, and the instrument
 *   prints why for each: their rings miss a pinhole by 3-28 mm against a 1.7-4.0 mm
 *   extraction scatter, so no focal length they imply is a measurement.
 *
 *   Two things this disagrees with, REPORTED AND NOT CHANGED (#1560's boundary):
 *   perspective_processing.cpp's hard-coded 120-degree diagonal is f = 424 px, and
 *   690 px is 94 degrees; and docs/rig.md's "~300 mm standoff", which is cited to
 *   #1340 but is arithmetic from the assumed 424 -- the same imaged board at 690 px
 *   stands 388 mm off. Changing either is a separate decision with its own evidence.
 *
 * WHAT WOULD MOVE ANY OF THIS. A frame where more than one camera's doubles and
 * treble edges extract as cleanly as rig-22 camera 2's do (that camera yields ~670
 * of 720 rays; the rest yield 100-370), which is a lighting and focus question and
 * therefore turnaus#1558's re-recording. Nothing about the lens.
 */

#include <cmath>
#include <cstddef>

namespace lens_census
{
    /**
     * THE MEASURED INTRINSICS, and the first this repository has. Pixel-space, about
     * the image centre, pooled over the six independent camera-fixture pairs above.
     *
     * NOTHING READS THESE YET, deliberately. Distortion is worth under a millimetre
     * on the board at this magnitude (#1513's table, the row below -0.05), the
     * detector's scoring is an image-space ring test that cancels it entirely
     * (#1513 section 1), and correcting for a constant this small would move numbers
     * eleven testers pin for no gain anybody can measure. They are recorded because
     * the measurement was made and because #1510 owns the decision about what to do
     * with it. What would have to be true first: a board-space millimetre scorer
     * (#1488's option, #1510-#1512's ladder), which is where the 0.5-1 mm starts
     * being spendable.
     */
    constexpr double MEASURED_KAPPA = -2.45e-7;       // per px^2, k1(424) = -0.044
    constexpr double MEASURED_KAPPA_SIGMA = 8.3e-8;   // per px^2, k1(424) +/- 0.015

    /**
     * The focal length ONE camera measured -- rig-20260922 camera 2, at both windows,
     * on the only ring extraction in the census clean enough to bound it. It is not
     * "the rig's f": two cameras on the same fixture could not answer at all. Held
     * here so the number is somewhere other than a closed issue thread, and NOT wired
     * into perspective_processing.cpp, whose 120-degree diagonal (424 px) this
     * contradicts -- that flip is its own decision with its own evidence (#1560).
     */
    constexpr double MEASURED_F_PX = 690.0;
    constexpr double MEASURED_F_PX_LOW = 616.0;   // 20%-rise profile width, low end
    constexpr double MEASURED_F_PX_HIGH = 774.0;  // and high end

    /** k1 (normalised Brown-Conrady) equivalent of a pixel-space kappa at focal f. */
    inline double k1At(double f_px, double kappa_per_px2)
    {
        return kappa_per_px2 * f_px * f_px;
    }

    /** Forward radial distortion about (cx, cy): in/out in pixels. */
    inline void distortPx(double cx, double cy, double kappa, double &x, double &y)
    {
        const double dx = x - cx, dy = y - cy;
        const double s = 1.0 + kappa * (dx * dx + dy * dy);
        x = cx + dx * s;
        y = cy + dy * s;
    }

    /** Inverse of distortPx by fixed-point iteration; exact to well under 1e-6 px at
     *  the |kappa| r^2 <= 0.15 this rig can produce. */
    inline void undistortPx(double cx, double cy, double kappa, double &x, double &y)
    {
        const double dx = x - cx, dy = y - cy;
        double ux = dx, uy = dy;
        for (int i = 0; i < 8; i++)
        {
            const double s = 1.0 + kappa * (ux * ux + uy * uy);
            if (std::fabs(s) < 1e-6)
            {
                break;
            }
            ux = dx / s;
            uy = dy / s;
        }
        x = cx + ux;
        y = cy + uy;
    }

    /**
     * The bow of a straight segment's distorted image: sample the segment, distort
     * each sample, and read the largest perpendicular deviation from the distorted
     * endpoints' own chord -- which is what the instrument measures off a wire trace,
     * and is linear in kappa to first order (|kappa| r^2 is at most a few percent
     * here, so the linear term is the measurement).
     *
     * The segment runs from (x0,y0) to (x1,y1) in undistorted pixels; n samples.
     */
    inline double sagittaPx(double cx, double cy, double kappa,
                            double x0, double y0, double x1, double y1, int n = 25)
    {
        double ax = x0, ay = y0, bx = x1, by = y1;
        distortPx(cx, cy, kappa, ax, ay);
        distortPx(cx, cy, kappa, bx, by);
        const double chx = bx - ax, chy = by - ay;
        const double len = std::sqrt(chx * chx + chy * chy);
        if (len < 1e-9 || n < 3)
        {
            return 0.0;
        }
        const double nx = -chy / len, ny = chx / len;
        double worst = 0.0;
        for (int i = 1; i + 1 < n; i++)
        {
            const double t = static_cast<double>(i) / (n - 1);
            double px = x0 + t * (x1 - x0), py = y0 + t * (y1 - y0);
            distortPx(cx, cy, kappa, px, py);
            const double d = (px - ax) * nx + (py - ay) * ny;
            if (std::fabs(d) > std::fabs(worst))
            {
                worst = d;
            }
        }
        return worst;
    }

    /**
     * The ensemble kappa estimator the census fit reduces to on wire bows alone:
     * each measured bow s_i is kappa times the per-unit-kappa slope g_i of that
     * wire's geometry (linearity above; the caller computes g_i in the linear regime
     * as sagittaPx at a small reference kappa divided by that kappa), so least
     * squares over the twenty wires is one division. kappa reads 0 when the geometry
     * carries no signal (all g_i = 0 -- every wire through the distortion centre),
     * which is the trap surfacing as arithmetic: the caller must ask `gain` before
     * believing the estimate.
     */
    struct KappaFit
    {
        double kappa = 0.0;
        double gain = 0.0; // sum of squared unit-kappa bows: the signal the geometry buys
    };

    inline KappaFit fitKappaFromBows(const double *bows, const double *unitBows, std::size_t n)
    {
        KappaFit fit;
        double num = 0.0, den = 0.0;
        for (std::size_t i = 0; i < n; i++)
        {
            num += bows[i] * unitBows[i];
            den += unitBows[i] * unitBows[i];
        }
        fit.gain = den;
        fit.kappa = (den > 0.0) ? num / den : 0.0;
        return fit;
    }
} // namespace lens_census
