#pragma once

#include <opencv2/opencv.hpp>
#include <string>
#include <cmath>

/**
 * #1423: WHICH RING DID THE COLOUR STAGE MEASURE?
 *
 * `bull_processing::measureBoard` measures exactly one length -- the radius of the
 * smallest circle around the largest outermost contour of the red/green mask -- and
 * every stage below it is handed that length as though it were the board. On
 * `mocks/cam_*.mp4` it is: the doubles ring survives the colour mask, so the largest
 * region's boundary is the last coloured thing on the board. On `mocks/rig-20260918`
 * it is not: the doubles ring is there in the mask but broken into twenty arcs, each
 * enclosing almost nothing, so the largest OUTERMOST region is the closed TREBLE ring
 * and the span is 107/170 of the board while everything downstream believes it is 170.
 *
 * Nothing asked which. #1378's ROI margin, #1407's colour cutoff and #1416's
 * `max_radius_change` are three constants each guessing at it differently, and this
 * module is the measurement none of them owned.
 *
 * THE METHOD, AND THE THREE THAT WERE MEASURED AND REFUSED
 *
 * What is measured is WHAT LIES OUTSIDE the ring that was measured. A dartboard's last
 * coloured thing is the outer wire of the doubles ring, so:
 *
 *   - if the span landed on the DOUBLES ring, there is no dartboard colour outside it
 *     and the outermost coloured radius is the span itself -- a reach of 1.0;
 *   - if the span landed on the TREBLE ring, the doubles ring is still out there in the
 *     mask, arcs and all, at `outerDoubleRadius / outerTripleRadius` = 1.589 spans.
 *
 * So the reading is a ratio with two legal answers, 1.0 and 1.589, and the identity is
 * whichever of the two the reading is nearer to IN LOG -- which puts every boundary in
 * this file at sqrt(170/107) = 1.2605 times one of the two legal values and leaves no
 * number fitted to a rig (#1322). Measured over 720 rays on both fixtures, three looks
 * apiece, the twelve readings are 0.921 to 0.970 on the mocks and 1.559 to 1.760 on the
 * rig: two clusters sitting on the two legal answers, with a factor of 1.61 of clear air
 * between them and no reading closer than 14% to any boundary.
 *
 * THE RING'S OWN WIDTH was the first candidate and it was refused on a measurement that
 * is worth stating exactly, because it is nearer to working than it sounds. The
 * arithmetic is right: a doubles ring is 8 mm of a 170 mm radius, 0.047 of it, and a
 * treble 8 mm of 107, 0.075. Measured as the median over 720 rays of the outermost
 * coloured run, the mocks read 0.042 to 0.061 of their span and the rig 0.071 to 0.077 --
 * two clusters that do not overlap, sitting roughly where the millimetres say.
 *
 * What it cannot do is separate them against a cutoff nobody fitted. The boundary the
 * geometry gives is the geometric mean of the two, sqrt(0.047 x 0.075) = 0.0593, and one
 * of the nine mocks looks reads 0.0608 -- a DOUBLES ring called a treble ring, which is
 * this issue's own failure with the sign flipped. The mask is not the board: the colour
 * stage's closing kernels add the same two or three pixels to every ring edge, and the
 * rig's board is 195 px of span where the mocks' is 291 to 335, so one absolute inflation
 * is worth twice as much of the smaller ratio. Making it work means moving the cutoff off
 * the millimetres and onto the two rigs we happen to have, and #1322 is entirely about
 * that.
 *
 * The reach has the same shape of argument and survives it: its boundary is also a
 * geometric mean of two legal answers, and no look is nearer than 1.14x to one.
 * `i1423_ring_census` prints the width on every row and section 4 of `i1423_run.sh`
 * asserts that it still misclassifies, because a method refused on a measurement is only
 * refused for as long as the measurement can be re-taken.
 *
 * WHAT IS INSIDE IT was the second, and it is the same question asked where the answer
 * is weaker: inside a treble ring a board is black and cream out to the 25-ring, and
 * inside a doubles ring there is a treble ring -- but the treble ring is exactly the
 * thing whose presence in the mask is in doubt, so a board whose colour is failing reads
 * as a treble ring by having lost its trebles. Outside is the stronger direction because
 * an ABSENCE out there is what a doubles reading claims, and the rig's doubles ring is
 * in the mask even when it is in twenty pieces: the reach is a percentile over rays, so
 * it needs the ring to be present, not to be closed, which is the whole reason the span
 * could not be told from the ring in the first place.
 *
 * REFUSING THE CAMERA is #1389's shape and it is not what an unreadable ring gets here.
 * See `Sighting::boardRadiusOfSpan` below for what it gets instead, and why.
 *
 * THE ONE THING THIS MEASUREMENT NEEDS, AND WHO ALREADY GUARANTEES IT. A reach of 1.0
 * is a claim that there is no colour outside the span, and a frame can make that true by
 * cutting the board off. ADR-0079 section 2 is exactly that guarantee and it is already
 * enforced at STEP 1.5, on the same mask, one statement earlier: the union of the kept
 * colour is inside the frame or the camera does not calibrate. Both fixtures clear the
 * frame edge -- the mocks by 119 to 148 px and the rig by 39 to 75 -- so where this
 * module is called the doubles ring is in the picture if it is anywhere.
 */
