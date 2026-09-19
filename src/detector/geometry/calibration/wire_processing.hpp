#pragma once

#include <opencv2/opencv.hpp>
#include <array> // Add this include
#include <cstddef>
#include <type_traits>

using namespace cv;
using namespace std;

// Forward declaration to avoid circular dependency - GLOBAL scope
struct DartboardCalibration;

namespace wire_processing
{
    /**
     * How many wires a dartboard has, and therefore how many the wire stage must find
     * before anything downstream may use its result. #1317: ONE number, in one place.
     *
     * Until this constant existed the same decision was written three times, as three
     * different numbers, in three files: `== 20` in wire_processing (isValid), `< 16` in
     * perspective_processing and `< 20` in score_processing. None of the three could fire,
     * because all three asked `std::array<Point2f, 20>::size()`, which is the template
     * argument and not anything that was detected -- so a board that found nine wires
     * reported twenty, read eleven Point2f past the end of a nine-element vector, and
     * calibrated.
     *
     * Twenty rather than sixteen, and that is a deliberate narrowing of the `< 16` that
     * perspective_processing carried. The wedge is the unit scoring works in:
     * score_processing::findWedgeSlot walks `(start + i) % wires` around the ring and the
     * slot it lands on indexes `dartboard_numbers`, a twenty-long sequence. With
     * nineteen wires every wedge past the gap is a different number than it should be, and
     * the answer is not "less accurate" but a plausible wrong score -- which is the class
     * ADR-0055 says must not be able to look trustworthy. Sixteen was never a threshold
     * anybody had reasoned about; it was a tolerance written beside a tautology, and PnP
     * needs four points, not sixteen. Restoring a looser number for the perspective fit is
     * one edit here, and it should be argued for rather than inherited.
     */
    constexpr int kWiresRequired = 20;

    // Configuration for wire detection methods
    struct WireDetectionConfig
    {
        // Current working method
        bool useHoughLinesDetection = false; // use Hough Lines detection method only

        // Hough line parameters
        int cannyLow = 50;
        int cannyHigh = 150;
        int houghThreshold = 30;
        int houghMinLineLength = 50;
        int houghMaxLineGap = 10;
        float houghAngleTolerance = 15.0f;    // Degrees tolerance for radial lines
        float houghDistanceTolerance = 20.0f; // Pixels tolerance from center
    };

    /**
     * The wire stage's endpoints, and how many of them there are.
     *
     * #1317: the count is a member rather than a template argument, so `.size()` answers
     * what was found. Every reader of `wireEndpoints` already spelled its question with
     * `.size()`, `[]`, `empty()` or a range-for, so giving those four the true count is
     * the whole repair at the call sites -- and the three guards that could not fire now
     * can. A plain `std::vector` would say the same thing more briefly and is not
     * available: DartboardCalibration is written to the calibration cache with a raw
     * fwrite of sizeof(DartboardCalibration) bytes (utils/cache.hpp), so nothing in it may
     * own memory. Hence a fixed store plus a length, and the two asserts below it, which
     * are what would notice if that ever stopped being true.
     *
     * `add()` is the other half of the repair: filling the result is a push rather than an
     * indexed copy, so it cannot read past its source or write past its own end however
     * many wires it is handed. The old loop copied colorWires[0..19] with nothing looking
     * at colorWires.size().
     */
    struct WireEndpoints
    {
        std::array<Point2f, kWiresRequired> points{};
        int found = 0;

        size_t size() const { return (size_t)found; }
        bool empty() const { return found == 0; }
        bool full() const { return found >= kWiresRequired; }

        Point2f &operator[](size_t i) { return points[i]; }
        const Point2f &operator[](size_t i) const { return points[i]; }

        // Range-for and the algorithms stop at what was found, not at the capacity.
        Point2f *begin() { return points.data(); }
        Point2f *end() { return points.data() + found; }
        const Point2f *begin() const { return points.data(); }
        const Point2f *end() const { return points.data() + found; }

