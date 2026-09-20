#pragma once

#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

#include "geometry_calibration.hpp"
#include "orientation_processing.hpp" // #1450: wedgeCanBeRead, the seal's own question

// #899: whether the board in front of a camera now is the board that camera was
// calibrated on.
//
// #1388 added a second comparison of the same kind, `fingerprint`, and its docblock says
// what it is for. This file still holds comparisons and no policy. The policy -- what a board does about a
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
        // the other two cannot see.
        //
        // #1416: WHAT THERE IS, NOW THAT SOMEBODY HAS LOOKED. There is still no measured
        // positive for this term -- no camera anywhere in either fixture has been
        // measured moving along its own axis -- and there are 44 measured NEGATIVES, on
        // footage of a rig nobody touched. `testers/i1388_disturbance` calibrates from
        // the clean opening of a clip and re-calibrates once a second across the rest, by
        // the same two calls `Scorer::attemptRecovery` makes: 348 samples over the six
        // clips, 45 disagreements, 44 of them this term and one a bull shift.
        //
        // EVERY ONE OF THE 44 IS A RING THE STAGE RENAMED, AND NONE IS A CAMERA. The
        // radius cannot tell the two apart, because it is the thing both of them move, so
        // the instrument takes two witnesses that are not the radius. The fitted ring
        // measured against the 50-bull, which is carved out of the red by radius and
        // fitted from its own mask: a camera really pushed along its own axis scales
        // every ring in the picture by one factor, so that RATIO is invariant and only
        // the radius moves, while a ray trace that fitted a different ring moves the
        // ratio by the factor it moved the radius by. Measured across the 44, the radius
        // moved and the ratio moved with it, a median of 0.50 percentage points apart.
        // And the ring's own width as a fraction of its own outer radius -- a doubles
        // ring runs 162 -> 170 mm and a treble 99 -> 107 mm, both 8 mm wide but 0.047 and
        // 0.075 of their own outer radius -- which moved by a median factor of 1.566
        // against the 1.589 those millimetres predict.
        //
        // The two fixtures disagree about WHICH WAY, and that is what makes it one
        // finding rather than two numbers. On the mocks the held calibration fits the
        // doubles ring and play occasionally fits the treble: the ring shrinks to a
        // median 0.6087 of itself and the term reads ~39%. On the rig it is the mirror
        // image -- the held calibration fits the TREBLE and the disagreements are the
        // moments the stage briefly got the doubles right, 1.647x, reading ~64%. The
        // same two populations stand behind both: ring-against-bull 22.6-22.9 where the
        // doubles were fitted and 13.4-14.1 where the treble was, on both fixtures, and
        // 0.6087 against 1/1.647 = 0.6071 for one ratio measured in opposite directions.
        // The board's own millimetres put it at 107/170 = 0.629. This is #1423, whose
        // ground is `mask_processing` handing the ray trace the LARGEST CONNECTED
        // COMPONENT of the red/green mask and nothing anywhere asking which ring that is.
        //
        // SO THE 5% IS LEFT ALONE, AND THAT IS A MEASUREMENT RATHER THAN AN OMISSION.
        // Across all 348 samples on both trees there is no sample whose radius change
        // lies between 3.29% and 38.26%. The band is empty, an order of magnitude wide
        // on each side of this constant, and every tolerance from 3.3% to 38.2% returns
        // exactly the same verdict on every sample -- so no value in it is better or
        // worse than any other and there is nothing here to fit. The only values that
        // WOULD move a verdict are below the re-measurement noise, which manufactures
        // false positives out of nothing, or above 66%, which is a term blind to a camera
        // that moved two thirds of the board's radius and is retiring it under another
        // name. Retiring it is #1423's to decide once a stage can say which ring it
        // measured; widening it to fit the renames would leave a board scoring on a
        // geometry that is 0.629 of its board with nothing left to notice.
        /**
         * #1423, WHAT CHANGED FOR THIS TOLERANCE. Not its value -- 0.05 stands -- but the
         * reading it is asked of.
         *
         * 44 of this tolerance's 45 measured false positives are the radius term, stable
         * at about 64% on `mocks/rig-20260918`, and #1388's sentence is that a doubles
         * ring does not change size by 39% and hold there for six seconds. 64% is
         * 107/170: the two calibrations were measuring two different rings and the
         * tolerance was being asked whether one board had moved.
         *
         * STEP 1.6 now names the ring behind each radius, on `Evidence::ring_measured`.
         * So a comparison between two calibrations can ask whether they measured the SAME
         * ring before asking whether the radius moved, and a change of ring is a different
         * event from a bumped rig -- which is the distinction that turns 44 false
         * positives into 44 correct readings of something else. Wiring that in is #1416's
         * work; this issue supplies the statement it needs and moves nothing here.
         */
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

    /**
     * #1450's falsification, in the shape od_fix, #1339, #1340, #1378, #1392 and #1442
     * established: one binary, the spelling chosen at run time, so "different build" is
     * never a confound.
     *
     * OD_SEAL=star restores exactly what `fingerprint` sealed before #1450 -- the star
     * MEASUREMENT alone, with nothing about whether the board may be READ -- and is how a
     * camera whose `anchored` moved can be made to slip past `geometryBreach()` again on
     * the very binary that now catches it. Anything but that exact word is ignored rather
     * than obeyed.
     *
     * There is deliberately no word for the reverse. Sealing what the scorer reads is
     * what this line IS, not a mode it is in.
     */
    inline bool sealsOnlyTheStarMeasurement()
    {
        static const bool v = []
        {
            const char *e = std::getenv("OD_SEAL");
            return e && std::string(e) == "star";
        }();
        return v;
    }

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

    /**
     * #1388: the geometry the board is scoring with, reduced to one line.
     *
     * The second question this file answers, and it is the same KIND of question as the
     * first -- two calibrations, are they the same rig -- asked of a different pair.
     * `measure` above compares the held calibration against a fresh one taken from the
     * camera; this compares the held calibration against ITSELF as it was when the board
     * was calibrated. It is a comparison and not a policy: what a board does about a
     * geometry that changed under it is scorer.cpp's, like everything else in the
     * lifecycle.
     *
     * WHY IT EXISTS. ADR-0080 keeps #899's whole protection -- a board never scores on
     * geometry that has not been confirmed -- while making a `Moved` verdict survivable.
     * That is one sentence in the ADR and a standing obligation in the code: after #1388
     * a board recovers from a disagreement and goes on scoring, so "it resumed on the
     * geometry it held" stopped being obvious from the control flow and became something
     * that has to be checked. A test can only check it if there is something to read,
     * and this is it: seal the line at the end of `initialize`, read it on every cycle,
     * and a board scoring on anything but the calibration it started with says so by
     * name instead of scoring quietly.
     *
     * WHAT IS IN IT. Every field a dart's score is derived from and nothing else -- the
     * bull the score is measured from, the angle the wedges are counted from, the size of
     * the ring the radius is taken as a fraction of, the slot's own camera index, and
     * whether the slot is scoring at all. Those are exactly the fields `measure` reads,
     * which is not a coincidence: a change this cannot see is a change that could not put
     * a dart in the wrong wedge either. It is deliberately NOT a hash of the struct's
     * bytes: `DartboardCalibration` carries debug-only members and a capture timestamp,
     * and a line that changes when the timestamp does would be a guard nobody could keep
     * green.
     *
     * #1450: WHETHER THE BOARD MAY BE READ, WHICH IS NOT WHETHER A STAR WAS MEASURED.
     *
     * The paragraph above says "every field a dart's score is derived from", and until
     * #1450 the line did not keep it. `star=` is what the ORIENTATION STAGE MEASURED.
     * What `score_processing` actually asks before it reads a wedge is
     * `wedgeCanBeRead(orientation)` -- #1449's one copy of the expression -- and it is
     * `anchored && wedge20WireIndex >= 0`, neither half of which was sealed.
     *
     * #1363 made `anchored` a separate field precisely because the two can disagree, and
     * they disagree in a SHIPPED configuration: OD_CAMERA_WEDGES, written for a Winmau
     * Blade 6 over a black surround where no camera anchors itself, sets `anchored` true
     * and leaves `isStarCamera` false. So the seal printed `star=0` for a camera the
     * scorer WILL read, and said nothing about the field that decided it.
     *
     * The consequence is #1447's and #1448's: an unanchored camera takes #1346's
     * asserted-twenty path and answers `dartboard_numbers[0]` for every tip on any ring.
     * A board could go on publishing the same number for every dart while `geometryBreach`
     * reported the geometry unchanged.
     *
     * WHY `wedge20=` AS WELL, WHICH IS MORE THAN THE FIELD THE ISSUE NAMED. `read=` is a
     * bit, and it stays 1 while the index under it moves. The index is the wire the wedge
     * count STARTS FROM -- `findWedgeSlot(pixel, calib, start, fraction)` with
     * `start = wedge20WireIndex` -- so an index that moved from 3 to 8 is every dart five
     * wedges wrong with `read=1` on both sides of the comparison. It is a field a dart's
     * score is derived from by the paragraph above's own test, and sealing it costs
     * nothing extra: the spelling changes once either way.
     *
     * WHAT IT COSTS TO CHANGE THIS LINE, MEASURED RATHER THAN ASSUMED. Nothing. The
     * fingerprint is NOT PERSISTED: `sealed_geometry` is a member of GeometryDetector,
     * taken at the end of every `initialize()` and compared only against fingerprints
     * built in the same process from the same live `calibrations`. No file holds one --
     * the cache holds `DartboardCalibration` records and no string -- so there is no old
     * spelling anywhere for a new binary to read as a breach. A board restarting across
     * this change re-seals in the new spelling and compares like with like.
     *
     * Nor does it force a re-calibration. The cache's refusal is `record_bytes !=
     * sizeof(DartboardCalibration)` (#1330), and this change adds no field to that struct
     * -- it reads two that #1363 already put there. A cached calibration loads, has the
     * configured anchors applied to it as before, and is sealed with the same line a
     * fresh calibration of the same rig would produce. `testers/i1450_seal_check.cpp`
     * measures that as a round trip through the cache's own raw-bytes copy.
     *
     * Both of those are the reason no version field is wanted here, one step past the
     * maintainer's decision on #1450: there is no cost to amortise. A version would be
     * machinery guarding a boundary nothing crosses.
     */
    inline std::string fingerprint(const std::vector<DartboardCalibration> &held)
    {
        auto twoPlaces = [](double v)
        {
            std::string s = std::to_string(v);
            const std::string::size_type dot = s.find('.');
            return dot == std::string::npos ? s : s.substr(0, dot + 3);
        };

        std::string line;
        for (size_t i = 0; i < held.size(); i++)
        {
            const DartboardCalibration &calibration = held[i];
            line += (line.empty() ? "" : " | ") + std::string("camera ") + std::to_string(i + 1) +
                    " index=" + std::to_string(calibration.camera_index) +
                    " scoring=" + (calibration.sees_board ? "1" : "0") +
                    " bull=" + std::to_string(calibration.bullCenter.x) + "," +
                    std::to_string(calibration.bullCenter.y) +
                    " star=" + (calibration.orientation.isStarCamera ? "1" : "0");
            if (!sealsOnlyTheStarMeasurement())
            {
                line += std::string(" read=") +
                        (orientation_processing::wedgeCanBeRead(calibration.orientation) ? "1" : "0") +
                        " wedge20=" + std::to_string(calibration.orientation.wedge20WireIndex);
            }
            line += " angle=" + twoPlaces((double)calibration.orientation.angleOffsetFromSouth) +
                    " radius=" + twoPlaces(meanRadius(calibration));
        }
        return line.empty() ? std::string("no camera holds a calibration") : line;
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
            // #1416: "the ring fitted as the doubles" and not "the doubles ring". The
            // stage names what it fitted rather than measuring it, and on every one of
            // the 44 disagreements measured on untouched footage it had fitted the
            // TREBLE ring under this name. A line saying the doubles ring changed size
            // by 39% cannot be argued with the way #1321 means, because in each of those
            // cases it was not the doubles ring and the line gave a reader no way to
            // suspect it.
            line += ", the ring fitted as the doubles changed size by " +
                    twoPlaces(movement.radius_change * 100.0) +
                    "% of an allowed " + twoPlaces(limits.max_radius_change * 100.0) + "%";
        }
        else
        {
            line += ", the doubles ring was not fitted in both pictures so the size was not compared";
        }
        return line;
    }
}
