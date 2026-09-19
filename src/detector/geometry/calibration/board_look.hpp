#pragma once

#include <cmath>
#include <cstdio>
#include <cstdlib>
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
//   red_green_pixels  the doubles mask, i.e. how much of the picture keys as dartboard
//                     red or dartboard green. A dartboard's doubles and trebles are a
//                     thin annulus around a bull; skin in warm room light is a filled
//                     blob. Measured on the rig: 27,624 pixels for a board camera
//                     against 237,036 for the webcam pointed at a face, in the same run,
//                     at 1280x720.
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
//
// ===========================================================================
// #1392: WHAT THOSE PIXELS ARE DIVIDED BY, AND WHY IT IS NOT THE FRAME.
// ===========================================================================
//
// Until #1392 the two numbers above were turned into a verdict by dividing the first of
// them BY THE FRAME. That is a statement about where somebody bolted the camera, not
// about what it is looking at. Ring pixels scale with the square of how much of the
// frame the board fills, so the same board camera that measured 3.0% of its frame
// measures 12.0% behind a lens of twice the focal length, or mounted at half the
// distance -- and 12% was the line. A rig that is merely CLOSER was refused outright,
// the whole camera dropped from the board, with a sentence saying it was not looking at
// a dartboard. #1340's fraction-of-frame census found this one alongside #1393 and
// #1394, and this is the dangerous one: its failure is refusal rather than a fallback.
//
// The headroom was never theoretical. color_processing records boards already enclosing
// 19.4% to 20.6% of the frame on mocks/cam_*.mp4, and the only reason the gate had not
// fired is that a ring is thinner than the disc it encloses -- an accident of ring
// width, not anything the check stated.
//
// SO THE QUESTION IS ASKED ABOUT THE BOARD. `bull_processing::measureBoard` has already
// measured a radius by the time any of this is asked -- on the FULL frame, at STEP 1,
// before a region is drawn -- so the denominator is a circle of that radius:
//
//       board_disc_colour = red_green_pixels / (pi * board_span_px^2)
//
// "What share of the circle this camera's own board measures out is coloured." Double
// every pixel and the numerator goes up by four and so does the denominator; the number
// does not move. That is the whole of the repair, and testers/phases1392 measures it by
// scaling one clip rather than by arguing it.
//
// THE NUMERATOR IS A MASK AND NOT AN ANNULUS, which is the trap in the arithmetic. A
// dartboard's doubles ring is a ring of known width -- 170 mm outer, 162 mm inner in
// perspective_processing's own PhysicalBoard, so (170^2-162^2)/170^2 = 9.2% of the disc
// it sits in -- but `countNonZero(masks.doublesMask)` counts what the COLOUR mask kept
// and what the morphology then did to it, and #1378 measured that on
// mocks/rig-20260918 the doubles ring drops out of that mask altogether, leaving the
// TREBLE ring as the largest surviving contour. A treble ring against its own enclosing
// circle is (107^2-99^2)/107^2 = 14.4%. Both are rings and both are small; neither is
// 9.2% on the nose, and a threshold derived from the physical annulus alone would be a
// number fitted to a mask nobody measured. The numbers in Limits below are measured on
// both fixtures on one binary, and the physical shares above are what they are checked
// AGAINST rather than what they are taken from.
namespace board_look
{
    /**
     * #1392's falsification, in the shape od_fix, #1339, #1340 and #1378 established:
     * one binary, the measure chosen at run time, so "different build" is never a
     * confound.
     *
     * OD_LOOK=frame restores exactly what this file decided before #1392 -- the
     * numerator divided by the FRAME, the 12% line, and the sentence that quoted a share
     * of a frame -- and is how a board mounted closer can be made to lose its whole
     * camera again on the very binary that calibrates it. Anything but that exact word
     * is ignored rather than obeyed.
     */
    inline bool measuredAgainstTheFrame()
    {
        static const bool v = []
        {
            const char *e = std::getenv("OD_LOOK");
            return e && std::string(e) == "frame";
        }();
        return v;
    }

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

        // #1331 / ADR-0079 §2: whether the whole board is in this camera's frame. The
        // board is found on the full frame before any region is drawn around it, and if
        // what the colour stage kept of that frame runs off the frame's own edge then this
        // camera does not see a whole board -- a fact about where the hardware is bolted,
        // not about anybody's aim (ADR-0079 §3). `board_edge_gap` is the smallest distance
        // in pixels from the extremes of that kept colour to any of the four frame edges,
        // so a refusal carries a number rather than a verdict; a gap of 0 or less is a
        // board with part of itself outside the picture.
        bool board_clipped = false;
        int board_edge_gap = 0;

