#pragma once

#include <cmath>
#include <string>

#include "geometry_calibration.hpp"

// #899: whether the board in front of a camera now is the board that camera was
// calibrated on.
//
// This file holds one comparison and no policy. The policy -- what a board does about a
// camera that stopped answering -- is in scorer.cpp, where the lifecycle is. What is
// here is the only question that lifecycle cannot answer for itself: a camera has come
// back, a fresh calibration has been taken from it, and somebody has to say whether the
// two calibrations describe the same rig.
//
// WHY IT IS A COMPARISON AND NOT A RE-CALIBRATION.
//
// A camera that stopped answering was very likely touched -- a reseated USB plug, a
// nudged tripod, a bumped mount. Calibration is a fact about where the cameras are, so a
// board that reopens its cameras and resumes on the calibration it had is scoring against
// geometry that may no longer be true, and that failure is silent: the darts land in the
// wrong wedge and every control still looks like darts.
//
// The cure is not to replace the geometry. Replacing it would trade a silent wrong answer
// for a different silent wrong answer, because a calibration taken at nine in the evening
// is taken with darts in the board and quite possibly with a person in front of it, and
// nothing would compare it to anything. The cure is to use the fresh calibration as a
// WITNESS and then throw it away: if it agrees with the geometry the board holds, nothing
// moved and the held geometry is still true; if it disagrees, something moved and the
// board does not know what, so it must stop scoring.
//
// That asymmetry is what makes the darts-in-the-board worry harmless. Darts, a person, a
// dimmed light -- everything that can go wrong with a mid-evening calibration pushes the
// witness toward DISAGREEMENT, which costs an evening and is safe. Nothing about them
// can manufacture agreement with a geometry the camera no longer has.
//
// THE THREE NUMBERS, AND WHY THESE THREE.
//
// A camera can be moved in three ways that matter and each has one number already
// measured by the calibration the board performs anyway:
//
//   the bull moved       bullCenter, in pixels. Pans, tilts and slides show up here
//                        first and largest; it is the origin every score is measured
//                        from, so a shift in it is a shift in every score.
//   the board turned     orientation.angleOffsetFromSouth, in degrees. A camera rotated
//                        about its own axis leaves the bull where it was and turns the
//                        wedges under it, which is the move a bull-only test cannot see.
//                        Only read when BOTH calibrations found the star pattern
//                        (isStarCamera), because angleOffsetFromSouth is written nowhere
//                        else and a default 0 compared against a default 0 agrees for a
//                        reason that has nothing to do with the rig.
//   the camera moved in  the mean radius of the outer doubles ellipse, as a fraction.
//   or out               A camera pushed along its own axis keeps the bull and the
//                        angle and changes the scale of everything, which is the move
//                        the first two cannot see.
//
// The tolerances are a struct rather than three constants for board_look::Limits' reason:
// a test can move them and watch the answer move, and a filter nothing can fail is not
// evidence that anything was filtered. Their values are measured rather than chosen --
// see testers/phases899/899-recover.sh, which reads the same run's own agreement figures
// on unmoved footage and its refusal figures on footage translated by a known amount.
namespace geometry_agreement
{
    struct Limits
    {
        // MEASURED, on the mock rig at 1280x720, by testers/phases899/899-recover.sh: an
        // unmoved camera re-measuring its bull from a LATER stretch of its own footage --
        // a different moment, different darts in the board, the same rig -- moved the
        // bull by 6.70, 1.00 and 0.00 px on the three cameras. A camera translated 25 px
        // in the same run moved it by 25.00. 12 px sits between the two with room on
        // both sides, and it is under a tenth of the 300-odd px a board spans in that
        // frame, so a camera off by more than this is a camera whose scores are off by
        // more than a wedge at the edge of the double ring.
        double max_bull_shift_px = 12.0;

        // Wedge boundaries are 18 degrees apart, so 4 degrees is under a quarter of the
        // narrowest thing a wrong angle can move a dart across. The same run measures
        // 0.45 degrees of re-measurement on the one camera whose star pattern is found in
        // both pictures.
        double max_angle_shift_deg = 4.0;

