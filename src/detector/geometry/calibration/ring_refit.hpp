#pragma once

#include <opencv2/opencv.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "ring_census.hpp"

/**
 * turnaus#1997: THE CAMERA'S MAP, REFITTED THROUGH THE FOUR RINGS. OPT-IN.
 *
 * `board_model::fitBoardToCamera` builds one camera's map from nine numbers -- the mask's
 * outer-doubles ellipse, the bull as a pixel, the comb offset, a scale from the doubles
 * marks -- and #1992's census read that map against the frame on nine camera-fixtures: the
 * treble band 1.0-3.4 mm inward all round, the outer doubles edge turning 1.7-4.6 mm with
 * the angle. One homography through the rings read held-out crossings to 0.50-0.70 mm with
 * NO lens term, and #1560's kappa made all nine worse. So this is that homography and
 * nothing else: after today's fit, the four ring edges `ring_census.hpp` already measures
 * (half the band's own height on 720 rays) are put on their own radii by a projective
 * correction G of the board plane, least squares on |G x| - R, and the camera's map becomes
 * G after today's.
 *
 * WHAT IT ASSUMES, SAID ONCE. The four radii are the spec's (`BoardProfile`: 99 / 107 /
 * 162 / 170 mm); whether the treble's middle is at 103 or 102.2 mm cannot be told from
 * these edges (#1992) and is NOT fitted. A band's two half-height edges are taken for its
 * two wires, which is true where the blur is symmetric and the paint is centred between
 * them. Kappa is 0.
 *
 * THE ROTATION IS NOT THE RINGS'. Circles about a centre say nothing about a rotation
 * about it, and #1992 measured that the rings predict the wires' angles no better than the
 * comb. So G is fitted in the gauge with no rotation in it (a symmetric linear part), and
 * `fitBoardToCamera` re-fits the comb's offset to the calibration's own twenty boundaries
 * through the refitted plane, exactly as it does through today's.
 *
 * A CAMERA WHOSE RINGS DO NOT READ KEEPS TODAY'S MAP AND SAYS SO: a ring the census
 * cannot read, a ring with an arc of more than `kMaxGapDeg` missing (the frame's edge on
 * the live Pi's camera 2, a hand), a fit that does not settle, or -- the acceptance asked
 * of the result itself -- a ring the census still reads more than `kWithinMm` off, all
 * round or turning, through the refitted map.
 *
 * `OD_RING_REFIT=on` refits and scores the treble at the spec's own edges: #1553's bloom
 * adjustment (the treble boundary moved to the mask's traced edge) is dropped on a
 * refitted camera, because that edge is the mask's and the map no longer needs it.
 * `OD_RING_REFIT=bloom` refits and keeps the adjustment, so the two can be told apart on
 * one binary. Unset, empty or any other word is off: nothing here runs and no fit moves.
 *
 * Pure over (frame, Hinv, unitPerMm, bull) and OpenCV -- no board_model.hpp -- so
 * testers/i1997_refit_check.cpp holds it against a drawn board.
 */
namespace ring_refit
{
    enum Setting : int32_t
    {
        Off = 0,
        On = 1,    // refit; the treble scored at the spec's edges
        Bloom = 2, // refit; #1553's bloom adjustment kept
    };

    /** `on` and `bloom`, and nothing else. */
    inline Setting settingFrom(const char *e)
    {
        if (e != nullptr && std::strcmp(e, "on") == 0)
        {
            return On;
        }
        if (e != nullptr && std::strcmp(e, "bloom") == 0)
        {
            return Bloom;
        }
        return Off;
    }

    /** Read once per process. */
    inline Setting asked()
    {
        static const Setting s = settingFrom(std::getenv("OD_RING_REFIT"));
        return s;
    }

    inline const char *wordOf(int32_t setting)
    {
        return setting == On ? "on" : setting == Bloom ? "bloom" : "unset";
    }

