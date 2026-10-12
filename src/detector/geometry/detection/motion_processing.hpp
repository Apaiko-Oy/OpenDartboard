#pragma once

#include <opencv2/opencv.hpp>
#include <chrono>
#include <string>

#include "detector/geometry/camera_quorum.hpp"

using namespace cv;
using namespace std;

namespace motion_processing
{
    /**
     * #1339: the region a dart can be in, for one camera.
     *
     * Every threshold in MotionParams is a fraction of an area, and until #1339 that
     * area was the whole 1280x720 frame while everything a dart does happens inside the
     * board. That makes a threshold a property of the lens and the mounting: hold the
     * footage still and move only how much of the frame the board fills, and the figure
     * moves with the square of it. Measured on one clip re-encoded at 0.6x by
     * `testers/i1339_scaled_footage.cpp` -- the same darts, the same arms, the board at
     * 0.351 of the pixels it had:
     *
     *   over the frame   top-5 intensity 0.4011 -> 0.1426   x0.356, the area
     *   over the board   top-5 intensity 0.4844 -> 0.4932   x1.018
     *
     * So the region is what the ratio is taken over now, numerator and denominator
     * both, and calibration has already fitted it: the outer edge of the double ring.
     *
     * Two things a reader coming from the issue should know, because both were measured
     * here and neither is what it says. The board is NOT smaller in frame on
     * `mocks/rig-20260918/` than on the shipped mocks -- the fitted ellipses are 197117,
     * 200385 and 194335 px against 183859, 173006 and 175444, so the second rig's board
     * is the LARGER of the two. The 247.8 px against 151.0 px in #1339's table is
     * `bull_processing`'s red/green disc, which measures the coloured segments and finds
     * far fewer of them on that rig's duller board; it is not the board's extent. And
     * what the rig gained is therefore mostly the NUMERATOR: motion in the room outside
     * the board stops counting as motion on the board, which is why its quiet cycles
     * fall from a 90th percentile of 0.001937 -- above the threshold an event has to
     * settle under, so events could not finish -- to 0.000410.
     *
     * `known` false is not "use the frame instead". A camera whose board was never
     * fitted has no scale to be measured against, so it abstains from the figure the
     * way #798's camera with no frame abstains -- it did not say "no motion".
     */
    struct BoardExtent
    {
        bool known = false;   // Did calibration fit this camera's board?
        cv::RotatedRect edge; // The outer double ring, in this camera's pixels
    };

    // Motion data for a single camera
    struct MotionData
    {
        bool motion_detected = false; // Boolean result (for backward compatibility)
        double motion_ratio = 0.0;    // Actual motion intensity (0.0 to 1.0)
        int motion_pixels = 0;        // Pixels that changed inside the region
        int region_pixels = 0;        // #1339: the area that ratio is a fraction of
        bool measured = false;        // #1339: this camera produced a figure at all
        double board_level = -1.0;    // #1646: mean grey level inside the region, this cycle
        // turnaus#1783's census (OD_MOTION_REGION_CENSUS=1): the same diff counted the OTHER
        // way -- to the rim when the figure above is the double's, and the reverse -- as a
        // share of the same denominator. -1 when the census is off.
        double other_ratio = -1.0;
    };

    /**
     * Motion detection parameters.
     *
     * #1339: every ratio below -- `threshold_ratio`, `spike_threshold`,
     * `low_threshold` -- is **a fraction of the board's own area in that camera's
     * frame**, counting only pixels that changed inside the board. Not of the frame.
     * That is what makes one number mean the same thing on two rigs, and BoardExtent
     * above holds the measurement that says so.
     */
    struct MotionParams
    {
        // detectMotion params
        int morph_type = MORPH_RECT;   // Type of morphological kernel (MORPH_RECT, MORPH_ELLIPSE)
        int morph_kernel_size = 4;     // Size of morphological kernel
        double threshold_ratio = 0.05; // 5% of the board's pixels changed
        int binary_threshold = 10;     // Threshold for binary difference
        int blur_kernel_size = 5;      // Size of Gaussian blur kernel
        int blur_sigma_x = 10;         // Sigma for Gaussian blur in X direction
        int blur_sigma_y = 10;         // Sigma for Gaussian blur in Y direction

