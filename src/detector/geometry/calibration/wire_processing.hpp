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
     * #1441: the wire stage's OWN region, and why it is not the board finder's.
     *
     * `roi_processing` draws a region around a board that has NOT been found yet: its
     * input is `bull_processing::measureBoard`'s smallest circle around the largest
     * red/green contour, which #1378 measured landing on the TREBLE ring on a dull board,
     * so its margin has to carry the board's whole rim -- 225.5/107 = 2.107 -- for the
     * colour stage to reach the doubles ring at all. That is a search radius, and it is
     * correct for the consumer it was sized for.
     *
     * The wire stage asks a different question. It runs after STEP 6, so the doubles
     * ellipse has already been FITTED, and every wire it can ever return is an endpoint
     * ON that ellipse: `findWiresByColorTransitions` intersects each segment's angular
     * extent with `outerDoubleEllipse` and `detectMetalWires` masks its own work to
     * 1.05x it and then to 0.95x it. So the region it wants is the thing it is looking
     * inside, at the buffer it already states.
     *
     * Until this issue nobody drew that region: the wire stage was handed the colour
     * stage's output, which is computed inside the board finder's region, and #1378
     * widening that region from 243 px to 410 px on mocks/rig-20260918 put the number
     * ring, the wire ends and the wall into the mask the wire stage subtracts its green
     * from. `mocks/rig-20260918/cam_2.mp4` went from 1 refusal in 15 held frames to 8,
     * two-sided -- 17, 18 and 19 wires, and 21 and 22 -- and no value of the one margin
     * buys both halves: #1437 swept it on one binary and the board is repaired from 1.60
     * up while the wire stage is intact only at 1.25.
     *
     * `regionFor` is the repair: the frame with everything outside the FITTED doubles
     * ellipse blacked out, at the frame's own size, so the colour stage's frame-relative
     * windows still mean what they meant. The margin is 1.05 because that is not a new
     * number -- it is `detectMetalWires`'s own buffered ring, said once here instead of
     * twice there, and it is the outermost thing this stage ever reads.
     *
     * `OD_WIRE_REGION=roi` hands the wire stage the board finder's region again, which is
     * the behaviour every commit before this one had, on the same binary. Anything else
     * -- including an empty value or a word this does not know -- is ignored rather than
     * obeyed, because a region nobody can name is not a region and a silent fallback is
     * what #1378 was invisible behind.
     */
    struct WireRegionParams
    {
        /**
         * How much wider than the FITTED doubles ellipse the wire stage's region is.
         *
         * 1.05 is `detectMetalWires`'s `bufferedRing`, which has been the outer edge of
         * everything this stage reads since long before this issue; the region is drawn
         * at the same place so that nothing the stage can read is outside the region the
         * stage was measured in. `OD_WIRE_REGION_MARGIN=<x>` moves it at run time on one
         * binary. A value of zero or less, or anything atof cannot read, is ignored
         * rather than obeyed -- a region of no radius is a black frame, and a wire stage
         * handed one returns nothing while naming a count rather than a region.
         */
        float regionOfDoublesEllipse = 1.0f;
    };

    /** True when OD_WIRE_REGION=roi put the board finder's region back. */
    bool readsInsideTheBoardFindersRegion();

    /**
     * The wire stage's region, drawn around the doubles ellipse fitted at STEP 6.
     *
     * The result is the frame with everything outside that ellipse blacked out, at the
     * frame's own size -- the same shape `roi_processing::processROI` returns, and for
     * the same reason: the colour stage's own windows are frame-relative.
     *
     * A camera whose doubles ring was never fitted has no region: the caller is handed
     * an empty Mat and must not go on, which is the state `processWires` already declines
     * to run in.
     */
    Mat regionFor(const Mat &frame, const DartboardCalibration &calib,
                  const WireRegionParams &params = WireRegionParams());

    /** The region as an ellipse, which is what this stage's own masks are drawn from. */
    RotatedRect regionOf(const DartboardCalibration &calib,
                         const WireRegionParams &params = WireRegionParams());

    WireData processWires(const Mat &frame, const Mat &colorMask, const DartboardCalibration &calib, bool enableDebug = false, const WireDetectionConfig &config = WireDetectionConfig());
}