    /** A ring with a wider arc than this missing does not hold a projective fit. Degrees. */
    inline constexpr double kMaxGapDeg = 45.0;
    /** What the census must read through the refitted map, all round and turning, mm. */
    inline constexpr double kWithinMm = 1.0;

    /**
     * One camera's refitted map, as plain data: it rides on `DartboardCalibration`, which
     * is held to owning nothing (#1330) and is written to the calibration cache raw.
     * `state` is the Setting it was fitted under where it was APPLIED, Off where the
     * camera keeps today's map (not asked, or asked and refused).
     */
    struct Map
    {
        int32_t state = Off;
        int32_t asked = Off; // the OD_RING_REFIT the cache record was written under (utils/cache.hpp)
        int32_t rays = 0;    // edge points the fit kept, of 2880
        int32_t reserved = 0;
        double rmsMm = 0.0; // of those points about their rings, after the fit
        double H[9] = {0, 0, 0, 0, 0, 0, 0, 0, 0};    // board plane -> image
        double Hinv[9] = {0, 0, 0, 0, 0, 0, 0, 0, 0}; // image -> board plane
    };

    /**
     * Put a refitted map where the fit keeps its plane. True where it did; a camera that
     * carries none is left exactly as it was.
     */
    inline bool applyTo(const Map &map, cv::Matx33d &H, cv::Matx33d &Hinv)
    {
        if (map.state != On && map.state != Bloom)
        {
            return false;
        }
        for (int i = 0; i < 9; i++)
        {
            H.val[i] = map.H[i];
            Hinv.val[i] = map.Hinv[i];
        }
        return true;
    }

    /** Whether a camera carrying this map scores its treble at the spec's own edges. */
    inline bool dropsBloom(const Map &map)
    {
        return map.state == On;
    }

    // ---- the calibration cache ---------------------------------------------------------

    /**
     * Why a cache written under one setting may not be read under another; empty where it
     * may. The words name both settings. utils/cache.hpp stamps every record it writes
     * with the setting (`Map::asked`) and asks this of every record it reads.
     */
    inline std::string cacheRefusal(int32_t written, int32_t now)
    {
        if (written == now)
        {
            return "";
        }
        return std::string("Calibration cache was written with OD_RING_REFIT ") + wordOf(written) +
               " and this start runs with OD_RING_REFIT " + wordOf(now) +
               ": its maps are not the ones this start would fit, so it is not read. Recalibrating.";
    }

    // ---- the fit -----------------------------------------------------------------------

    struct Result
    {
        Map map;           // state Off where today's map is kept
        std::string story; // one sentence either way
    };

    namespace detail
    {
        struct Obs
        {
            double x, y, R; // board plane through the current map, and the ring's radius; units of 100 mm
        };

        inline cv::Matx33d correctionOf(const double p[7])
        {
            return cv::Matx33d(1.0 + p[0], p[1], p[2],
                               p[1], 1.0 + p[3], p[4],
                               p[5], p[6], 1.0);
        }

        inline double residualOf(const cv::Matx33d &G, const Obs &o)
        {
            const cv::Vec3d q = G * cv::Vec3d(o.x, o.y, 1.0);
            return std::sqrt(q[0] * q[0] + q[1] * q[1]) / std::fabs(q[2]) - o.R;
        }