        // Event-based dart detection params.
        //
        // #1339 kept both of them and measured both on both rigs, over the board. The
        // state machine below was replayed over the recorded per-camera ratios of a
        // full run of each:
        //
        //   spike_threshold   0.04 0.05 0.06 0.08 0.10  -> mocks 40 events, rig 6, flat
        //                     0.12 -> rig 4     0.15 -> rig 2     0.20 -> rig 0
        //   low_threshold     0.0005 0.001 0.002 0.005  -> mocks 40, rig 6, flat
        //
        // So 0.08 is kept because it sits in the middle of a plateau on which nothing
        // either rig answers changes -- which is the property a constant fitted to one
        // rig does not have, and #1322's question about every other constant here.
        // The dart events themselves now run 0.332 to 0.574 on the mocks and 0.033 to
        // 0.196 on the rig, where over the frame the rig's ran 0.042 to 0.101 and
        // straddled the threshold. Quiet cycles sit at a 90th percentile of 0.000112 on
        // the mocks and 0.000410 on the rig, both an order of magnitude under
        // low_threshold; over the frame the rig's quiet p90 was 0.001937, ABOVE it, so
        // an event on that rig could not settle and finish at all.
        // #1353 moved this constant and the two lines below say against what. The trace
        // census on mocks/rig-20260918 (25 excursions, debian-12/OpenCV 4.6, cameras 1
        // and 3 measured): a THROW is a 1-4 cycle splash whose STRONGEST camera peaks at
        // 0.0135-0.089 of its board, the retrievals run 0.13-0.41, the noise micro-blips
        // top out at 0.0088 and the quiet p90 is 0.0004 (#1339's census; the shipped
        // mocks' throws run 0.33-0.57 with quiet p90 0.0001). At 0.08 no throw has ever
        // entered an event on the rig -- the six events #1345 measured were the six
        // retrievals. 0.011 is 22% under the weakest splash, 25% over the biggest
        // micro-blip and 27x the quiet p90; the plateau sweep is in issue #1353.
        double spike_threshold = 0.011;    // Fraction of ONE camera's board that changed: a dart hit (per-camera peak, not the average -- see processMotion)
        double low_threshold = 0.001;      // Fraction of the board that changed: settled
        // #1353: 1, was 2, and the same census is why. A splash is a ONE-camera motion
        // fact: the weak-side camera of the same throw measures 0.0002-0.008, under any
        // threshold that clears noise, and which camera is the strong side varies per
        // throw. What every camera shares is the dart's PERSISTENT change, and that is
        // the dart-state vote's own quorum (dart_processing, two cameras) -- so the
        // event quorum stops pretending to be it. A spurious event is cheap since
        // #1350: an empty window is one refused STATE VOTE line. This also closes half
        // of #1348: with 1 here, whyNoEventIsPossible's answering-count gate and the
        // spiking ceiling can no longer disagree by one abstaining camera.
        //
        // #1389 NARROWED WHAT THIS NUMBER IS, AND LEFT ITS VALUE ALONE. It used to be
        // asked two ways: how many cameras must SPIKE TOGETHER inside one window (the
        // gate in processMotion, which is what the census above measured), and how many
        // cameras a board must HAVE before an event is possible at all
        // (whyNoEventIsPossible). Those are a trigger and a census, and ADR-0081 §2 is
        // about exactly that -- a count taken against one population compared against
        // another. The census half now reads `camera_quorum::cameras()` with the other
        // two quorums; this field is the trigger and nothing else, and 1 is the number
        // the rig census above measured. Raising it to two would stop a healthy
        // three-camera board scoring at all.
        // camera-quorum-exempt: this is the per-window SPIKE TRIGGER and not the camera
        // quorum -- how many cameras must spike together inside one window, which #1353
        // measured on the rig at one. ADR-0081 §2 lists it among the three quorums; the
        // half of it that really counted cameras is whyNoEventIsPossible's census, and
        // that half now reads camera_quorum::cameras(). Moving this number to two would
        // stop a HEALTHY three-camera board forming any event at all, which is a worse
        // failure than the one ADR-0081 is about. If this marker is ever removed, the
        // number beside it has to become camera_quorum::cameras() and the rig census in
        // #1353 has to be re-measured first.
        int min_cameras_for_event = 1;     // Cameras whose own board must spike TOGETHER in one window
        int spike_window_frames = 10;      // Frames to wait for other cameras to join spike
        int stability_frames = 15;         // Consecutive low-motion frames needed for stability
        // #1685: the same settle in milliseconds of the motion clock, and what is counted
        // unless OD_WINDOW_UNIT=cycles: 15 cycles x 33.3 ms, the rig's 30 fps. Half a
        // second of quiet board is what the settle has always asked for on the rig; as a
        // count it asked for 3 s on a board whose cycle takes 200 ms (od_clock::window_reached).
        int stability_ms = 500;
        int max_event_duration_ms = 10000; // Maximum time for dart event (safety timeout)
        // #1358: this is a floor on how long a QUIET board waits, not a deaf period. A
        // spike above `spike_threshold` inside it starts its own event immediately --
        // an event reaches its end only by settling under `low_threshold`, so the
        // motion this clock exists to ignore is over before the clock starts, and what
        // it used to ignore was the next throw. See processMotion, case COOLDOWN.
        int cooldown_period_ms = 1000;     // Cooldown after dart detection

