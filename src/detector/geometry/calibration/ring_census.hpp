#pragma once

#include <opencv2/opencv.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

/**
 * turnaus#1992: WHERE THE RINGS ARE, READ OFF THE FRAME AT HALF HEIGHT, THROUGH THE MAP.
 *
 * The start-up line `board_model::fitBoardToCamera` already prints carries a residual per
 * ring, and what it measures is the colour MASK's traced edge: a band 10-18 mm wide where
 * the board's is 8, so its -3 / +2 mm are bloom and #1553 uses them as bloom. This reads
 * the frame instead. Along 720 rays from the bull each band is sought within `kSeekMm` of
 * where the map puts it, its peak found, and its two edges taken where the bandness falls
 * to half that ray's own peak -- the edge of a blurred band whatever its brightness. Each
 * edge point goes back through the map to millimetres and is compared with the ring's own,
 * and a ring's residuals are split into the part that is the same all the way round (a
 * scale) and the amplitude of the part that turns once with the angle (a tilt, a centre).
 *
 * The census that bought it (testers/i1992_crossing_census.py, docs/rig.md): on nine
 * camera-fixtures the map read the treble 1.0-3.4 mm inward all round and turned the outer
 * doubles edge by 1.7-4.6 mm, with ring conics through these same edges at 0.6-1.3 px.
 *
 * MEASUREMENT ONLY, AND OFF UNLESS ASKED. `OD_RING_CENSUS=on` adds one log line per look
 * that passed; unset, empty or any other word is off and nothing here runs. Nothing reads
 * a `Census`: no fit, no score, no cache byte. The map tells the search where to look and
 * is in none of the edge points, so a map a few millimetres out still finds its bands.
 *
 * Pure over (frame, Hinv, unitPerMm, bull) -- no board_model.hpp, no wire_model.cpp -- so
 * testers/i1992_ring_check.cpp holds it against a drawn board with nothing but OpenCV.
 */
namespace ring_census
{
    /** `on` and nothing else. */
    inline bool onFrom(const char *e)
    {
        return e != nullptr && std::strcmp(e, "on") == 0;
    }

    /** Read once per process. */
    inline bool isOn()
    {
        static const bool on = onFrom(std::getenv("OD_RING_CENSUS"));
        return on;
    }

    inline constexpr int kRays = 720;
    /** How far either side of the map's own band the band is sought, millimetres. */
    inline constexpr double kSeekMm = 9.0;
    /** A peak under this bandness is not a band (the census's floor). */
    inline constexpr double kFloor = 14.0;
    /** The edge is where the band falls to this share of its own peak on that ray. */
    inline constexpr double kHalf = 0.5;
    /** Fewer edge points than this and a ring is not read: a quarter of the rays. */
    inline constexpr int kMinRays = 180;

    /** How red-or-green a BGR pixel is, bilinear: the bands and nothing black or white. */
    inline double bandness(const cv::Mat &bgr, double x, double y)
    {
        if (x < 0.0 || y < 0.0 || x >= bgr.cols - 1 || y >= bgr.rows - 1)
        {
            return 0.0;
        }
        const int x0 = (int)x, y0 = (int)y;
        const double fx = x - x0, fy = y - y0;
        double c[3];
        for (int k = 0; k < 3; k++)
        {
            c[k] = bgr.at<cv::Vec3b>(y0, x0)[k] * (1 - fx) * (1 - fy) +
                   bgr.at<cv::Vec3b>(y0, x0 + 1)[k] * fx * (1 - fy) +
                   bgr.at<cv::Vec3b>(y0 + 1, x0)[k] * (1 - fx) * fy +
                   bgr.at<cv::Vec3b>(y0 + 1, x0 + 1)[k] * fx * fy;
        }
        const double b = c[0], g = c[1], r = c[2];
        return std::max(r - std::max(g, b), g - std::max(r, b));
    }

    /** One ring's edge, against the millimetres the board gives it. */
    struct Reading
    {
        bool read = false;
        int rays = 0;
        double allRoundMm = 0.0; // + = the map reads this edge further out than the ring is
        double turningMm = 0.0;  // amplitude of the part that turns once with the angle
        double leftRmsMm = 0.0;  // what neither explains
    };

