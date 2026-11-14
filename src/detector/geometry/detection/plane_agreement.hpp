#pragma once
// turnaus#1993: fresh change that AGREES IN THE BOARD PLANE across cameras.
//
// A shadow lies on the board; a dart stands off it. Carried through each camera's own map
// to the board plane, a shadow lands at one board position from every camera, while the
// dart's body lands as a streak leading away from that camera -- so two cameras' pictures
// of one dart cross at its entry and nowhere else. Fresh pixels that another camera also
// has, at the same place on the board, are therefore either the dart's last few millimetres
// or something lying on the face: a shadow (Target's US20250093134A1 removes shadows by
// this comparison).
//
// This header is the PURE half: points already in the board plane (millimetres, the
// sequence space entry_intersection.hpp solves in), a tolerance, and which of one camera's
// points coincide with any other camera's. score_processing.cpp carries the pixels there
// and owns the retry; nothing here knows a camera, a map or a gate.
//
// THE TOLERANCE IS MILLIMETRES WIDE BY NECESSITY. #1992 measured each camera's map reading
// the rings 1-4.6 mm off, so two cameras' pictures of ONE shadow edge sit that far apart on
// the board; 6 mm is the default and OD_PLANE_TOL_MM moves it. docs/rig.md has the census
// that says what a tolerance that wide can and cannot separate.
//
//   OD_PLANE_CENSUS=1   one `I1993 PLANE` line per fitted camera per called dart: how much
//                       of its figure coincides, over what span, and what the fit would
//                       read without those pixels. Measurement only: nothing is adopted.
//   OD_PLANE_DROP=on    a figure refused `not-straight` is fitted again without its
//                       coincident pixels, every gate unchanged, and the second fit is the
//                       camera's axis only if it passes them all. Unset, nothing is kept,
//                       computed or printed, and every output is today's.

#include <opencv2/opencv.hpp>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

namespace plane_agreement
{
    inline bool dropFrom(const char *e) { return e != nullptr && std::string(e) == "on"; }
    inline bool censusFrom(const char *e) { return e != nullptr && std::string(e) == "1"; }
    inline double toleranceFrom(const char *e)
    {
        const double v = e != nullptr ? std::atof(e) : 0.0;
        return v > 0.0 && v <= 50.0 ? v : 6.0;
    }
    inline bool dropIsOn()
    {
        static const bool v = dropFrom(std::getenv("OD_PLANE_DROP"));
        return v;
    }
    inline bool censusIsOn()
    {
        static const bool v = censusFrom(std::getenv("OD_PLANE_CENSUS"));
        return v;
    }
    /** Whether a window keeps what this needs at all: unset, it keeps nothing. */
    inline bool wanted() { return dropIsOn() || censusIsOn(); }
    inline double toleranceMm()
    {
        static const double v = toleranceFrom(std::getenv("OD_PLANE_TOL_MM"));
        return v;
    }

    /** One camera's account: which of its points coincide, how many, over what span. */
    struct Agreement
    {
        std::vector<unsigned char> coincident; // one per point offered, 1 where it coincides
        int count = 0;
        double extentMm = 0.0; // diagonal of the coincident points' 2nd-98th percentile box
        int others = 0;        // other cameras that offered any change to compare against
    };

    namespace detail
    {
        const double kHalfSpanMm = 320.0; // past the number ring; a point outside coincides with nothing
        const double kCellMm = 1.0;

        inline bool cellOf(const cv::Point2f &mm, int &col, int &row)
        {
            col = (int)std::lround((mm.x + kHalfSpanMm) / kCellMm);
            row = (int)std::lround((mm.y + kHalfSpanMm) / kCellMm);
            const int n = (int)(2.0 * kHalfSpanMm / kCellMm) + 1;
            return std::isfinite(mm.x) && std::isfinite(mm.y) && col >= 0 && row >= 0 && col < n && row < n;
        }

