#pragma once
// turnaus#1910: OD_SETTLE_FRAMES, more than one picture of a settled dart offered to the
// axis fit.
//
// WHY. A dart vibrates for a few hundred milliseconds after it lands. The dart window
// averages its cameras' settled frames into one picture per camera (dart_processing.cpp,
// `averaged_frame`) and the axis is fitted to that one picture; a dart still swinging
// while the average was taken is a smeared silhouette, and a smear is what "the kept
// centreline scatters 3..5 px RMS against a 2.5 px gate" describes. On the Pi board at
// Pelipaikka 6 on 2026-10-20 five of 22 refusals were within 0.3 px of the gate, and
// nothing tried a second picture.
//
// WHAT IT DOES. OD_SETTLE_FRAMES=<n> offers each arriving camera n pictures of the window:
//
//   candidate 0       the window's average -- exactly today's picture, so n = 1 (and unset,
//                     and anything unreadable) is today's detector byte for byte
//   candidates 1..n-1 the window's last n-1 settled frames, newest first, each one cycle's
//                     grey, unaveraged
//
// Each picture is cut against the same reference, its tip found and its axis fitted
// exactly as candidate 0's are; the fit with the lowest straightness residual among
// those the gates ADMIT is the camera's line, and the tip is the tip found in that same
// picture -- so the tip and the axis always come from one picture. Where no candidate is
// admitted, candidate 0 answers, refusal and all, as it does today. Only the arriving
// camera's tip and axis change: the vote, the fresh share, and every reference the window
// leaves behind are still read off the average.
//
// MEMORY. A kept frame is one grey byte per pixel of the window's area: at most
// 1280 x 720 = 921,600 bytes per camera, 2,764,800 bytes (2.7 MB) for three cameras, and
// n - 1 of them are kept -- under the issue's n x 2.7 MB bound. n is capped at
// kMaxSettleFrames = 5, so the most this ever holds is 4 x 2.7 = 11 MB on the 2 GB Pi
// (and a cropped window, OD_WINDOW_CROP, holds only its area).
//
// The selection is a pure function over the candidates' observations, here, so
// testers/i1910_settle_check.cpp holds it on synthetic figures without the detector.

#include <cstdlib>
#include <string>
#include <vector>

#include "shaft_axis.hpp"

namespace settle_frames
{
    /** The cap on OD_SETTLE_FRAMES: see MEMORY above. */
    constexpr int kMaxSettleFrames = 5;

    /** Unset, empty, unreadable or below 1 reads 1 (today's single average); above the
     *  cap reads the cap. */
    inline int settleFramesFrom(const char *value)
    {
        if (value == nullptr || *value == '\0')
        {
            return 1;
        }
        char *end = nullptr;
        const long n = std::strtol(value, &end, 10);
        if (end == value || *end != '\0' || n < 1)
        {
            return 1;
        }
        return n > kMaxSettleFrames ? kMaxSettleFrames : (int)n;
    }

    /** The process's OD_SETTLE_FRAMES, read once. */
    inline int settleFrames()
    {
        static const int n = settleFramesFrom(std::getenv("OD_SETTLE_FRAMES"));
        return n;
    }

    /** What the n fits said, and which one won. */
    struct SettledChoice
    {
        int winner = 0;      // index into the candidates; 0 (the average) where none was admitted
        int tried = 0;       // candidates fitted
        int admitted = 0;    // of those, how many the gates admitted
        double rmsMin = 0.0; // the straightness residual's spread across every candidate fitted
        double rmsMax = 0.0;
    };

    /**
     * The camera's line among `fits`, of which only the first `n` are considered: the
     * admitted fit with the lowest centreline residual; ties go to the earlier candidate,
     * so the average keeps a tie. None admitted: candidate 0.
     */
    inline SettledChoice chooseSettledFit(const std::vector<shaft_axis::AxisObservation> &fits, int n)
    {
        SettledChoice c;
        const int count = (int)fits.size() < n ? (int)fits.size() : n;
        c.tried = count;
        int best = -1;
        for (int k = 0; k < count; k++)
        {
            const double rms = fits[k].centrelineRmsPx;
            if (k == 0 || rms < c.rmsMin)
            {
                c.rmsMin = rms;
            }
            if (k == 0 || rms > c.rmsMax)
            {
                c.rmsMax = rms;
            }
            if (!fits[k].valid)
            {
                continue;
            }
            c.admitted++;
            if (best < 0 || rms < fits[best].centrelineRmsPx)
            {
                best = k;
            }
        }
        c.winner = best < 0 ? 0 : best;
        return c;
    }

    /** The per-camera log line: which picture won and the residual spread across the n. */
    inline std::string settleLine(long window, int camera, const SettledChoice &c,
                                  const std::vector<shaft_axis::AxisObservation> &fits)
    {
        std::string out = "I1910 SETTLE window=" + std::to_string(window) + " cam=" + std::to_string(camera + 1) +
                          " n=" + std::to_string(c.tried) + " admitted=" + std::to_string(c.admitted) +
                          " won=" + (c.winner == 0 ? std::string("average") : "frame-" + std::to_string(c.winner)) +
                          " spread=" + shaft_axis::detail::fmt("%.2f", c.rmsMin) + ".." +
                          shaft_axis::detail::fmt("%.2f", c.rmsMax) + " rms=";
        for (int k = 0; k < c.tried && k < (int)fits.size(); k++)
        {
            out += (k ? "," : "") + shaft_axis::detail::fmt("%.2f", fits[k].centrelineRmsPx) +
                   (fits[k].valid ? "" : "x");
        }
        return out;
    }
}