        // 5% of the board's radius. The same run measures 0.00%, 0.02% and 0.10% of
        // re-measurement across a reopen, so this is two orders of magnitude clear of the
        // noise; it is the term that catches a camera pushed along its own axis, which
        // the other two cannot see, and there is no measured positive for it here.
        double max_radius_change = 0.05;
    };

    /** What one camera's two calibrations differ by, in the units they were measured in. */
    struct Movement
    {
        double bull_shift_px = 0.0;
        double angle_shift_deg = 0.0;
        double radius_change = 0.0;
        bool angle_comparable = false;  // both calibrations found the star pattern
        bool radius_comparable = false; // both fitted a doubles ring with a size
    };

    /** The mean radius of a fitted ellipse, or -1 when there is not one to measure. */
    inline double meanRadius(const DartboardCalibration &calibration)
    {
        if (!calibration.ellipses.hasValidDoubles)
        {
            return -1.0;
        }
        const cv::Size2f size = calibration.ellipses.outerDoubleEllipse.size;
        if (size.width <= 0.0f || size.height <= 0.0f)
        {
            return -1.0;
        }
        return (size.width + size.height) / 4.0;
    }

    /** Measure `now` against `held`. Nothing here decides anything; see `hasMoved`. */
    inline Movement measure(const DartboardCalibration &held, const DartboardCalibration &now)
    {
        Movement movement;

        const double dx = (double)now.bullCenter.x - (double)held.bullCenter.x;
        const double dy = (double)now.bullCenter.y - (double)held.bullCenter.y;
        movement.bull_shift_px = std::sqrt(dx * dx + dy * dy);

        if (held.orientation.isStarCamera && now.orientation.isStarCamera)
        {
            movement.angle_comparable = true;
            double turned = std::fabs((double)now.orientation.angleOffsetFromSouth -
                                      (double)held.orientation.angleOffsetFromSouth);
            // The offset is an angle, so 359 degrees away is one degree away.
            while (turned > 180.0)
            {
                turned = 360.0 - turned;
            }
            movement.angle_shift_deg = turned;
        }

        const double held_radius = meanRadius(held);
        const double now_radius = meanRadius(now);
        if (held_radius > 0.0 && now_radius > 0.0)
        {
            movement.radius_comparable = true;
            movement.radius_change = std::fabs(now_radius - held_radius) / held_radius;
        }

        return movement;
    }

    /** Whether this movement is past any of the tolerances it can be judged against. */
    inline bool hasMoved(const Movement &movement, const Limits &limits = Limits())
    {
        if (movement.bull_shift_px > limits.max_bull_shift_px)
        {
            return true;
        }
        if (movement.angle_comparable && movement.angle_shift_deg > limits.max_angle_shift_deg)
        {
            return true;
        }
        if (movement.radius_comparable && movement.radius_change > limits.max_radius_change)
        {
            return true;
        }
        return false;
    }

    /**
     * The numbers, each beside the tolerance it is being judged against, so that a line
     * in the log contradicts itself if the figure it prints came from anywhere but the
     * measurement that decided -- #1321's rule for a refusal that can be argued with.
     */
    inline std::string account(int camera_index, const Movement &movement, const Limits &limits = Limits())
    {
        auto twoPlaces = [](double v)
        {
            std::string s = std::to_string(v);
            const std::string::size_type dot = s.find('.');
            return dot == std::string::npos ? s : s.substr(0, dot + 3);
        };

        std::string line = "camera " + std::to_string(camera_index + 1) +
                           ": the bull moved " + twoPlaces(movement.bull_shift_px) +
                           " px and at most " + twoPlaces(limits.max_bull_shift_px) + " px is still the same rig";
        if (movement.angle_comparable)
        {
            line += ", the 20 turned " + twoPlaces(movement.angle_shift_deg) +
                    " degrees of an allowed " + twoPlaces(limits.max_angle_shift_deg);
        }
        else
        {
            line += ", the 20 was not found in both pictures so the angle was not compared";
        }
        if (movement.radius_comparable)
        {
            line += ", the doubles ring changed size by " + twoPlaces(movement.radius_change * 100.0) +
                    "% of an allowed " + twoPlaces(limits.max_radius_change * 100.0) + "%";
        }
        else
        {
            line += ", the doubles ring was not fitted in both pictures so the size was not compared";
        }
        return line;
    }
}
