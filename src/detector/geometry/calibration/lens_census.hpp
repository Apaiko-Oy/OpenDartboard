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
 * THE CENSUS (2026-09-24, instrument at testers/i1560_k1_census.py; frames are
 * 30-frame averages at the named calibration window, #1551; principal point assumed
 * at 640,360 -- OV9732 native 1280x720, no crop, docs/rig.md):
 *
 *   [CENSUS-TABLE]
 *
 * [CENSUS-VERDICT]
 */

#include <cmath>
#include <cstddef>

namespace lens_census
{
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
