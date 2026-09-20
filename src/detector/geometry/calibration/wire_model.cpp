#include "wire_model.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdlib>
#include <string>

using namespace cv;
using namespace std;

namespace wire_model
{
    namespace
    {
        /** Fold an angle into the half-open sector (-9, +9] degrees, in radians. */
        double intoSector(double a)
        {
            return a - kSector * std::round(a / kSector);
        }

        double degOf(double radians) { return radians * 180.0 / kPi; }
        double radOf(double degrees) { return degrees * kPi / 180.0; }
    }

    Plane planeOf(const RotatedRect &conic, const Point2f &bull, double conicOfDoubles)
    {
        Plane plane;

        const double a = conic.size.width * 0.5 * conicOfDoubles;
        const double b = conic.size.height * 0.5 * conicOfDoubles;
        if (!(a > 0.0) || !(b > 0.0) || !std::isfinite(a) || !std::isfinite(b))
        {
            return plane; // built stays false; a conic with no radius names no plane
        }

        // A: the unit circle onto the (doubles) ellipse. R(phi) diag(a, b), then translate.
        const double phi = conic.angle * kPi / 180.0;
        const double c = std::cos(phi), s = std::sin(phi);
        const Matx33d A(a * c, -b * s, conic.center.x,
                        a * s, b * c, conic.center.y,
                        0.0, 0.0, 1.0);

        double det = 0.0;
        const Matx33d Ainv = A.inv(DECOMP_LU, &det);
        if (det == 0.0 || !std::isfinite(det))
        {
            return plane;
        }

        // p = A^-1(bull): where the board's centre sits inside the ring around it.
        const Vec3d q = Ainv * Vec3d(bull.x, bull.y, 1.0);
        const double px = q[0], py = q[1];
        const double r = std::sqrt(px * px + py * py);

        // A bull outside the ring it is supposed to be the centre of is not a tilt, it is
        // a measurement that cannot be true, and atanh would answer with an infinity. The
        // plane stays unbuilt and the caller refuses the camera in those words.
        if (!std::isfinite(r) || r >= 0.999)
        {
            return plane;
        }

        // M: the Klein-disk boost carrying the origin to p. rapidity = atanh|p| along arg p.
        const double t = std::atanh(r);
        const double ch = std::cosh(t), sh = std::sinh(t);
        const double psi = (r > 0.0) ? std::atan2(py, px) : 0.0;
        const double cp = std::cos(psi), sp = std::sin(psi);

        const Matx33d rot(cp, -sp, 0.0,
                          sp, cp, 0.0,
                          0.0, 0.0, 1.0);
        const Matx33d rotT(cp, sp, 0.0,
                           -sp, cp, 0.0,
                           0.0, 0.0, 1.0);
        const Matx33d boost(ch, 0.0, sh,
                            0.0, 1.0, 0.0,
                            sh, 0.0, ch);
        const Matx33d M = rot * boost * rotT;

        plane.H = A * M;
        plane.Hinv = plane.H.inv(DECOMP_LU, &det);
        if (det == 0.0 || !std::isfinite(det))
        {
            plane.built = false;
            return plane;
        }

        plane.built = true;
        plane.tilt = r;
        plane.tiltAngle = psi;
        // The FITTED conic sits at this board radius: 1.0 when it is the doubles ring,
        // 107/170 when the caller said it was the treble ring. Endpoints are generated
        // there, so they land where this pipeline's endpoints have always landed.
        plane.conicRadius = (conicOfDoubles > 0.0) ? 1.0 / conicOfDoubles : 1.0;
        return plane;
    }

    double boardAngleOf(const Plane &plane, const Point2f &image)
    {
        if (!plane.built)
        {
            return 0.0;
        }
        const Vec3d q = plane.Hinv * Vec3d(image.x, image.y, 1.0);
        if (q[2] == 0.0)
        {
            return 0.0;
        }
        return std::atan2(q[1] / q[2], q[0] / q[2]);
    }

    Point2f imageOfBoardAngle(const Plane &plane, double theta, double radius)
    {
        if (!plane.built)
        {
            return Point2f(0.f, 0.f);
        }
        const Vec3d q = plane.H * Vec3d(radius * std::cos(theta), radius * std::sin(theta), 1.0);
        if (q[2] == 0.0)
        {
            return Point2f(0.f, 0.f);
        }
        return Point2f((float)(q[0] / q[2]), (float)(q[1] / q[2]));
    }

    std::vector<double> residualsOf(const Plane &plane, const std::vector<Point2f> &candidates, double offset)
    {
        std::vector<double> out;
        out.reserve(candidates.size());
        for (const Point2f &p : candidates)
        {
            out.push_back(degOf(intoSector(boardAngleOf(plane, p) - offset)));
        }
        return out;
    }