        /** Appends one endpoint; returns false, writing nothing, when already full. */
        bool add(const Point2f &endpoint)
        {
            if (full())
            {
                return false;
            }
            points[found++] = endpoint;
            return true;
        }
    };

    // Public data structures
    struct WireData
    {
        WireEndpoints wireEndpoints;

        /**
         * What the detector returned before anything was kept, which is what the log
         * reports. It is not always wireEndpoints.size(): the ensemble groups candidates
         * by angle and can return more than twenty groups -- camera 3 of
         * mocks/rig-20260918 returns twenty-one -- and those beyond the twentieth are
         * dropped as they were before this issue. Keeping the two numbers apart is what
         * lets "Found 21 wire boundaries, keeping the first 20" be said at all.
         */
        int wiresDetected = 0;

        int camera_index = -1;
        bool isValid = false;
    };

    // DartboardCalibration is written to the calibration cache with a raw fwrite of
    // sizeof(DartboardCalibration) bytes (utils/cache.hpp), so WireData may not own
    // memory. is_trivially_copyable is NOT the assertion, and that is a measurement
    // rather than a preference: cv::Point_ declares its own copy constructor on the
    // OpenCV this builds against, so the std::array this replaced would fail it too and
    // the assert would be about OpenCV instead of about this struct. What can be asserted
    // is the thing that would actually break the cache -- that the endpoints are stored
    // inline rather than behind a pointer. A std::vector here is 24 bytes and fails this.
    static_assert(std::is_standard_layout<WireEndpoints>::value,
                  "WireEndpoints is written to the calibration cache as bytes");
    static_assert(sizeof(WireEndpoints) == sizeof(Point2f) * kWiresRequired + sizeof(int),
                  "WireEndpoints must store its endpoints inline: the calibration cache "
                  "fwrites DartboardCalibration and cannot follow a pointer");

