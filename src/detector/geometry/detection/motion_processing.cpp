#include "motion_processing.hpp"
#include "logging.hpp"
#include "utils.hpp"
#include "utils/streamer.hpp"
#include "utils/od_clock.hpp"
#include "utils/od_fix.hpp"
#include <cstdio>

using namespace cv;
using namespace std;

namespace motion_processing
{
    // Static storage for previous frames (maintains state between calls)
    static vector<Mat> previous_frames;
    static bool initialized = false;

    static unique_ptr<streamer> motion_streamer;
    static unique_ptr<streamer> motion2_streamer;

    // Event-based dart detection state
    static DartEventState current_state = DartEventState::IDLE;
    static long long event_start_time = 0;
    static long long cooldown_start_time = 0;
    static vector<bool> cameras_spiked;
    static vector<double> intensity_history;
    static int stable_frame_count = 0;

    // #811 trace. A ring-free append into memory; nothing is written until the dump,
    // so the per-cycle cost is a push_back and the wall clock is not disturbed.
    struct MotionTrace
    {
        long long cycle;
        long long now_ms;
        long long wall_ms;
        long long capture_ms;
        double intensity;
        double r0, r1, r2;
        int state_in;
        int state_out;
        long long event_ms;
        int spiked;
        int stable;
    };
    static vector<MotionTrace> motion_trace;

    vector<MotionData> detectMotion(const vector<Mat> &current_frames, const vector<Mat> &background_frames, bool debug_mode, const MotionParams &params)
    {
        // Initialize previous frames on first run
        if (!initialized || previous_frames.size() != current_frames.size())
        {
            previous_frames.clear();

            // check if we have any frames to initialize
            if (current_frames.size() != 3)
            {
                log_warning("Motion processing initialized with " + to_string(current_frames.size()) + " cameras, but expected 3 cameras. Skipping initialization.");
                return vector<MotionData>(current_frames.size());
            }

            for (const auto &frame : current_frames)
            {
                previous_frames.push_back(frame.clone()); // an unavailable slot stays empty and self-heals
            }

            initialized = true;
            log_debug("Motion processing initialized with " + to_string(current_frames.size()) + " cameras");

#ifdef DEBUG_VIA_VIDEO_INPUT
            // #812: see dart_processing.cpp — the debug build and the debug flag, both.
            if (debug_mode)
            {
                motion_streamer = make_unique<streamer>(8082, 15);
                motion2_streamer = make_unique<streamer>(8083, 15);
            }
#endif

            // Return no motion on first frame
            return vector<MotionData>(current_frames.size());
        }

        // Detect current motion using our processing - now with actual data
        vector<MotionData> motion_data(current_frames.size());
        vector<Mat> motion_viz_frames;
        vector<Mat> motion_viz_frames2;

        // Motion detection for each camera
        for (size_t i = 0; i < current_frames.size() && i < previous_frames.size(); i++)
        {
            if (current_frames[i].empty() || previous_frames[i].empty() || background_frames[i].empty())
                continue;

            // Convert to grayscale
            Mat prev_gray, curr_gray;
            cvtColor(previous_frames[i], prev_gray, COLOR_BGR2GRAY);
            cvtColor(current_frames[i], curr_gray, COLOR_BGR2GRAY);

            // clean up frames so there no noise
            GaussianBlur(prev_gray, prev_gray, Size(params.blur_kernel_size, params.blur_kernel_size), params.blur_sigma_x, params.blur_sigma_y, BORDER_DEFAULT);
            GaussianBlur(curr_gray, curr_gray, Size(params.blur_kernel_size, params.blur_kernel_size), params.blur_sigma_x, params.blur_sigma_y, BORDER_DEFAULT);

            // Calculate absolute difference
            Mat diff;
            absdiff(prev_gray, curr_gray, diff);

            // Apply threshold
            Mat thresh;
            threshold(diff, thresh, params.binary_threshold, 255, THRESH_BINARY);

            // Apply morphological operations to reduce noise
            Mat kernel = getStructuringElement(params.morph_type, Size(params.morph_kernel_size, params.morph_kernel_size));
            morphologyEx(thresh, thresh, MORPH_CLOSE, kernel);

            // Debug: Save motion detection images
            if (debug_mode)
            {
                odfs::ensureDirectory("debug_frames/motion_processing");
                imwrite("debug_frames/motion_processing/diff_cam_" + to_string(i) + ".jpg", diff);
                imwrite("debug_frames/motion_processing/thresh_cam_" + to_string(i) + ".jpg", thresh);

                // this just an example of how to visualize motion
                // this should do MORE;

                motion_viz_frames.push_back(diff);
                motion_viz_frames2.push_back(thresh);
            }

            // Count motion pixels and calculate ratio - CAPTURE THE REAL DATA!
            int motion_pixels = countNonZero(thresh);
            int total_pixels = thresh.rows * thresh.cols;
            double motion_ratio = (double)motion_pixels / total_pixels;

            // Store all the motion data
            motion_data[i].motion_pixels = motion_pixels;
            motion_data[i].total_pixels = total_pixels;
            motion_data[i].motion_ratio = motion_ratio;
            motion_data[i].motion_detected = (motion_ratio > params.threshold_ratio);
        }

        // Update previous frames for next iteration.
        // #798: a camera that did not answer keeps the last frame it really produced,
        // so the next comparison is against a real image rather than against nothing.
        for (size_t i = 0; i < current_frames.size(); i++)
        {
            if (!current_frames[i].empty())
                previous_frames[i] = current_frames[i].clone();
        }

        if (debug_mode && motion_streamer)
        {
            Mat combined_motion = debug::createCombinedFrame(motion_viz_frames, "diff");
            motion_streamer->push(combined_motion);
            Mat combined_motion2 = debug::createCombinedFrame(motion_viz_frames2, "thresh");
            motion2_streamer->push(combined_motion2);
        }

        return motion_data;
    }

