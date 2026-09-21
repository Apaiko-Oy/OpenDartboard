#pragma once

#include <opencv2/opencv.hpp>
#include <string>
#include <vector>

/**
 * #1498: THE BOARD STATES ITS OWN ORIENTATION IN PRINT, SO READ THAT.
 *
 * WHAT THIS REPLACES, AND WHY IT IS NOT A BETTER CLIP FINDER. `isStarCamera` requires
 * exactly four clip wires. The maintainer's board is a Winmau Blade 6: the numbers are
 * printed on the surround and there is NO WIRE NUMBER RING AT ALL, so the finder sees one
 * clip where both of its branches demand four. `ORIENTATION: 0 of 3`, every dart published
 * as #1346's asserted 20, and no camera placement or lighting change alters any of it --
 * it is the wrong instrument for this hardware rather than a detection failure. #1497
 * measured the alternative: all twenty printed numbers read off every camera of both
 * fixtures, worst cell 64 degrees oblique and 33 px of ring depth.
 *
 * WHAT IT IS. Twenty candidate rotations scored against the sequence the scorer already
 * carries, the best one taken, and a SCORE that makes a weak match refusable. You do not
 * have to read a glyph correctly: you need the rotation at which twenty half-read glyphs
 * best match a known ordering, which is massively over-determined. No new dependency --
 * the templates are cv::putText into a cv::Mat, which OpenCV has always had.
 *
 * THE FOUR PROPERTIES THAT MAKE IT SCORE RATHER THAN GUESS, in the order they matter:
 *
 *   1. A CANDIDATE IS AN ASSIGNMENT, NOT A READING. Every candidate rotation pairs the
 *      twenty cells with the twenty numbers, each exactly once -- the sequence is a cyclic
 *      permutation, so the twenty candidates form a Latin square over the score matrix.
 *      Any bias belonging to a CELL (this one is bright, this one holds a GEN6 TEC
 *      sticker) and any bias belonging to a TEMPLATE (this one has more ink, this one
 *      likes the white circle that crosses every cell) therefore contributes THE SAME
 *      TOTAL to every candidate, and cannot move the argmax. `doubleCentre` removes both
 *      exactly rather than approximately, and after it the totals of the twenty sum to
 *      zero, which is what lets the winner be measured against a random assignment of the
 *      twenty numbers to the twenty cells in closed form (`minimumSeparation`).
 *
 *   2. THE CELL IS RECTIFIED INTO BOARD SPACE, so #1497's 4.4x perspective spread and
 *      26-64 degrees of local obliquity are undone by the same map the wire model already
 *      fits. A far cell arrives blurred, not small; a template is compared with a glyph
 *      of the same board size wherever on the board it is printed. This is why an
 *      isotropically scaled image-space template -- which #1497 warned would not match a
 *      wide, short, smeared far-side numeral -- is not what is matched.
 *
 *   3. NOTHING IS FITTED TO THIS BOARD (#1322). The annulus is the standard board's own
 *      millimetres; the glyph heights are a BRACKET of the annulus depth, every one of
 *      them tried for every cell and every number, so no size is assumed; the position
 *      of the glyph inside its cell is searched rather than stated, and the one constant
 *      that WAS borrowed for it -- the wire model's `kSnapDeg`, to hold a template near
 *      the middle of its own wedge -- was built, measured on both fixtures and removed
 *      again, because it cost confidence on every ring and changed no camera's answer;
 *      and the one place a
 *      radius would have had to be invented -- separating a numeral from the white circle
 *      it touches, which is what #1497 removed its own tape measure for needing -- does
 *      not arise, because nothing here segments a glyph. It correlates a whole cell.
 *
 *   4. THE HANDEDNESS IS MEASURED, NOT ASSUMED. Which way up a glyph lands in a rectified
 *      cell is a fact about how the board is printed and about the orientation of the
 *      plane map, and this file states neither: both half-turns are candidates, so there
 *      are forty rather than twenty, and `glyphsReadOutward` reports which won. Forty
 *      candidates over a Latin square still sum to zero, so the separation is read the
 *      same way.
 *
 * WHAT IT DOES NOT DO. It does not refuse a camera. A board on which no camera can read
 * its numbers is the board every release before this one had, and `orientation_processing`
 * warns by name against making an unanchored camera fatal: the honest answer there is
 * OD_CAMERA_WEDGES (#1363), stated out loud at default level so that a silent fallback --
 * which is how the asserted 20 went unnoticed for six weeks -- cannot happen again.
 */
