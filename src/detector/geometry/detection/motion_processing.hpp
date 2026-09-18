#pragma once

#include <opencv2/opencv.hpp>
#include <chrono>
#include <string>

using namespace cv;
using namespace std;

namespace motion_processing
{
    // Motion data for a single camera
    struct MotionData
    {
        bool motion_detected = false; // Boolean result (for backward compatibility)
        double motion_ratio = 0.0;    // Actual motion intensity (0.0 to 1.0)
        int motion_pixels = 0;        // Number of pixels that changed
        int total_pixels = 0;         // Total pixels in frame
    };

    // Motion detection parameters
    struct MotionParams
    {
        // detectMotion params
        int morph_type = MORPH_RECT;   // Type of morphological kernel (MORPH_RECT, MORPH_ELLIPSE)
        int morph_kernel_size = 4;     // Size of morphological kernel
        double threshold_ratio = 0.05; // 5% of pixels changed
        int binary_threshold = 10;     // Threshold for binary difference
        int blur_kernel_size = 5;      // Size of Gaussian blur kernel
        int blur_sigma_x = 10;         // Sigma for Gaussian blur in X direction
        int blur_sigma_y = 10;         // Sigma for Gaussian blur in Y direction

        // Event-based dart detection params
        double spike_threshold = 0.08;     // High motion intensity that indicates potential dart hit
        double low_threshold = 0.001;      // Low motion intensity for stability detection
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

    // Process motion detection with event-based dart landing detection
    MotionResult processMotion(
        const vector<Mat> &current_frames,
        const vector<Mat> &background_frames,
        bool debug_mode = false,
        const MotionParams &params = MotionParams());

    // #811: write the per-cycle trace to $OD_TRACE, if set.
    void dumpTrace();

    /**
     * #1338: why this board can never form a dart event, or an empty string if it can.
     *
     * A dart event is a motion spike seen by `min_cameras_for_event` cameras at once, and
     * this file is the only place that number is written. Nothing above it knew: a rig
     * whose cameras 2 and 3 delivered no frames calibrated on camera 1, called itself
     * ready, and sat in a state where `cameras_that_spiked` is bounded above by 1 and the
     * threshold is 2 -- not a board that rarely scores, a board that arithmetically
     * cannot. So the arithmetic is asked here, where its own constant lives, and the
     * detector reads the sentence rather than a count of objects.
     *
     * Two facts, both about this translation unit and both checkable against the code:
     *
     *   1. `cameras_answering` is how many cameras produced a frame TO CALIBRATE ON, and
     *      it is the ceiling on `cameras_that_spiked` for the life of the run rather than
     *      for this cycle. `detectMotion` skips any slot whose `background_frames[i]` is
     *      empty, and the backgrounds are the calibration frames, saved once. A camera
     *      that was silent at calibration therefore reports `motion_ratio` 0.0 for ever,
     *      even if it starts answering later, and 0.0 never exceeds `spike_threshold`.
     *
     *   2. `camera_slots` must be exactly 3. `detectMotion`'s initialisation refuses any
     *      other number, returns zeroed MotionData and never sets `initialized`, so a
     *      two-camera board reports no motion on any camera on any cycle.
     *
     * #1321's rule on the sentence: every count is stated against the threshold it fell
     * short of, so a line reporting the wrong number can be seen to be wrong.
     */
    inline std::string whyNoEventIsPossible(int camera_slots, int cameras_answering,
                                            const MotionParams &params = MotionParams())
    {
        if (camera_slots != 3)
        {
            return "this board is running " + std::to_string(camera_slots) +
                   " cameras and motion detection only initialises on 3, so no camera ever "
                   "reports motion and no dart can be scored";
        }
        if (cameras_answering < params.min_cameras_for_event)
        {
            return "only " + std::to_string(cameras_answering) + " of " + std::to_string(camera_slots) +
                   " cameras produced a frame to calibrate on, and a dart event needs a motion "
                   "spike seen by at least " + std::to_string(params.min_cameras_for_event) +
                   " cameras at once, so no dart can be scored until the missing " +
                   std::to_string(camera_slots - cameras_answering) + " answer";
        }
        return "";
    }

} // namespace motion_processing