    // Public interfaces - using global DartboardCalibration
    /**
     * #1441: the wire stage's OWN region, and why the doubles ring is outside it.
     *
     * WHAT THIS STAGE READS. `detectMetalWires` keeps the dark, red and green pixels
     * inside the fitted doubles ellipse, subtracts the colour stage's green from them,
     * and hands the surviving blobs to `findWiresByColorTransitions`, which takes each
     * blob's angular extent and intersects the two edges with `outerDoubleEllipse`. So
     * the blobs are the board's WEDGES, and one intact wedge is two wire endpoints. Ten
     * wedges surviving as ten blobs is the twenty `kWiresRequired` wants.
     *
     * WHY THE RING BREAKS THEM. The doubles ring is a coloured annulus across the outer
     * end of every wedge. Where the region reaches it, the green half of the ring is
     * subtracted and the red half is not, so a wedge is cut into an inner piece and an
     * outer one at some angles and left whole at others. The angular grouping downstream
     * then returns 21 and 22 as readily as 17 and 18 -- which is exactly the two-sided
     * error #1441 was filed on, and a board does not grow wires.
     *
     * WHY IT DID NOT BREAK THEM BEFORE #1378, WHICH IS THE PART WORTH READING. The
     * region has always been drawn from `ellipses.outerDoubleEllipse`, and that ellipse
     * is downstream of the board finder's region: on mocks/rig-20260918 the colour stage
     * measures the TREBLE ring (#1378), so at the old margin of 1.25 the search region
     * cut the doubles ring and the fit COLLAPSED onto what was left -- 91,849 px against
     * the 258,582 px it really is. The wire stage was therefore reading inside a circle
     * the size of the treble ring, nowhere near the doubles, and it found twenty wires
     * because of a defect rather than in spite of one. #1378 repaired the fit, and this
     * stage read out to a real doubles ring for the first time.
     *
     * So the two stages were never sharing a region; they were sharing a MISTAKE, and
     * repairing it downstream is what exposed this one. #1437 measured the pair on one
     * binary and no value of the board finder's margin buys both: the board is repaired
     * from 1.60 up and mocks/rig-20260918/cam_2.mp4 refuses 1 held frame in 15 only at
     * 1.25, where the board is collapsed. The wire stage needs its own number, and it is
     * this one.
     */
    struct WireRegionParams
    {
        /**
         * How much of the FITTED doubles ellipse the wire stage's region is.
         *
         * Derived, not fitted. `detectMetalWires` buffers this region by 5% before its
         * morphology, so the outermost thing the stage can read is 1.05 of it, and what
         * that must not reach is the doubles ring's INNER edge. A board's doubles ring
         * is 162 mm inside and 170 mm outside, so the ring's inner edge is 162/170 =
         * 0.9529 of the ellipse that is fitted to its outer edge, and
         *
         *     0.9529 / 1.05 = 0.9076
         *
         * is the region whose own buffer stops there. Nothing is fitted to a rig: the
         * arithmetic is the board's millimetres, the way #1378's 2.107 is 225.5/107.
         *
         * Measured on the two fixtures, refusals over #1437's fifteen-frame window, one
         * binary, `OD_WIRE_REGION_MARGIN` sweeping this number:
         *
         *   scale   mocks 1,2,3   rig 1,2,3          rig cam_2's fitted board
         *   0.78    0, 3, 6       4, 11, 2           258,582 px throughout: this
         *   0.80    2, 3, 6       3,  7, 3           number cannot move the fit, which
         *   0.82    1, 3, 6       4,  1, 3           is the whole point of splitting it
         *   0.84    0, 3, 6       4,  0, 3           off the board finder's margin
         *   0.86    0, 3, 6       4,  0, 3
         *   0.88    0, 3, 6       4,  0, 3
         *   0.9076  0, 3, 6       3,  0, 3   <-- every clip at its pre-#1378 count
         *   0.92    3, 3, 6       4,  0, 3
         *   0.94    0, 3, 6       4,  0, 3
         *   0.96    0, 3, 6       4,  0, 3
         *   0.98    0, 3, 6       3,  4, 3
         *   1.00    0, 3, 6       3,  8, 3   <-- the region before this issue, exactly
         *
         * The repair is the PLATEAU and not the row: rig cam_2 is whole from 0.84 to
         * 0.96, thirteen points wide, and the cliffs at 0.80 and 0.98 are the region
         * failing to clear the ring on one side and losing the wedge on the other. The
         * +-1 wobble on the other five clips is a frame or two tipping between 19 and 20
         * or 20 and 21 and is jitter rather than structure -- it is written down here
         * because a reader who sees 0.9076 alone holding all six would reasonably suspect
         * the number of having been chosen for it, and it was not: it was computed from
         * the millimetres above before this sweep was run.
         *
         * `OD_WIRE_REGION_MARGIN=<x>` moves it at run time on one binary.
         * `OD_WIRE_REGION=doubles` puts the region back at the doubles ellipse itself,
         * which is row 1.00 and is what every commit before this one read.
         */
        float regionOfDoublesEllipse = 0.9076f;
    };

    /**
     * The wire stage's region, as an ellipse: a fraction of the doubles ring fitted at
     * STEP 6, which is what both of `detectMetalWires`'s own masks are drawn from.
     *
     * It is asked of the calibration rather than stored on it because the calibration is
     * fwritten to the cache byte for byte (utils/cache.hpp) and a second ellipse in it
     * would be a second thing to keep true of the first.
     */
    RotatedRect regionOf(const DartboardCalibration &calib,
                         const WireRegionParams &params = WireRegionParams());

    WireData processWires(const Mat &frame, const Mat &colorMask, const DartboardCalibration &calib, bool enableDebug = false, const WireDetectionConfig &config = WireDetectionConfig());
}