namespace number_anchor
{
    /** What one camera's number ring said, and how strongly. */
    struct Reading
    {
        bool attempted = false;    // the ring could be sampled at all
        bool read = false;         // a rotation was found AND cleared the separation cut
        int wedge20WireIndex = -1; // the winner, in `wireEndpoints`' own index
        double separation = 0.0;   // deviations clear of a random assignment
        double margin = 0.0;       // winner minus runner-up, in the same units
        double total = 0.0;        // the winner's own doubly-centred total
        bool glyphsReadOutward = false; // which half-turn won
        int cells = 0;             // how many cells were sampled (20 or nothing)
        std::string why;           // one sentence, for the log and for the census
    };

    /**
     * Read the printed numbers and say where the sequence starts.
     *
     * Pure, and deliberately free of `DartboardCalibration`: it takes the four things the
     * board plane is built from, so a tester holds them too (#1338's shape) and this
     * header can be included by `orientation_processing` without a cycle.
     *
     * `endpoints` is the wire ring in its own image-angle order -- the order
     * `score_processing::findWedgeSlot` walks -- so the index answered is the index that
     * function wants for `start`.
     */
    Reading readTheNumbers(const cv::Mat &frame,
                           const std::vector<cv::Point2f> &endpoints,
                           const cv::RotatedRect &conic,
                           const cv::Point2f &bull,
                           double conicOfDoubles,
                           const std::string &debugDir = std::string());

    /**
     * `OD_NUMBER_ANCHOR=off` puts the orientation stage back on the clip wires alone, on
     * the SAME binary, so "a different build" is never a confound -- #1339's, #1442's,
     * #1450's, #1486's and #1489's shape, and `OD_ANCHOR=own`'s convention exactly:
     * anything but that one word is ignored rather than obeyed, so a typo reads in the
     * behaviour this stage is measured in rather than silently in the one it replaced.
     */
    bool notAsked();

    /**
     * `OD_NUMBER_ANCHOR=inner` reads the WRONG ANNULUS -- the scoring wedges between the
     * treble and the doubles rings, which carry no printed numbers -- with everything
     * else about the reader unchanged.
     *
     * This is the control, on the same binary, and it is the only thing that can tell
     * "the numbers were read" from "twenty cells of anything score a rotation". If a cut
     * admits this, the cut is measuring the machinery and not the board. It is also where
     * the cut came from: `testers/i1498_run.sh` prints both populations and the gap
     * between them.
     */
    bool readsTheNumberlessRing();

    /**
     * The separation below which a rotation is not believed, and the camera says so and
     * falls back.
     *
     * MEASURED RATHER THAN CHOSEN, and the sweep is in `testers/i1498_run.sh` section 3:
     * the same reader on the number ring of six cameras across both fixtures against the
     * same reader on the NUMBERLESS annulus of the same six -- twelve rings, of which six
     * have numbers printed in them and six do not.
     *
     *     number ring     3.50  3.52  3.75  4.12  4.28  4.54
     *     numberless ring 1.62  1.63  1.78  1.88  1.94  1.99
     *
     * 2.75 is the middle of the gap between the two populations rather than a row of a
     * table, and there is no row to choose: every cut from 2.0 to 3.5 refuses all six
     * numberless rings and admits all six number rings alike, so the table the sweep
     * prints is flat across that whole span. The honest half, said out loud: THE GAP IS
     * TWO FIXTURES WIDE AND NO MORE. Twelve rings is not a population, and what the cut
     * rests on is that the two halves of those twelve do not overlap and are nowhere near
     * overlapping -- the nearest real ring clears it by 27% and the nearest numberless one
     * misses it by 28%. A board worse lit than either of these is a board that falls back
     * and says so, which is the outcome this reader is built to make safe rather than the
     * one it is built to avoid.
     *
     * `OD_NUMBER_ANCHOR_MIN=<x>` moves it on one binary, which is how that sweep was
     * taken. A value outside (0, 40] names no separation forty candidates can produce, so
     * it is ignored rather than obeyed (`OD_WIRE_FIT_MIN`'s rule).
     */
    double minimumSeparation();
}