namespace ring_identity
{
    // MSVC does not define M_PI, and #1355 is why this is a header-local constant rather
    // than a define in CMakeLists.txt.
    inline constexpr double kPi = 3.14159265358979323846;

    /** Which ring the span landed on. */
    enum class Ring : int
    {
        Unknown = 0,
        Doubles = 1,
        Trebles = 2,
    };

    /**
     * The two millimetre figures this whole module is derived from, quoted from
     * `perspective_processing::DartboardSpec` rather than restated as a ratio, so that a
     * reader can check them against a dartboard.
     */
    struct Spec
    {
        double outerDoubleRadius = 170.0; // mm, the outer wire of the doubles ring
        double outerTripleRadius = 107.0; // mm, the outer wire of the treble ring

        /** 1.589: what a span that landed on the treble ring has to be multiplied by. */
        double boardRadiusOfTrebleSpan() const { return outerDoubleRadius / outerTripleRadius; }

        /**
         * 1.2605: the boundary between the two legal answers, and the only shape of
         * number in this file. It is the geometric mean of 1.0 and 1.589 -- halfway
         * between them in ratio, which is the only halfway that means anything for a
         * quantity whose error is multiplicative. Every threshold below is this applied
         * to one of the two legal answers, above or below, so there are four boundaries
         * and not one of them was chosen.
         */
        double band() const { return std::sqrt(boardRadiusOfTrebleSpan()); }
    };

    /**
     * What one camera's ring identity reads. A plain struct: it is carried in
     * DartboardCalibration, which is written to the cache with a raw fwrite, so nothing
     * here may own memory (geometry_calibration.hpp's rule).
     */
    struct Sighting
    {
        Ring ring = Ring::Unknown;
        double reach = 0.0;   // outermost coloured radius, in spans
        int rays_asked = 0;   // rays cast
        int rays_answered = 0; // rays that carried any colour at all

        bool stated() const { return ring != Ring::Unknown; }

        /**
         * The arithmetic this identity licenses: multiply the span by this and you have
         * the board's radius.
         *
         * WHAT AN UNKNOWN GETS, decided here and not left to each caller. It gets 1.589,
         * the treble reading -- which is to say it gets exactly what every downstream
         * constant already assumes today. `color_processing::boardRadiusOfBoardSpan` is
         * 1.589 unconditionally and #1378's ROI margin is the worst case for the same
         * reason: a span taken for smaller than it is widens a window that KEEPS and
         * grows a region the frame will clip, and both of those failures are free where
         * the opposite one is not.
         *
         * SO AN UNREADABLE RING IS NOT A REFUSED CAMERA, and that is a decision rather
         * than an omission. #1389's refusal is the right answer to "there is no dartboard
         * in this picture", which is a question STEP 1 and STEP 1.5 have already asked and
         * answered by the time anything here runs. What is unreadable here is narrower:
         * a board WAS found and measured, and the one thing that could not be read is
         * which of its rings the measurement landed on. Refusing that camera would turn
         * a fact nobody knew until today into a rig that stops calibrating, which is a
         * regression dressed as rigour.
         *
         * What an Unknown does get is a WARN line naming the reading and both bands it
         * missed, because the fallback's cost is real and one-sided: on a board whose
         * span really is the doubles ring, 1.589 over-measures it by 59%.
         */
        double boardRadiusOfSpan() const
        {
            return (ring == Ring::Doubles) ? 1.0 : Spec().boardRadiusOfTrebleSpan();
        }
    };

    /**
     * Read the identity of the ring `span` was measured on.
     *
     * `colourMask` is the colour stage's output on the FULL frame -- the same picture
     * `bull_processing::measureBoard` was handed -- and `spanCentre`/`span` are what it
     * measured. Casts rays from that centre and reports the outermost radius carrying
     * colour, as a multiple of the span.
     */
    Sighting identify(const cv::Mat &colourMask, const cv::Point2f &spanCentre, double span);

    /** One line, in words and numbers, for the log and for a tester to read. */
    std::string sentence(const Sighting &sighting);

    /** `OD_RING=span` restores the state before this issue: no identity is ever stated. */
    bool identityNotAsked();
}