        // #1392: the radius `bull_processing::measureBoard` measured on the FULL frame,
        // which is what `red_green_pixels` is divided by. Zero where no board was found
        // at all -- a camera with no board in its picture is refused by the stage that
        // could not find one, in that stage's own words, and has no disc to be a share
        // of. `frame_pixels` is kept beside it because it is still what NoFrame is asked
        // of, because it is what OD_LOOK=frame divides by, and because a reader
        // comparing the two is reading the whole of this issue.
        double board_span_px = 0.0;
    };

    /**
     * Where the line is drawn. Stated as a struct rather than as constants so that a
     * test can move it and watch the answer move -- a filter nothing can fail is not
     * evidence that anything was filtered.
     */
    struct Limits
    {
        // #1392. MEASURED, on one binary, on every fixture this repository has, with
        // `board_span_px` as the denominator and the mask each site really hands over:
        //
        //   MEASURED_TABLE_GOES_HERE
        //
        double max_board_disc_colour = 0.30;

        // The pre-#1392 line, kept for OD_LOOK=frame and reachable by nothing else. 12%,
        // sitting in the gap between the two measurements #1318 took -- 3.0% for a board
        // camera and 25.7% for a face, both at 1280x720 in one run -- rather than beside
        // either of them. Both of those numbers are shares of a frame and so both move
        // when a camera moves, which is what #1392 is about; they are kept here because a
        // falsifier that reproduces another issue's numbers to the decimal is restoring
        // the old stage, and one that merely refuses proves nothing.
        double max_red_green_fraction = 0.12;
    };

    /** The area of the circle the board was measured out to, in pixels. */
    inline double boardDiscPixels(const Evidence &e)
    {
        return M_PI * e.board_span_px * e.board_span_px;
    }

    /** A share as a percentage with one decimal, because 5.7 and 6 are different claims. */
    inline std::string percentOf(double share)
    {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.1f", share * 100.0);
        return std::string(buf);
    }

    /**
     * The share of the picture, or of the board's own circle, that keys as dartboard red
     * or green -- whichever this run is deciding on.
     */
    inline double colourFraction(const Evidence &e)
    {
        if (measuredAgainstTheFrame())
        {
            return e.frame_pixels > 0 ? (double)e.red_green_pixels / (double)e.frame_pixels : 0.0;
        }
        const double disc = boardDiscPixels(e);
        return disc > 0.0 ? (double)e.red_green_pixels / disc : 0.0;
    }

    /** The line this run is deciding against, in the same terms `colourFraction` is in. */
    inline double colourAllowed(const Limits &limits)
    {
        return measuredAgainstTheFrame() ? limits.max_red_green_fraction : limits.max_board_disc_colour;
    }

    /** Which of the tests refused this camera, or `None`. */
    enum class Refused
    {
        None,
        NoFrame,
        TooMuchRedGreen,
        BoardClipped,
        RingNotTraced
    };

    inline Refused verdict(const Evidence &e, const Limits &limits = Limits())
    {
        if (e.frame_pixels <= 0)
        {
            return Refused::NoFrame;
        }
        // Asked before the ring, because it is the one that explains the ring: a flooded
        // mask is why the rays found nothing, and "too much of this board's own circle is
        // dartboard red and green" is the sentence that sends somebody to look at where
        // the camera is pointed rather than at the ellipse fitter.
        if (colourFraction(e) > colourAllowed(limits))
        {
            return Refused::TooMuchRedGreen;
        }
        // Asked after the flood and before the ring, and the order is the argument. A
        // camera pointed at a face is clipped too -- a face runs off the frame -- and
        // "too much of this is dartboard red and green" is the sentence that sends
        // somebody to the right place. A camera really looking at a board that the frame
        // cuts is not flooded, so it reaches this, and it must not be told instead that
        // no ring could be traced: nothing tried to trace one.
        if (e.board_clipped)
        {
            return Refused::BoardClipped;
        }
        if (!e.traced_doubles)
        {
            return Refused::RingNotTraced;
        }
        return Refused::None;
        // #1392 removed a `min_outer_points` floor from the end of this function, and it
        // is worth one paragraph because the issue asked for the floor to be revisited
        // rather than for it to go.
        //
        // It was `50`, and the file said out loud that it could not fire: it was
        // `EllipseParams::minValidRays` said a second time, and `validOuterPoints` is
        // only ever written inside the `>= minValidRays` branch of processEllipse, which
        // is the same branch that sets `hasValidDoubles`. So `traced_doubles &&
        // outer_points < 50` is unreachable while the two numbers are equal, and it was
        // kept on the argument that this is where a STRICTER floor would be raised.
        //
        // There is no stricter floor to raise it to that is not fitted. The board cameras
        // this repository has ever measured score 68, 96, 103 and 110 of 120 rays, so a
        // floor that can fire has to sit in 51..67, and the only evidence anywhere near
        // that window is a real board camera at 68. Worse, #1340's own fixture -- a
        // doubles ring painted out over 150 degrees, which is 50 of the 120 rays -- is
        // deliberately refused by the WIRE stage, the one that can see the damage, and a
        // floor in the sixties would take that refusal away from it. A number nothing has
        // ever produced, chosen so that a guard can be said to fire, is exactly #1322.
        //
        // So the floor lives in one place, `EllipseParams::minValidRays`, enforced where
        // the fit happens and printed there with its own count. `outer_points` is still
        // carried and still logged, because it is evidence a refusal is argued from; it
        // is no longer a second, identical gate pretending to be a first.
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
        const int percent = (int)(colourFraction(e) * 100.0 + 0.5);
        const int allowed = (int)(colourAllowed(limits) * 100.0 + 0.5);

        switch (verdict(e, limits))
        {
        case Refused::NoFrame:
            return "no frame to look at";
        case Refused::TooMuchRedGreen:
            // Two sentences, because there are two measures and a reader has to be able
            // to tell from the line alone which one refused this camera. The pre-#1392
            // wording is kept WORD FOR WORD on the OD_LOOK=frame side, so a falsifier run
            // reproduces the old line and not merely the old verdict.
            if (measuredAgainstTheFrame())
            {
                return "is not looking at the dartboard: " + std::to_string(percent) +
                       "% of its frame keys as dartboard red or green and this check allows at most " +
                       std::to_string(allowed) +
                       "% -- a board's doubles and trebles are a few per cent of a frame, and skin in warm "
                       "room light is most of one";
            }
            return "is not looking at the dartboard: " + percentOf(colourFraction(e)) +
                   "% of the circle its own board spans -- " + std::to_string((int)(e.board_span_px + 0.5)) +
                   " px of radius -- keys as dartboard red or green and this check allows at most " +
                   percentOf(colourAllowed(limits)) +
                   "% -- a board's doubles and trebles are a thin ring around a bull and fill a tenth to a "
                   "seventh of the circle they sit in however close the camera is bolted, and a face fills "
                   "most of one";
        case Refused::BoardClipped:
            return "is not looking at a WHOLE dartboard: the coloured region that is its doubles ring "
                   "comes within " +
                   std::to_string(e.board_edge_gap) +
                   " px of the edge of its own frame and a board wholly in shot leaves at least 1 -- so "
                   "part of this board is outside this camera's picture, which is where the cameras are "
                   "bolted rather than how they are aimed";
        case Refused::RingNotTraced:
            return "is not looking at the dartboard: no doubles ring could be traced around the bull";
        default:
            return "";
        }
    }

    inline bool seesBoard(const Evidence &e, const Limits &limits = Limits())
    {
        return verdict(e, limits) == Refused::None;
    }

    /**
     * The numbers themselves, for the log, so a refusal can be argued with.
     *
     * #1392: BOTH shares are printed, always, whichever one is deciding. The frame share
     * is the one this issue removed from the decision and it is the one a reader who has
     * moved a camera will want to see move; the board share is the one that decides and
     * the one that should not have moved. A line carrying only the deciding number would
     * make the whole claim of this issue unreadable from a log.
     */
    inline std::string measured(const Evidence &e)
    {
        const double frameShare = e.frame_pixels > 0 ? (double)e.red_green_pixels / (double)e.frame_pixels : 0.0;
        const double disc = boardDiscPixels(e);
        const double discShare = disc > 0.0 ? (double)e.red_green_pixels / disc : 0.0;

        return "red_green=" + std::to_string(e.red_green_pixels) +
               " of frame " + std::to_string(e.frame_pixels) +
               " (" + percentOf(frameShare) + "%)" +
               " of board disc " + std::to_string((long)(disc + 0.5)) +
               " at span " + std::to_string((int)(e.board_span_px + 0.5)) + " px" +
               " (" + percentOf(discShare) + "%)" +
               " ring=" + std::string(e.traced_doubles ? "traced" : "not traced") +
               " outer_points=" + std::to_string(e.outer_points) +
               " inner_points=" + std::to_string(e.inner_points);
    }
}
