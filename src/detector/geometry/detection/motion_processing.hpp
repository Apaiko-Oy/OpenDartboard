#pragma once

#include <opencv2/opencv.hpp>
#include <chrono>

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
        double spike_threshold = 0.08;     // Fraction of the board that changed: a dart hit
        double low_threshold = 0.001;      // Fraction of the board that changed: settled
        int min_cameras_for_event = 2;     // Minimum cameras that must participate in dart event
        int spike_window_frames = 10;      // Frames to wait for other cameras to join spike
        int stability_frames = 15;         // Consecutive low-motion frames needed for stability
        int max_event_duration_ms = 10000; // Maximum time for dart event (safety timeout)
        int cooldown_period_ms = 1000;     // Cooldown after dart detection
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

} // namespace motion_processing