    MotionResult processMotion(const vector<Mat> &current_frames, const vector<Mat> &background_frames, bool debug_mode, const MotionParams &params)
    {
        MotionResult result;

        // Get motion data from all cameras
        vector<MotionData> motion_data = detectMotion(current_frames, background_frames, debug_mode, params);
        long long now = od_clock::now_ms();
        DartEventState state_in = current_state;

        // Calculate overall motion intensity (average across all cameras)
        double total_intensity = 0.0;
        int cameras_with_motion = 0;
        int cameras_answering = 0;

        for (size_t i = 0; i < motion_data.size(); i++)
        {
            // #798: a camera that produced no frame did not say "no motion".
            if (i < current_frames.size() && current_frames[i].empty())
                continue;

            cameras_answering++;
            total_intensity += motion_data[i].motion_ratio;
            if (motion_data[i].motion_detected)
                cameras_with_motion++;
        }

        double current_intensity = cameras_answering > 0 ? total_intensity / cameras_answering : 0.0;

        // Initialize cameras_spiked vector if needed
        if (cameras_spiked.size() != motion_data.size())
        {
            cameras_spiked.resize(motion_data.size(), false);
        }

        // Calculate detection duration if we're in an active state
        if (current_state != DartEventState::IDLE && current_state != DartEventState::COOLDOWN)
        {
            result.detection_duration_ms = (int)(now - event_start_time);
        }

        // State machine for dart event detection
        switch (current_state)
        {
        case DartEventState::IDLE:
        {
            // Look for motion spike that could indicate dart hit
            if (current_intensity > params.spike_threshold)
            {
                current_state = DartEventState::SPIKE_DETECTED;
                event_start_time = now;
                fill(cameras_spiked.begin(), cameras_spiked.end(), false);
                intensity_history.clear();
                stable_frame_count = 0;

                // Mark cameras that are spiking
                for (size_t i = 0; i < motion_data.size(); i++)
                {
                    if (motion_data[i].motion_ratio > params.spike_threshold)
                    {
                        cameras_spiked[i] = true;
                    }
                }
            }
            break;
        }

        case DartEventState::SPIKE_DETECTED:
        {
            // Continue tracking which cameras spike during the event window
            for (size_t i = 0; i < motion_data.size(); i++)
            {
                if (motion_data[i].motion_ratio > params.spike_threshold)
                {
                    cameras_spiked[i] = true;
                }
            }

            int cameras_that_spiked = count(cameras_spiked.begin(), cameras_spiked.end(), true);
            long long event_duration = now - event_start_time;

            // #815 defect 2: shipped code is `spike_window_frames * 50`, an unstated 20 fps.
            // The fix converts the frame count with the frame period the program actually has.
            long long spike_window_ms = od_fix::spikewin()
                                            ? (long long)(params.spike_window_frames * od_clock::frame_period_ms())
                                            : (long long)(params.spike_window_frames * 50);

            // Check if we have enough camera participation and motion is settling
            if (cameras_that_spiked >= params.min_cameras_for_event &&
                current_intensity <= params.low_threshold)
            {
                current_state = DartEventState::STABILIZING;
                stable_frame_count = 1;
            }
            // Timeout if event takes too long or insufficient participation
            else if (event_duration > params.max_event_duration_ms ||
                     (event_duration > spike_window_ms && cameras_that_spiked < params.min_cameras_for_event))
            {
                bool safety_timeout = event_duration > params.max_event_duration_ms;
                current_state = DartEventState::IDLE;
                od_clock::timeouts(safety_timeout ? 0 : 1).fetch_add(1);
                if (od_fix::warnsplit())
                {
                    // #815 defect 3: two branches, two messages.
                    if (safety_timeout)
                    {
                        log_warning("DART EVENT: Abandoned - event exceeded max_event_duration_ms (" + to_string(event_duration) + "ms > " + to_string(params.max_event_duration_ms) + "ms) with " + to_string(cameras_that_spiked) + "/" + to_string(params.min_cameras_for_event) + " cameras spiked");
                    }
                    else
                    {
                        log_warning("DART EVENT: Abandoned - only " + to_string(cameras_that_spiked) + "/" + to_string(params.min_cameras_for_event) + " cameras joined the spike within the " + to_string(spike_window_ms) + "ms window (" + to_string(event_duration) + "ms elapsed)");
                    }
                }
                else
                {
                    log_warning("DART EVENT: Event timeout or insufficient cameras (" + to_string(cameras_that_spiked) + "/" + to_string(params.min_cameras_for_event) + ") after " + to_string(event_duration) + "ms");
                }
            }
            break;
        }

        case DartEventState::STABILIZING:
        {
            // #816: max_event_duration_ms is tested only in case SPIKE_DETECTED. With
            // #815 defect 1's break restored an event spends nearly all of its life
            // here, which is the one state the safety timeout cannot reach.
            if (od_fix::safety())
            {
                long long event_duration = now - event_start_time;
                if (event_duration > params.max_event_duration_ms)
                {
                    int cameras_that_spiked = count(cameras_spiked.begin(), cameras_spiked.end(), true);
                    current_state = DartEventState::IDLE;
                    od_clock::timeouts(0).fetch_add(1);
                    log_warning("DART EVENT: Abandoned in STABILIZING - event exceeded max_event_duration_ms (" + to_string(event_duration) + "ms > " + to_string(params.max_event_duration_ms) + "ms) with " + to_string(cameras_that_spiked) + "/" + to_string(params.min_cameras_for_event) + " cameras spiked, stable_frame_count " + to_string(stable_frame_count));
                    break;
                }
            }

            // Track motion intensity to confirm stability
            if (current_intensity <= params.low_threshold)
            {
                stable_frame_count++;
                if (stable_frame_count >= params.stability_frames)
                {
                    // Motion event finished!
                    current_state = DartEventState::END;
                    result.motion_finished = true;
                    int cameras_that_spiked = count(cameras_spiked.begin(), cameras_spiked.end(), true);
                }
                // #815 defect 1: shipped code has no break here, so control falls through
                // into case END and the event finishes on the first settled cycle whatever
                // stable_frame_count says. This is the missing break.
                else if (od_fix::stability())
                {
                    break;
                }
            }
            else
            {
                // Motion increased - could be dart removal or false positive
                if (current_intensity > params.spike_threshold)
                {
                    // Big spike during stabilization - probably dart removal, reset
                    current_state = DartEventState::IDLE;
                }
                else
                {
                    // Small increase, reset stability counter but keep trying
                    stable_frame_count = 0;
                }
                break;
            }
        }

        case DartEventState::END:
        {
            // Transition to cooldown immediately
            current_state = DartEventState::COOLDOWN;
            cooldown_start_time = now;
            result.motion_finished = true;

            break;
        }

        case DartEventState::COOLDOWN:
        {
            long long cooldown_elapsed = now - cooldown_start_time;
            if (cooldown_elapsed >= params.cooldown_period_ms)
            {
                current_state = DartEventState::IDLE;
            }
            else if (debug_mode && current_intensity > params.spike_threshold)
            {
                log_debug("COOLDOWN: Motion during cooldown period - intensity: " + to_string(current_intensity) + ", remaining: " + to_string(params.cooldown_period_ms - cooldown_elapsed) + "ms");
            }
            break;
        }
        }

        {
            MotionTrace t;
            t.cycle = od_clock::cycles().load();
            t.now_ms = now;
            t.wall_ms = od_clock::wall_ms();
            t.capture_ms = od_clock::capture_ms().load();
            t.intensity = current_intensity;
            t.r0 = motion_data.size() > 0 ? motion_data[0].motion_ratio : -1.0;
            t.r1 = motion_data.size() > 1 ? motion_data[1].motion_ratio : -1.0;
            t.r2 = motion_data.size() > 2 ? motion_data[2].motion_ratio : -1.0;
            t.state_in = (int)state_in;
            t.state_out = (int)current_state;
            t.event_ms = (state_in == DartEventState::IDLE) ? 0 : (now - event_start_time);
            t.spiked = (int)count(cameras_spiked.begin(), cameras_spiked.end(), true);
            t.stable = stable_frame_count;
            motion_trace.push_back(t);
        }

        result.current_state = current_state;
        return result;
    }



    void dumpTrace()
    {
        const char *path = std::getenv("OD_TRACE");
        if (!path)
            return;
        FILE *f = std::fopen(path, "w");
        if (!f)
            return;
        std::fprintf(f, "cycle,now_ms,wall_ms,capture_ms,intensity,r0,r1,r2,state_in,state_out,event_ms,spiked,stable\n");
        for (const auto &t : motion_trace)
            std::fprintf(f, "%lld,%lld,%lld,%lld,%.6f,%.6f,%.6f,%.6f,%d,%d,%lld,%d,%d\n",
                         t.cycle, t.now_ms, t.wall_ms, t.capture_ms, t.intensity,
                         t.r0, t.r1, t.r2, t.state_in, t.state_out, t.event_ms, t.spiked, t.stable);
        std::fclose(f);
    }

} // namespace motion_processing