        // #1646: an event does not finish while a camera's EXPOSURE is still moving.
        // The motion figure is a frame-to-frame difference, so a camera whose automatic
        // exposure is walking back after the thrower left its view reads as quiet --
        // under a grey level a frame -- while its whole board sits 5-15 grey levels off
        // the picture the dart vote compares against. Measured on mocks/rig-20260922
        // camera 3 after visit 1's takeout: motion quiet from f242, the frame's centre
        // at 122 grey at f242, 117 at f248, 114 at f260 and back to 107 only at f272.
        // The window opened at f243 saw 122223 of camera 3's 182307 board pixels
        // changed, could not call the board CLEAN, and published the takeout as a dart;
        // the next arrival's window then saw that figure "fall" and baked the arriving
        // dart into the clean reference. By default since #1662 (OD_SETTLE_EXPOSURE=off pins the old settle) the settle asks that every measured
        // camera's board level has spanned less than `exposure_span` grey levels over the
        // last `exposure_frames` cycles, for at most `exposure_hold_cycles` of waiting.
        int exposure_frames = 10;       // Cycles the board level is compared across
        double exposure_span = 1.0;     // Grey levels: the largest max-min that still counts as still
        int exposure_hold_cycles = 90;  // Longest an otherwise settled event waits (3 s at 30 fps)
        // #1685: the two above in milliseconds of the motion clock, counted unless
        // OD_WINDOW_UNIT=cycles. 10 cycles x 33.3 ms: the stretch #1646 measured an
        // exposure walk against (a third of a second of the level holding still). The
        // history always keeps at least two levels, because a span needs two, however long
        // one cycle is. 90 cycles x 33.3 ms: the 3 s the hold was written as.
        int exposure_ms = 333;
        int exposure_hold_ms = 3000;
    };

    // Event states for dart detection
    enum class DartEventState
    {
        IDLE,           // No motion detected
        SPIKE_DETECTED, // High motion detected, waiting for pattern confirmation
        STABILIZING,    // Waiting for motion to settle down
        END,            // Motion event finished,
        COOLDOWN        // Preventing immediate re-detection
    };

    // Session result information
    struct MotionResult
    {
        bool motion_finished = false;                        // Did a motion event just finish
        DartEventState current_state = DartEventState::IDLE; // Current detection state
        int detection_duration_ms = 0;                       // How long current detection has been running
    };

    // Process motion detection with event-based dart landing detection.
    // #1339: `boards` carries each camera's board, in that camera's slot, and is what
    // every ratio here is a fraction of.
    MotionResult processMotion(
        const vector<Mat> &current_frames,
        const vector<Mat> &background_frames,
        const vector<BoardExtent> &boards,
        bool debug_mode = false,
        const MotionParams &params = MotionParams());

    // #811: write the per-cycle trace to $OD_TRACE, if set.
    void dumpTrace();