        inline double spanOf(std::vector<float> &v)
        {
            if (v.empty())
            {
                return 0.0;
            }
            std::sort(v.begin(), v.end());
            const size_t lo = (size_t)(0.02 * (double)(v.size() - 1));
            const size_t hi = (size_t)std::ceil(0.98 * (double)(v.size() - 1));
            return (double)v[hi] - (double)v[lo];
        }
    }

    /**
     * For each camera's `subject` points, which have fresh change from ANOTHER camera
     * within `toleranceMm` on the board. `fresh[k]` is everything camera k saw change (its
     * subject is normally a subset of it); a camera is never compared with itself.
     */
    inline std::vector<Agreement> agree(const std::vector<std::vector<cv::Point2f>> &subject,
                                        const std::vector<std::vector<cv::Point2f>> &fresh, double toleranceMm)
    {
        const size_t n = subject.size();
        const int cells = (int)(2.0 * detail::kHalfSpanMm / detail::kCellMm) + 1;
        const int reach = std::max(0, (int)std::lround(toleranceMm / detail::kCellMm));
        const cv::Mat disc = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(2 * reach + 1, 2 * reach + 1));
        std::vector<cv::Mat> spread(n);
        for (size_t k = 0; k < n && k < fresh.size(); k++)
        {
            if (fresh[k].empty())
            {
                continue;
            }
            cv::Mat seen = cv::Mat::zeros(cells, cells, CV_8UC1);
            for (const cv::Point2f &p : fresh[k])
            {
                int c, r;
                if (detail::cellOf(p, c, r))
                {
                    seen.at<unsigned char>(r, c) = 255;
                }
            }
            cv::dilate(seen, spread[k], disc);
        }
        std::vector<Agreement> out(n);
        for (size_t k = 0; k < n; k++)
        {
            Agreement &a = out[k];
            a.coincident.assign(subject[k].size(), 0);
            cv::Mat others;
            for (size_t j = 0; j < n; j++)
            {
                if (j == k || spread[j].empty())
                {
                    continue;
                }
                a.others++;
                if (others.empty())
                {
                    others = spread[j].clone();
                }
                else
                {
                    cv::bitwise_or(others, spread[j], others);
                }
            }
            if (others.empty())
            {
                continue;
            }
            std::vector<float> xs, ys;
            for (size_t i = 0; i < subject[k].size(); i++)
            {
                int c, r;
                if (detail::cellOf(subject[k][i], c, r) && others.at<unsigned char>(r, c) != 0)
                {
                    a.coincident[i] = 1;
                    a.count++;
                    xs.push_back(subject[k][i].x);
                    ys.push_back(subject[k][i].y);
                }
            }
            const double w = detail::spanOf(xs), h = detail::spanOf(ys);
            a.extentMm = std::sqrt(w * w + h * h);
        }
        return out;
    }

    /** The points of `pixels` that did not coincide -- what the second fit is given. */
    inline std::vector<cv::Point> without(const std::vector<cv::Point> &pixels, const Agreement &a)
    {
        std::vector<cv::Point> kept;
        kept.reserve(pixels.size());
        for (size_t i = 0; i < pixels.size(); i++)
        {
            if (i >= a.coincident.size() || a.coincident[i] == 0)
            {
                kept.push_back(pixels[i]);
            }
        }
        return kept;
    }

    /**
     * Whether a second fit is made at all: only a figure the straightness gate refused,
     * only where another camera offered change to compare with, and only where something
     * coincided and something is left.
     */
    inline bool retries(const std::string &reason, const Agreement &a, size_t pixels)
    {
        return reason.find("not-straight") != std::string::npos && a.others > 0 && a.count > 0 &&
               (size_t)a.count < pixels;
    }

    /** The retry is the camera's axis only under the switch and only if it passed every gate. */
    inline bool adopts(bool switchOn, bool retried, bool retryValid) { return switchOn && retried && retryValid; }
}
