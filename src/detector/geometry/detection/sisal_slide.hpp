#pragma once

#include <opencv2/core.hpp>
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

/**
 * turnaus#1909: A DART SITS IN THE SISAL, AND `OD_SISAL_MM=<d>` SLIDES THE ENTRY ALONG ITS AXIS.
 *
 * The solve intersects lines transported to the board plane the calibration fitted (the
 * wires' plane), and a dart's tip rests some millimetres into the sisal below it. The point
 * the dart scores at is where its axis reaches that depth, which is the solved entry moved
 * along the axis's own in-plane direction by d x cot(elevation), elevation the axis's angle
 * to the board.
 *
 * THE TRANSPORTED LINES DO NOT GIVE THAT ANGLE. Every one lies IN the board plane and passes
 * through the entry, so together they fix the entry and nothing about elevation. The angle
 * needs each camera's centre, and the only thing that names it is the camera's own plane map:
 * H = K [r1 r2 t] for a pinhole with square pixels and its principal point at the frame's
 * centre leaves the focal length the one unknown, and the two rotation constraints
 * (r1 . r2 = 0, |r1| = |r2|) answer it in least squares. The dart's 3D axis is then the line
 * common to every used camera's plane through its centre and its transported line.
 *
 * MEASURED BEFORE IT WAS BUILT (docs/rig.md, "The sisal census"): on the annotated fixtures
 * the focal comes out 757-854 px on every camera and the darts stand at median 81 degrees to
 * the board, so the slide is about a sixth of d. Unset (the default) nothing here runs and
 * every solve is byte-identical; testers/i1909_sisal_check.cpp holds the arithmetic.
 *
 * Pure and inline for #1338's reason. No identifier is named near or far (MSVC's windows.h
 * defines both).
 */
namespace sisal_slide
{
    /** OD_SISAL_MM, mm of depth below the wires' plane; 0 (off) where unset or unreadable. */
    inline double depthFromEnvironment()
    {
        static const double v = []
        {
            const char *e = std::getenv("OD_SISAL_MM");
            if (e == nullptr || *e == '\0')
            {
                return 0.0;
            }
            char *end = nullptr;
            const double d = std::strtod(e, &end);
            return (end != e && std::isfinite(d) && d > 0.0 && d <= 10.0) ? d : 0.0;
        }();
        return v;
    }

    /** An axis under this sine of elevation (30 degrees) is not a dart standing in a board. */
    inline constexpr double kMinSinElevation = 0.5;

    /** One camera's centre in the canonical frame: x, y in mm, and its height off the plane. */
    struct Centre
    {
        bool valid = false;
        double focalPx = 0.0;
        cv::Vec3d at; // (x, y, h), h > 0 on the cameras' side of the board
    };

    /**
     * The camera centre from `Hmm` (board mm, the camera's own plane frame -> image px) and
     * the principal point, in the camera's plane frame. Invalid where the rotation constraints
     * name no positive focal length.
     */
    inline Centre centreOf(const cv::Matx33d &Hmm, const cv::Point2d &principal)
    {
        Centre out;
        cv::Matx33d g;
        for (int j = 0; j < 3; j++)
        {
            g(0, j) = Hmm(0, j) - principal.x * Hmm(2, j);
            g(1, j) = Hmm(1, j) - principal.y * Hmm(2, j);
            g(2, j) = Hmm(2, j);
        }
        const double p1 = g(0, 0) * g(0, 1) + g(1, 0) * g(1, 1);
        const double q1 = g(2, 0) * g(2, 1);
        const double p2 = g(0, 0) * g(0, 0) + g(1, 0) * g(1, 0) - g(0, 1) * g(0, 1) - g(1, 1) * g(1, 1);
        const double q2 = g(2, 0) * g(2, 0) - g(2, 1) * g(2, 1);
        const double den = p1 * p1 + p2 * p2;
        if (!(den > 0.0))
        {
            return out;
        }
        const double w = -(p1 * q1 + p2 * q2) / den; // 1 / f^2
        if (!(w > 0.0) || !std::isfinite(w))
        {
            return out;
        }
        const double f = 1.0 / std::sqrt(w);
        cv::Vec3d a[3];
        for (int j = 0; j < 3; j++)
        {
            a[j] = cv::Vec3d(g(0, j) / f, g(1, j) / f, g(2, j));
        }
        const double lam = 2.0 / (cv::norm(a[0]) + cv::norm(a[1]));
        cv::Vec3d r1 = a[0] * lam, r2 = a[1] * lam, t = a[2] * lam;
        if (t[2] < 0.0)
        {
            r1 = -r1;
            r2 = -r2;
            t = -t;
        }
        const cv::Vec3d r3 = r1.cross(r2);
        out.at = cv::Vec3d(-r1.dot(t), -r2.dot(t), std::fabs(r3.dot(t)));
        out.focalPx = f;
        out.valid = std::isfinite(out.at[0]) && std::isfinite(out.at[1]) && out.at[2] > 0.0;
        return out;
    }

    /** One used camera's say: its transported line (canonical, point and unit direction) and centre. */
    struct LineAndCentre
    {
        cv::Vec2d point;
        cv::Vec2d dir;
        cv::Vec3d centre;
    };

    /**
     * The dart's 3D direction INTO the board (z < 0), common to every camera's plane through
     * its centre and its line: the cross product of the two most independent plane normals.
     * A zero vector where fewer than two planes are named.
     */
    inline cv::Vec3d axisOf(const std::vector<LineAndCentre> &cams)
    {
        std::vector<cv::Vec3d> normals;
        for (const LineAndCentre &c : cams)
        {
            const cv::Vec3d u(c.dir[0], c.dir[1], 0.0);
            const cv::Vec3d v(c.centre[0] - c.point[0], c.centre[1] - c.point[1], c.centre[2]);
            const cv::Vec3d n = u.cross(v);
            const double nn = cv::norm(n);
            if (nn > 0.0)
            {
                normals.push_back(n / nn);
            }
        }
        cv::Vec3d best(0.0, 0.0, 0.0);
        double bestNorm = 0.0;
        for (size_t i = 0; i < normals.size(); i++)
        {
            for (size_t j = i + 1; j < normals.size(); j++)
            {
                const cv::Vec3d d = normals[i].cross(normals[j]);
                const double n = cv::norm(d);
                if (n > bestNorm)
                {
                    bestNorm = n;
                    best = d / n;
                }
            }
        }
        if (best[2] > 0.0)
        {
            best = -best;
        }
        return best;
    }

    /** The in-plane move of the entry for a tip `depthMm` below the plane along `axis`. */
    inline cv::Point2d slideOf(const cv::Vec3d &axis, double depthMm)
    {
        const double s = depthMm / std::fabs(axis[2]);
        return cv::Point2d(s * axis[0], s * axis[1]);
    }
}