    /**
     * #1338: why this board can never form a dart event, or an empty string if it can.
     *
     * A dart event is a motion spike seen by `min_cameras_for_event` cameras at once, and
     * a board that cannot reach the camera floor can never form one usefully. Nothing
     * above it knew: a rig whose cameras 2 and 3 delivered no frames calibrated on camera
     * 1, called itself ready, and sat in a state where `cameras_that_spiked` is bounded
     * above by 1 and the threshold was 2 -- not a board that rarely scores, a board that
     * arithmetically cannot. So the arithmetic is asked here, against this stage's own
     * population, and the detector reads the sentence rather than a count of objects.
     *
     * #1389: the THRESHOLD is no longer this file's. It is `camera_quorum::cameras()`,
     * read by calibration admission and by the state vote's floor as well, because the
     * three used to be three different numbers and a one-camera board fell through all of
     * them (ADR-0081 §1). What stayed here is the per-window spike trigger, which is a
     * different question about a different population -- see MotionParams above.
     *
     * Two facts, both about this translation unit and both checkable against the code:
     *
     *   1. `cameras_that_can_spike` is the ceiling on `cameras_that_spiked` for the life
     *      of the run rather than for this cycle, and #1348 is what narrowed it. A camera
     *      needs BOTH halves to ever spike. It needs a frame to calibrate on, because
     *      `detectMotion` skips any slot whose `background_frames[i]` is empty and the
     *      backgrounds are the calibration frames, saved once -- a camera silent at
     *      calibration reports `motion_ratio` 0.0 for ever, even if it starts answering
     *      later, and 0.0 never exceeds `spike_threshold`. And since #1339 it needs a
     *      FITTED BOARD, because every ratio here is a fraction of the board's own area
     *      and a camera with `BoardExtent::known` false abstains from the figure rather
     *      than being measured against a frame -- so it never spikes either.
     *
     *      Until #1348 this argument was asked with the ANSWERING count, which is the
     *      wider of the two populations: a board with three answering cameras of which
     *      one fitted a board was told an event was possible on three, by a function
     *      whose own file says only one of them can ever produce one. The two happen to
     *      agree today -- `min_cameras_for_event` is 1 since #1353 and calibration takes
     *      at least one seeing camera -- and that is exactly the kind of agreement that
     *      stops holding the next time somebody moves a constant.
     *
     *   2. `camera_slots` is the number of camera slots this board actually opened.
     *      Motion keeps one frame, one region and one result per slot, so it supports a
     *      two-camera board when it satisfies the same quorum as the state vote.
     *
     * #1321's rule on the sentence: every count is stated against the threshold it fell
     * short of, so a line reporting the wrong number can be seen to be wrong.
     */
    inline std::string whyNoEventIsPossible(int camera_slots, int cameras_that_can_spike,
                                            int quorum = camera_quorum::cameras())
    {
        if (cameras_that_can_spike < quorum)
        {
            return "only " + std::to_string(cameras_that_can_spike) + " of " + std::to_string(camera_slots) +
                   " cameras can report motion -- a camera needs a frame to calibrate on and a "
                   "fitted board to measure it against -- and this board needs " +
                   std::to_string(quorum) +
                   " of them before it may score, so no dart can be scored until the missing " +
                   std::to_string(camera_slots - cameras_that_can_spike) + " answer";
        }
        return "";
    }

    /**
     * turnaus#1783: where a camera's motion figure is COUNTED, and what it is a share of.
     *
     * Live on 2b56b48 (2026-10-10 16:51) a visit of three misses on the surround published
     * three MISSes (#1689's rim count carried each one), and the takeout that pulled them
     * opened no event: no window, no vote, nothing in the log for 16 s, until a second
     * takeout's window reverted all three cameras. The motion figure is counted inside the
     * double's outer ellipse (BoardExtent::edge), the dart counts since #1689 out to the
     * physical rim. A hand gripping three darts between the double and the rim changes the
     * frame where the dart counts look and not where the motion figure does, and the same
     * shape is #1537's: a dart that lands there opens nothing either.
     *
     * `OD_MOTION_REGION=rim` counts the motion figure out to the rim -- the ellipse
     * dart_processing's Region::tip_mask is, the double's scaled by the board's own
     * 225.5/170 -- and keeps the DENOMINATOR the double's area, so every ratio in
     * MotionParams keeps its units (#1689's choice for the dart counts, for the same
     * reason). Off (the default): counted inside the double, as before.
     *
     * Pure and inline (#1338's reason): testers/i1783_region_check.cpp holds the masks
     * and the share without building the detector.
     */
    inline double rimOverDouble()
    {
        return 225.5 / 170.0;
    }

    struct MotionMasks
    {
        cv::Mat count;       // where a changed pixel is counted
        int denominator = 0; // what the count is a share of: the double's area, always
    };

    inline MotionMasks motionMasks(cv::Size frame, const cv::RotatedRect &double_edge, bool to_rim)
    {
        MotionMasks m;
        cv::Mat board = cv::Mat::zeros(frame, CV_8UC1);
        cv::ellipse(board, double_edge, cv::Scalar(255), cv::FILLED);
        m.denominator = cv::countNonZero(board);
        if (!to_rim)
        {
            m.count = board;
            return m;
        }
        cv::RotatedRect rim = double_edge;
        rim.size.width *= (float)rimOverDouble();
        rim.size.height *= (float)rimOverDouble();
        m.count = cv::Mat::zeros(frame, CV_8UC1);
        cv::ellipse(m.count, rim, cv::Scalar(255), cv::FILLED);
        return m;
    }

    // The share of the board a thresholded diff changed, counted where `masks` says.
    inline double boardShare(const cv::Mat &thresh, const MotionMasks &masks)
    {
        if (masks.denominator <= 0 || thresh.size() != masks.count.size())
            return 0.0;
        cv::Mat inside;
        cv::bitwise_and(thresh, masks.count, inside);
        return (double)cv::countNonZero(inside) / (double)masks.denominator;
    }

} // namespace motion_processing
