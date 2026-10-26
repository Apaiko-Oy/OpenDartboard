#pragma once

#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

#include <opencv2/core.hpp>

/**
 * turnaus#1906: OD_FOCAL_PX -- the focal length perspective_processing's solvePnP is given.
 *
 * Unset, the pose is solved at the 120-degree guess it always was: half the frame's width
 * over tan(60 degrees), scaled by the imaged doubles radius over the board's 170 mm. That is
 * NOT a fixed 424 px. It is a focal that stands every board at the same distance --
 * (cols / 2) / tan(60) mm, 369.5 mm on a 1280-wide frame -- and it reads 424 px only on a
 * board imaged at 195 px radius; the fixtures' 194-250 px give 422-543 px. #1560 measured
 * f = 690 px (a 94-degree diagonal) on one camera, rig-20260922 camera 2.
 *
 *   OD_FOCAL_PX=690            every camera's pose is solved at 690 px
 *   OD_FOCAL_PX=690,700,712    camera 1, 2, 3 in --cams order; a camera the list does not
 *                              reach keeps the guess, and its calibration line says so
 *
 * Unset, empty or unparsable (a word, a zero, a negative, an empty slot, over 100000) is no
 * pin at all, the guess on every camera; an unparsable pin is said once at WARN by the
 * caller rather than half-applied.
 *
 * What it reaches, measured (docs/rig.md, "OD_FOCAL_PX"): the pose is solved for the
 * rectified image and the reprojection line only. Nothing downstream scores through it.
 */
namespace focal_pin
{
    /** The pin's per-camera focal lengths, px; empty when there is no (usable) pin. */
    inline std::vector<double> focalPinFrom(const char *text)
    {
        std::vector<double> out;
        if (text == nullptr || *text == '\0')
        {
            return out;
        }
        const std::string all(text);
        size_t start = 0;
        while (true)
        {
            const size_t comma = all.find(',', start);
            const std::string slot = all.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
            char *end = nullptr;
            const double v = std::strtod(slot.c_str(), &end);
            if (slot.empty() || end == slot.c_str() || *end != '\0' || !std::isfinite(v) || v <= 0.0 || v > 100000.0)
            {
                return std::vector<double>();
            }
            out.push_back(v);
            if (comma == std::string::npos)
            {
                break;
            }
            start = comma + 1;
        }
        return out;
    }

    /** Read once per process. */
    inline const std::vector<double> &focalPin()
    {
        static const std::vector<double> v = focalPinFrom(std::getenv("OD_FOCAL_PX"));
        return v;
    }

    /** True when OD_FOCAL_PX is set to something it refused (the caller says so). */
    inline bool focalPinRefused()
    {
        const char *e = std::getenv("OD_FOCAL_PX");
        return e != nullptr && *e != '\0' && focalPin().empty();
    }

    /** Today's 120-degree guess, exactly as perspective_processing has always computed it. */
    inline double guessedFocalPx(int cols, double outerDoubleWidthPx)
    {
        return (cols / 2.0) / std::tan(120.0 * CV_PI / 360.0) * (outerDoubleWidthPx * 0.5) / 170.0;
    }

    struct Focal
    {
        double px = 0.0;
        bool pinned = false; // from OD_FOCAL_PX rather than the guess
    };

    /** The focal camera `cameraIndex` (0-based, --cams order) is solved at. */
    inline Focal focalFor(const std::vector<double> &pin, int cameraIndex, int cols, double outerDoubleWidthPx)
    {
        Focal f;
        if (pin.size() == 1)
        {
            f.px = pin[0];
            f.pinned = true;
        }
        else if (cameraIndex >= 0 && static_cast<size_t>(cameraIndex) < pin.size())
        {
            f.px = pin[cameraIndex];
            f.pinned = true;
        }
        else
        {
            f.px = guessedFocalPx(cols, outerDoubleWidthPx);
        }
        return f;
    }

    /** Square pixels, the principal point at the frame's centre, no skew. */
    inline cv::Mat cameraMatrixFor(double focalPx, int cols, int rows)
    {
        return (cv::Mat_<double>(3, 3) << focalPx, 0, cols * 0.5, 0, focalPx, rows * 0.5, 0, 0, 1);
    }

    /** The calibration line's clause: which f the pose was solved with, and where it came from. */
    inline std::string focalClause(const Focal &f)
    {
        char buf[96];
        snprintf(buf, sizeof(buf), "solved at f=%.1f px (%s)", f.px,
                 f.pinned ? "OD_FOCAL_PX" : "the 120-degree guess");
        return buf;
    }
} // namespace focal_pin