    double coherenceAtFold(const Plane &plane, const std::vector<Point2f> &candidates, int fold)
    {
        if (!plane.built || candidates.empty() || fold <= 0)
        {
            return 0.0;
        }
        double sr = 0.0, si = 0.0;
        for (const Point2f &p : candidates)
        {
            const double th = boardAngleOf(plane, p) * fold;
            sr += std::cos(th);
            si += std::sin(th);
        }
        return std::sqrt(sr * sr + si * si) / (double)candidates.size();
    }

    Fit fitTwentyFold(const Plane &plane, const std::vector<Point2f> &candidates)
    {
        Fit fit;
        if (!plane.built)
        {
            return fit;
        }

        std::vector<double> th;
        th.reserve(candidates.size());
        for (const Point2f &p : candidates)
        {
            th.push_back(boardAngleOf(plane, p));
        }
        fit.candidates = (int)th.size();

        // Six candidates is a third of a board and is where a circular mean stops being a
        // statement about a ring. Below it the fit is not made at all rather than made
        // badly: a camera with five candidates is refused by the coherence it never got.
        if (fit.candidates < 6)
        {
            return fit;
        }

        // The unweighted transform: both the starting offset and the reported R, which
        // has to be about the frame rather than about what the reweighting believed.
        double sr = 0.0, si = 0.0;
        for (double a : th)
        {
            sr += std::cos(kFold * a);
            si += std::sin(kFold * a);
        }
        fit.coherence = std::sqrt(sr * sr + si * si) / (double)fit.candidates;
        double offset = std::atan2(si, sr) / (double)kFold;

        // Six Cauchy-weighted passes. The scale is the residual cut, so a candidate a
        // whole cut away from the ring carries half the weight of one on it and an
        // intruder six degrees out carries a fifth.
        const double scale = radOf(kResidualCutDeg);
        for (int pass = 0; pass < 6; pass++)
        {
            double wr = 0.0, wi = 0.0;
            for (double a : th)
            {
                const double res = intoSector(a - offset);
                const double w = 1.0 / (1.0 + (res / scale) * (res / scale));
                wr += w * std::cos(kFold * a);
                wi += w * std::sin(kFold * a);
            }
            if (wr == 0.0 && wi == 0.0)
            {
                break;
            }
            offset = std::atan2(wi, wr) / (double)kFold;
        }
        fit.offset = offset;

        double sumsq = 0.0;
        for (double a : th)
        {
            const double res = degOf(intoSector(a - offset));
            sumsq += res * res;
            if (std::fabs(res) <= kResidualCutDeg)
            {
                fit.inliers++;
            }
        }
        fit.rmsResidualDeg = std::sqrt(sumsq / (double)fit.candidates);
        fit.inlierFraction = (double)fit.inliers / (double)fit.candidates;
        fit.built = true;
        return fit;
    }

    Ring ringFrom(const Plane &plane, const Fit &fit, const std::vector<Point2f> &candidates,
                  const Point2f &bull)
    {
        Ring ring;
        if (!plane.built || !fit.built)
        {
            return ring;
        }

        std::vector<double> th;
        th.reserve(candidates.size());
        for (const Point2f &p : candidates)
        {
            th.push_back(boardAngleOf(plane, p));
        }

        const double snap = radOf(kSnapDeg);
        std::vector<Point2f> points;
        points.reserve(kFold);

        for (int k = 0; k < kFold; k++)
        {
            const double target = fit.offset + kSector * k;

            // The candidates this boundary really has, averaged as angles about the
            // target rather than as points: two endpoints of one wire sit at different
            // radii and their midpoint is not on the ring.
            double sum = 0.0;
            int n = 0;
            for (double a : th)
            {
                const double d = intoSector(a - target);
                if (std::fabs(d) <= snap)
                {
                    sum += d;
                    n++;
                }
            }

            double angle = target;
            if (n > 0)
            {
                angle = target + sum / n;
                ring.snapped++;
            }
            points.push_back(imageOfBoardAngle(plane, angle, plane.conicRadius));
        }

        // The image-angle order every reader of wireEndpoints already assumes.
        std::sort(points.begin(), points.end(), [&bull](const Point2f &a, const Point2f &b)
                  { return std::atan2(a.y - bull.y, a.x - bull.x) < std::atan2(b.y - bull.y, b.x - bull.x); });

        ring.endpoints = points;
        return ring;
    }

    double minimumCoherence()
    {
        static double asked = []
        {
            const char *e = std::getenv("OD_WIRE_FIT_MIN");
            const double v = e ? std::atof(e) : -1.0;
            // A cut outside (0, 1) names no coherence any frame can have, so it is
            // ignored rather than obeyed -- OD_WIRE_REGION_MARGIN's rule, and for the
            // same reason: a clamp obeys a value nobody meant.
            return (v > 0.0 && v < 1.0) ? v : -1.0;
        }();
        // MEASURED, not chosen. See testers/i1467_run.sh section 5's sweep.
        return (asked > 0.0) ? asked : 0.60;
    }

    bool modelNotAsked()
    {
        static bool v = []
        {
            const char *e = std::getenv("OD_WIRE_MODEL");
            return e && std::string(e) == "count";
        }();
        return v;
    }
}