    /** The four ring edges, innermost first: treble inner, treble outer, doubles inner, doubles outer. */
    struct Census
    {
        Reading ring[4];
    };

    namespace detail
    {
        struct Sample
        {
            double r, mm, band;
        };

        /** Least squares v = mean + c cos t + s sin t. False where the angles do not span it. */
        inline bool harmonic(const std::vector<std::pair<double, double>> &rows, double &mean, double &c, double &s)
        {
            cv::Matx33d m = cv::Matx33d::zeros();
            cv::Vec3d rhs(0, 0, 0);
            for (const auto &row : rows)
            {
                const cv::Vec3d f(1.0, std::cos(row.first), std::sin(row.first));
                for (int i = 0; i < 3; i++)
                {
                    rhs[i] += f[i] * row.second;
                    for (int j = 0; j < 3; j++)
                    {
                        m(i, j) += f[i] * f[j];
                    }
                }
            }
            cv::Vec3d x;
            if (!cv::solve(m, rhs, x, cv::DECOMP_LU))
            {
                return false;
            }
            mean = x[0];
            c = x[1];
            s = x[2];
            return true;
        }

        /** Mean, turning and what is left, after dropping what stands off its neighbours. */
        inline Reading readingOf(std::vector<std::pair<double, double>> rows)
        {
            Reading out;
            const int n = (int)rows.size();
            if (n < kMinRays)
            {
                out.rays = n;
                return out;
            }
            // A wire along the ray, a number, a dart: an edge point more than 2 mm off the
            // median of its eleven neighbours in angle is not the ring's.
            std::sort(rows.begin(), rows.end());
            std::vector<std::pair<double, double>> kept;
            kept.reserve(rows.size());
            for (int i = 0; i < n; i++)
            {
                double near[11];
                for (int j = -5; j <= 5; j++)
                {
                    near[j + 5] = rows[((i + j) % n + n) % n].second;
                }
                std::sort(near, near + 11);
                if (std::fabs(rows[i].second - near[5]) <= 2.0)
                {
                    kept.push_back(rows[i]);
                }
            }
            for (int pass = 0; pass < 2; pass++)
            {
                double mean = 0.0, c = 0.0, s = 0.0;
                if ((int)kept.size() < kMinRays || !harmonic(kept, mean, c, s))
                {
                    out.rays = (int)kept.size();
                    return out;
                }
                double sumsq = 0.0;
                for (const auto &row : kept)
                {
                    const double e = row.second - mean - c * std::cos(row.first) - s * std::sin(row.first);
                    sumsq += e * e;
                }
                const double rms = std::sqrt(sumsq / (double)kept.size());
                out.read = true;
                out.rays = (int)kept.size();
                out.allRoundMm = mean;
                out.turningMm = std::sqrt(c * c + s * s);
                out.leftRmsMm = rms;
                if (pass == 0)
                {
                    const double cut = 3.0 * std::max(rms, 0.3);
                    std::vector<std::pair<double, double>> again;
                    for (const auto &row : kept)
                    {
                        const double e = row.second - mean - c * std::cos(row.first) - s * std::sin(row.first);
                        if (std::fabs(e) <= cut)
                        {
                            again.push_back(row);
                        }
                    }
                    kept.swap(again);
                }
            }
            return out;
        }
    }