        /** Gauss-Newton on |G x| - R over the seven numbers of a correction with no rotation in it. */
        inline bool solve(const std::vector<Obs> &obs, double p[7], double &rms)
        {
            for (int i = 0; i < 7; i++)
            {
                p[i] = 0.0;
            }
            if (obs.size() < 60)
            {
                return false;
            }
            for (int iter = 0; iter < 12; iter++)
            {
                const cv::Matx33d G = correctionOf(p);
                cv::Mat JtJ = cv::Mat::zeros(7, 7, CV_64F), Jtr = cv::Mat::zeros(7, 1, CV_64F);
                cv::Matx33d Gk[7];
                const double eps = 1e-6;
                for (int k = 0; k < 7; k++)
                {
                    double q[7];
                    std::copy(p, p + 7, q);
                    q[k] += eps;
                    Gk[k] = correctionOf(q);
                }
                for (const Obs &o : obs)
                {
                    const double r = residualOf(G, o);
                    double J[7];
                    for (int k = 0; k < 7; k++)
                    {
                        J[k] = (residualOf(Gk[k], o) - r) / eps;
                    }
                    for (int a = 0; a < 7; a++)
                    {
                        Jtr.at<double>(a, 0) += J[a] * r;
                        for (int b = 0; b < 7; b++)
                        {
                            JtJ.at<double>(a, b) += J[a] * J[b];
                        }
                    }
                }
                for (int a = 0; a < 7; a++)
                {
                    JtJ.at<double>(a, a) *= 1.0 + 1e-9;
                }
                cv::Mat step;
                if (!cv::solve(JtJ, -Jtr, step, cv::DECOMP_SVD))
                {
                    return false;
                }
                double moved = 0.0;
                for (int k = 0; k < 7; k++)
                {
                    p[k] += step.at<double>(k, 0);
                    moved = std::max(moved, std::fabs(step.at<double>(k, 0)));
                }
                if (!std::isfinite(moved))
                {
                    return false;
                }
                if (moved < 1e-10)
                {
                    break;
                }
            }
            const cv::Matx33d G = correctionOf(p);
            double sumsq = 0.0;
            for (const Obs &o : obs)
            {
                const double r = residualOf(G, o);
                sumsq += r * r;
            }
            rms = std::sqrt(sumsq / (double)obs.size());
            return std::isfinite(rms);
        }

        inline double widestGapDeg(const std::vector<std::pair<double, double>> &sortedRows)
        {
            const size_t n = sortedRows.size();
            if (n < 2)
            {
                return 360.0;
            }
            double widest = sortedRows.front().first + 2.0 * CV_PI - sortedRows.back().first;
            for (size_t i = 1; i < n; i++)
            {
                widest = std::max(widest, sortedRows[i].first - sortedRows[i - 1].first);
            }
            return widest * 180.0 / CV_PI;
        }

        inline std::string num(const char *pattern, double v)
        {
            char buf[48];
            snprintf(buf, sizeof(buf), pattern, v);
            return buf;
        }
    }

    inline const char *ringName(int ring)
    {
        static const char *const names[4] = {"the treble ring's inner edge", "the treble ring's outer edge",
                                             "the doubles ring's inner edge", "the doubles ring"};
        return names[ring];
    }

