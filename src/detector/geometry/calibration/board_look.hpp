#pragma once

#include <string>

// #1318: whether the camera that produced a calibration was looking at a dartboard.
//
// Until this file existed, nothing in the program asked. A camera that OPENED was a
// camera the board scored with, and on a laptop the device at index 0 is the built-in
// webcam: on the maintainer's Windows rig on 2026-09-18 the detector calibrated the
// operator's face as camera 1, wrote a portrait to debug_frames/roi_processing/ and went
// on to score with it. Opening is not the same as being the right camera.
//
// The evidence is taken from the calibration the program already performs, because that
// is the only thing in the system that has actually looked at the picture. Two numbers
// carry it, and both come out of steps 2-6 of calibrateSingleCamera:
//
//   red_green_pixels  the doubles mask, i.e. how much of the frame keys as dartboard
//                     red or dartboard green. A dartboard's doubles and trebles are a
//                     thin annulus and a small fraction of the frame; skin in warm room
//                     light is most of it. Measured on the rig: 27,624 pixels for a
//                     board camera against 237,036 for the webcam pointed at a face, in
//                     the same run, at 1280x720 -- 3.0% against 25.7%.
//
//   outer_points      how many of the 120 rays cast out from the bull found the outer
//                     edge of the doubles ring and survived the ring-width validation.
//                     A dartboard is a closed ring around the bull, so a board camera
//                     scores most of them: 68 and 103 on that same rig. There is no ring
//                     in a picture of a room, so the rays land on whatever is furthest
//                     and the ellipse fit either fails outright or is fitted through a
//                     handful of survivors.
//
// MJPG capability was considered and is deliberately NOT the test. --autocams already
// rejects a camera that will not negotiate MJPG, and that check earns its place -- three
// 1280x720 cameras do not fit uncompressed on one USB bus -- but it is a statement about
// a camera's electronics, not about where it is pointed. Most laptop webcams negotiate
// MJPG perfectly well, so a filter built on it would have admitted the very face this
// issue is about. What separates a board camera from a webcam is that one of them can
// see a dartboard, and the only thing that can answer that is a look at the picture.
namespace board_look
{
    /**
     * What one camera's calibration measured, as plain numbers, so that it can be
     * logged, compared between cameras and carried in DartboardCalibration -- which is
     * written to the cache with a raw fwrite, so nothing here may own memory.
     */
    struct Evidence
    {
        int frame_pixels = 0;        // cols * rows of the frame the calibration ran on
        int red_green_pixels = 0;    // white pixels in the doubles mask
        int outer_points = 0;        // validated outer boundary points, of 120 rays
        int inner_points = 0;        // validated inner boundary points
        bool traced_doubles = false; // the doubles ring was fitted at all
    };

    /**
     * Where the line is drawn. Stated as a struct rather than as constants so that a
     * test can move it and watch the answer move -- a filter nothing can fail is not
     * evidence that anything was filtered.
     */
    struct Limits
    {
        // 12%, sitting in the gap between the two measurements above (3.0% and 25.7%)
        // rather than beside either of them. A board hung on a red wall costs a little
        // of this headroom and a face spends all of it.
        double max_red_green_fraction = 0.12;

        // Ray tracing already refuses to fit an ellipse through fewer than 50 rays
        // (EllipseParams::minValidRays), so this is that floor said again where the
        // decision is made, not a second, looser one.
        int min_outer_points = 50;
    };

    inline double redGreenFraction(const Evidence &e)
    {
        return e.frame_pixels > 0 ? (double)e.red_green_pixels / (double)e.frame_pixels : 0.0;
    }

    /** Which of the tests refused this camera, or `None`. */
    enum class Refused
    {
        None,
        NoFrame,
        TooMuchRedGreen,
        RingNotTraced,
        TooFewPoints
    };

    inline Refused verdict(const Evidence &e, const Limits &limits = Limits())
    {
        if (e.frame_pixels <= 0)
        {
            return Refused::NoFrame;
        }
        // Asked before the ring, because it is the one that explains the ring: a flooded
        // mask is why the rays found nothing, and "too much of this picture is dartboard
        // red and green" is the sentence that sends somebody to look at where the camera
        // is pointed rather than at the ellipse fitter.
        if (redGreenFraction(e) > limits.max_red_green_fraction)
        {
            return Refused::TooMuchRedGreen;
        }
        if (!e.traced_doubles)
        {
            return Refused::RingNotTraced;
        }
        if (e.outer_points < limits.min_outer_points)
        {
            return Refused::TooFewPoints;
        }
        return Refused::None;
    }

    /**
     * Why this camera is refused, or an empty string when it is not.
     *
     * #1321's rule, which this obeys: the count is asserted against the threshold that
     * refused it in the same sentence, so a line reporting the wrong number can be seen
     * to be wrong without knowing anything about the pipeline.
     */
    inline std::string refusal(const Evidence &e, const Limits &limits = Limits())
    {
        const int percent = (int)(redGreenFraction(e) * 100.0 + 0.5);
        const int allowed = (int)(limits.max_red_green_fraction * 100.0 + 0.5);

        switch (verdict(e, limits))
        {
        case Refused::NoFrame:
            return "no frame to look at";
        case Refused::TooMuchRedGreen:
            return "is not looking at the dartboard: " + std::to_string(percent) +
                   "% of its frame keys as dartboard red or green and this check allows at most " +
                   std::to_string(allowed) +
                   "% -- a board's doubles and trebles are a few per cent of a frame, and skin in warm "
                   "room light is most of one";
        case Refused::RingNotTraced:
            return "is not looking at the dartboard: no doubles ring could be traced around the bull";
        case Refused::TooFewPoints:
            return "is not looking at the dartboard: " + std::to_string(e.outer_points) +
                   " of 120 rays found the outer edge of a doubles ring and this check needs at least " +
                   std::to_string(limits.min_outer_points);
        default:
            return "";
        }
    }

    inline bool seesBoard(const Evidence &e, const Limits &limits = Limits())
    {
        return verdict(e, limits) == Refused::None;
    }

    /** The numbers themselves, for the log, so a refusal can be argued with. */
    inline std::string measured(const Evidence &e)
    {
        return "red_green=" + std::to_string(e.red_green_pixels) + "/" + std::to_string(e.frame_pixels) +
               " (" + std::to_string((int)(redGreenFraction(e) * 100.0 + 0.5)) + "%)" +
               " ring=" + std::string(e.traced_doubles ? "traced" : "not traced") +
               " outer_points=" + std::to_string(e.outer_points) +
               " inner_points=" + std::to_string(e.inner_points);
    }
}