    /**
     * Read the four ring edges of one frame through one map.
     *
     * `Hinv` is image -> board plane and `unitPerMm` the plane's units per millimetre
     * (`BoardFit::plane.Hinv`, `BoardFit::unitPerMm`); `ringMm` the four radii, innermost
     * first. A frame that is not 8-bit BGR, or a map with no scale, reads nothing.
     */
    inline Census read(const cv::Mat &frame, const cv::Matx33d &Hinv, double unitPerMm,
                       const cv::Point2f &bull, const double ringMm[4])
    {
        Census out;
        if (frame.empty() || frame.type() != CV_8UC3 || !(unitPerMm > 0.0))
        {
            return out;
        }
        std::vector<std::pair<double, double>> rows[4];
        std::vector<detail::Sample> samples, win;
        for (int i = 0; i < kRays; i++)
        {
            const double th = 2.0 * CV_PI * i / kRays;
            const double dx = std::cos(th), dy = std::sin(th);
            samples.clear();
            for (double r = 20.0;; r += 0.5)
            {
                const double x = bull.x + r * dx, y = bull.y + r * dy;
                if (x < 1.0 || y < 1.0 || x >= frame.cols - 2 || y >= frame.rows - 2)
                {
                    break;
                }
                const cv::Vec3d q = Hinv * cv::Vec3d(x, y, 1.0);
                if (q[2] == 0.0)
                {
                    break;
                }
                const double mm = std::sqrt(q[0] * q[0] + q[1] * q[1]) / std::fabs(q[2]) / unitPerMm;
                if (mm > ringMm[3] + 20.0)
                {
                    break;
                }
                if (mm >= ringMm[0] - 14.0)
                {
                    samples.push_back({r, mm, bandness(frame, x, y)});
                }
            }
            for (int band = 0; band < 2; band++)
            {
                const double lo = ringMm[2 * band], hi = ringMm[2 * band + 1];
                win.clear();
                for (const detail::Sample &s : samples)
                {
                    if (s.mm >= lo - kSeekMm && s.mm <= hi + kSeekMm)
                    {
                        win.push_back(s);
                    }
                }
                // The frame's edge cut the search short: not a whole band on this ray.
                if (win.size() < 12 || win.front().mm > lo - 6.0 || win.back().mm < hi + 6.0)
                {
                    continue;
                }
                size_t k = 0;
                for (size_t j = 1; j < win.size(); j++)
                {
                    if (win[j].band > win[k].band)
                    {
                        k = j;
                    }
                }
                const double peak = win[k].band;
                if (peak < kFloor)
                {
                    continue;
                }
                const double half = kHalf * peak;
                size_t a = k, b = k;
                while (a > 0 && win[a].band >= half)
                {
                    a--;
                }
                while (b < win.size() - 1 && win[b].band >= half)
                {
                    b++;
                }
                if (win[a].band >= half || win[b].band >= half)
                {
                    continue; // the band runs off the window: no edge on that side
                }
                const double rIn = win[a].r + (half - win[a].band) / (win[a + 1].band - win[a].band) * 0.5;
                const double rOut = win[b - 1].r + (win[b - 1].band - half) / (win[b - 1].band - win[b].band) * 0.5;
                if (rOut - rIn < 3.0)
                {
                    continue;
                }
                const double edge[2] = {rIn, rOut};
                for (int e = 0; e < 2; e++)
                {
                    const cv::Vec3d q = Hinv * cv::Vec3d(bull.x + edge[e] * dx, bull.y + edge[e] * dy, 1.0);
                    const double X = q[0] / q[2] / unitPerMm, Y = q[1] / q[2] / unitPerMm;
                    rows[2 * band + e].emplace_back(std::atan2(Y, X), std::sqrt(X * X + Y * Y) - ringMm[2 * band + e]);
                }
            }
        }
        for (int ring = 0; ring < 4; ring++)
        {
            out.ring[ring] = detail::readingOf(std::move(rows[ring]));
        }
        return out;
    }

    /** The line: every ring that was read, and by name the ones that were not. */
    inline std::string story(const Census &census)
    {
        static const char *const names[4] = {"the treble ring's inner edge", "the treble ring's outer edge",
                                             "the doubles ring's inner edge", "the doubles ring"};
        std::string rows, unread;
        for (int ring = 0; ring < 4; ring++)
        {
            const Reading &r = census.ring[ring];
            if (!r.read)
            {
                unread += (unread.empty() ? "" : ", ") + std::string(names[ring]);
                continue;
            }
            char buf[200];
            snprintf(buf, sizeof(buf), "%s %+.1f mm all round, %.1f mm turning with the angle (%d rays, %.1f mm rms left)",
                     names[ring], r.allRoundMm, r.turningMm, r.rays, r.leftRmsMm);
            rows += (rows.empty() ? "" : "; ") + std::string(buf);
        }
        std::string line = "rings at half height (#1992): ";
        line += rows.empty() ? "none could be read" : rows;
        if (!rows.empty() && !unread.empty())
        {
            line += "; not read: " + unread;
        }
        line += " -- each band's own edge on 720 rays, through this map; + is read further out than "
                "the ring is";
        return line;
    }
}