    /**
     * Refit one camera's map through the four rings of one frame.
     *
     * `Hinv`, `unitPerMm`, `bull`, `ringMm` are `ring_census::read`'s. The result's map is
     * in the SAME plane units as `Hinv`, so `unitPerMm` goes on meaning what it meant.
     * `setting` is what an applied map is stamped with (On or Bloom).
     */
    inline Result fit(const cv::Mat &frame, const cv::Matx33d &Hinv, double unitPerMm,
                      const cv::Point2f &bull, const double ringMm[4], Setting setting = On)
    {
        Result out;
        const std::string kept = "; this camera keeps today's map";
        const double unit = 100.0; // millimetres to the solver's units
        cv::Matx33d cur = Hinv;
        int rays = 0;
        double rms = 0.0;
        for (int pass = 0; pass < 3; pass++)
        {
            std::vector<std::pair<double, double>> rows[4];
            if (!ring_census::trace(frame, cur, unitPerMm, bull, ringMm, rows))
            {
                out.story = "ring refit (#1997): the frame or the map could not be read" + kept;
                return out;
            }
            std::vector<detail::Obs> obs;
            std::string unread, gapped;
            for (int ring = 0; ring < 4; ring++)
            {
                const std::vector<std::pair<double, double>> rowsKept =
                    ring_census::detail::withNeighbours(std::move(rows[ring]));
                if ((int)rowsKept.size() < ring_census::kMinRays)
                {
                    unread += (unread.empty() ? "" : ", ") + std::string(ringName(ring)) + " (" +
                              std::to_string(rowsKept.size()) + " rays)";
                    continue;
                }
                const double gap = detail::widestGapDeg(rowsKept);
                if (gap > kMaxGapDeg)
                {
                    gapped += (gapped.empty() ? "" : ", ") + std::string(ringName(ring)) + " (" +
                              detail::num("%.0f", gap) + " degrees)";
                }
                for (const auto &row : rowsKept)
                {
                    const double r = (row.second + ringMm[ring]) / unit;
                    obs.push_back({r * std::cos(row.first), r * std::sin(row.first), ringMm[ring] / unit});
                }
            }
            if (!unread.empty())
            {
                out.story = "ring refit (#1997): not read on enough rays: " + unread + kept;
                return out;
            }
            if (!gapped.empty())
            {
                out.story = "ring refit (#1997): an arc wider than " + detail::num("%.0f", kMaxGapDeg) +
                            " degrees is missing from " + gapped + kept;
                return out;
            }
            double p[7];
            for (int trim = 0; trim < 3; trim++)
            {
                if (!detail::solve(obs, p, rms))
                {
                    out.story = "ring refit (#1997): the fit through the rings did not settle" + kept;
                    return out;
                }
                if (trim == 2)
                {
                    break;
                }
                const cv::Matx33d G = detail::correctionOf(p);
                const double cut = 3.0 * std::max(rms, 0.3 / unit);
                std::vector<detail::Obs> again;
                for (const detail::Obs &o : obs)
                {
                    if (std::fabs(detail::residualOf(G, o)) <= cut)
                    {
                        again.push_back(o);
                    }
                }
                obs.swap(again);
            }
            rays = (int)obs.size();
            // G acts on the plane in the solver's units; T carries the plane's own units there.
            const double s = 1.0 / (unitPerMm * unit);
            const cv::Matx33d T(s, 0, 0, 0, s, 0, 0, 0, 1), Tinv(1.0 / s, 0, 0, 0, 1.0 / s, 0, 0, 0, 1);
            cur = Tinv * detail::correctionOf(p) * T * cur;
        }
        rms *= unit;

        const ring_census::Census after = ring_census::read(frame, cur, unitPerMm, bull, ringMm);
        std::string off;
        for (int ring = 0; ring < 4; ring++)
        {
            const ring_census::Reading &r = after.ring[ring];
            if (!r.read)
            {
                off += (off.empty() ? "" : ", ") + std::string(ringName(ring)) + " not read";
            }
            else if (std::fabs(r.allRoundMm) > kWithinMm || r.turningMm > kWithinMm)
            {
                off += (off.empty() ? "" : ", ") + std::string(ringName(ring)) + " " +
                       detail::num("%+.1f", r.allRoundMm) + " mm all round and " +
                       detail::num("%.1f", r.turningMm) + " mm turning";
            }
        }
        const double det = cv::determinant(cur);
        if (!off.empty() || !std::isfinite(det) || det == 0.0)
        {
            out.story = "ring refit (#1997): through the refitted map the census still reads " +
                        (off.empty() ? std::string("no map at all") : off) + ", past the " +
                        detail::num("%.1f", kWithinMm) + " mm asked of it" + kept;
            return out;
        }
        const cv::Matx33d H = cur.inv();
        out.map.state = setting;
        out.map.rays = rays;
        out.map.rmsMm = rms;
        for (int i = 0; i < 9; i++)
        {
            out.map.H[i] = H.val[i];
            out.map.Hinv[i] = cur.val[i];
        }
        out.story = "ring refit (#1997): the map refitted through " + std::to_string(rays) +
                    " edge points of the four rings, " + detail::num("%.2f", rms) +
                    " mm rms about the spec's radii, no lens term" +
                    (setting == Bloom ? "; the treble's bloom adjustment (#1553) kept"
                                      : "; the treble scored at the spec's own edges");
        return out;
    }
}
