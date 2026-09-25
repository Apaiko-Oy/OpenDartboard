#include "motion_processing.hpp"
#include "logging.hpp"
#include "utils.hpp"
#include "utils/streamer.hpp"
#include "utils/od_clock.hpp"
#include "utils/od_fix.hpp"
#include <cstdio>
#include <cstdlib>
#include <string>

using namespace cv;
using namespace std;

namespace motion_processing
{
    // Static storage for previous frames (maintains state between calls)
    static vector<Mat> previous_frames;
    static bool initialized = false;

    static unique_ptr<streamer> motion_streamer;
    static unique_ptr<streamer> motion2_streamer;

    // #1339: the region every ratio in this file is a fraction of, one per camera. The
    // mask is built from the board calibration already fitted and rebuilt only when the
    // board or the frame size changes, so the per-cycle cost is a bitwise_and.
    struct Region
    {
        bool known = false;
        Mat mask;
        int pixels = 0;
        Size frame;
        RotatedRect edge;
    };
    static vector<Region> regions;

    // #1339's falsification, in the shape od_fix established: one binary, the
    // denominator chosen at run time, so "different build" is never a confound.
    // OD_MOTION_DENOMINATOR=frame restores what this file did before #1339 -- the whole
    // frame, numerator and denominator both -- and is how the rig's darts can be made to
    // disappear again on the same binary that finds them.
    static bool measuredAgainstTheFrame()
    {
        static bool v = []
        {
            const char *e = std::getenv("OD_MOTION_DENOMINATOR");
            return e && std::string(e) == "frame";
        }();
        return v;
    }

    // #1358's falsification, in the shape #1339's `OD_MOTION_DENOMINATOR` established:
    // one binary, the TRIGGER chosen at run time, so "different build" is never a
    // confound. OD_DART_WINDOW=settle restores the dart window's trigger exactly as it
    // was when #1358 was filed and #1345 measured it -- a spike big enough that only a
    // board-wide disturbance reaches it, seen by two cameras at once, and a cooldown
    // that swallows whatever lands inside it. On mocks/rig-20260918 that is the trigger
    // under which every window this program opened had ZERO changed pixels inside every
    // camera's own fitted board: the six windows were the six RETRIEVALS, and the board
    // they measured had just been emptied by hand.
    static bool settleTrigger()
    {
        static bool v = []
        {
            const char *e = std::getenv("OD_DART_WINDOW");
            return e && std::string(e) == "settle";
        }();
        return v;
    }

    // The thresholds this cycle really reads. #1353 moved both constants and #1358's
    // switch is the only thing that moves them back.
    static double spikeThreshold(const MotionParams &params)
    {
        return settleTrigger() ? 0.08 : params.spike_threshold;
    }
    static int minCamerasForEvent(const MotionParams &params)
    {
        return settleTrigger() ? 2 : params.min_cameras_for_event;
    }

    // #1627's pin, in the shape of the two above: OD_SETTLE_SPIKE=discard restores what
    // case STABILIZING did before #1627 with a spike while an event settles -- drop the
    // event and go back to IDLE -- on the same binary. See that case for why it no longer
    // does.
    static bool settleSpikeDiscards()
    {
        static bool v = []
        {
            const char *e = std::getenv("OD_SETTLE_SPIKE");
            return e && std::string(e) == "discard";
        }();
        return v;
    }

    // The mask for one camera, built on first sight of its board and kept.
    static const Region &regionFor(size_t i, const vector<BoardExtent> &boards, Size frame, const MotionParams &params)
    {
        if (regions.size() <= i)
            regions.resize(i + 1);
        Region &r = regions[i];
        const BoardExtent extent = i < boards.size() ? boards[i] : BoardExtent();

        const bool same = r.known == extent.known && r.frame == frame &&
                          r.edge.center == extent.edge.center && r.edge.size == extent.edge.size &&
                          r.edge.angle == extent.edge.angle;
        if (same && (!r.known || !r.mask.empty()))
            return r;

        r = Region();
        r.frame = frame;
        r.edge = extent.edge;
        r.known = extent.known;
        if (!r.known)
        {
            log_warning("MOTION REGION: camera " + to_string(i + 1) + " has no fitted board, so it "
                        "abstains from the motion figure rather than being measured against the frame");
            return r;
        }
        r.mask = Mat::zeros(frame, CV_8UC1);
        ellipse(r.mask, r.edge, Scalar(255), FILLED);
        r.pixels = countNonZero(r.mask);
        if (r.pixels <= 0)
        {
            r.known = false;
            log_warning("MOTION REGION: camera " + to_string(i + 1) + " fitted a board of no area, so it abstains");
            return r;
        }
        const int frame_pixels = frame.width * frame.height;
        log_info("MOTION REGION: camera " + to_string(i + 1) + " measures motion inside its board, the " +
                 to_string((int)lround(r.edge.size.width)) + "x" + to_string((int)lround(r.edge.size.height)) +
                 " px ellipse at (" + to_string((int)lround(r.edge.center.x)) + "," +
                 to_string((int)lround(r.edge.center.y)) + ") turned " + to_string((int)lround(r.edge.angle)) +
                 " degrees: " +
                 to_string(r.pixels) + " px of a " + to_string(frame_pixels) + " px frame (" +
                 to_string((int)lround(100.0 * r.pixels / frame_pixels)) + "%); a ratio of " +
                 to_string(params.spike_threshold) + " is " +
                 to_string((int)lround(params.spike_threshold * r.pixels)) + " changed pixels");
        return r;
    }

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

    vector<MotionData> detectMotion(const vector<Mat> &current_frames, const vector<Mat> &background_frames, const vector<BoardExtent> &boards, bool debug_mode, const MotionParams &params)
    {
        // Initialize previous frames on first run
        if (!initialized || previous_frames.size() != current_frames.size())
        {
            previous_frames.clear();
            regions.clear(); // #1339: a fresh board is measured against a fresh region

            // There is no three-camera invariant in the state kept below: every
            // per-camera structure is sized from the frames that arrived.  A two-camera
            // board which met camera_quorum is therefore a usable board, not a zeroed
            // motion stream.  Only an empty input has nothing to initialise from.
            if (current_frames.empty())
            {
                log_warning("Motion processing received no camera frames. Skipping initialization.");
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

            // #1339: count what changed inside the board, over the board's own area.
            // Both halves are the region: a ratio whose numerator is the whole frame
            // and whose denominator is the board makes `low_threshold` unreachable on a
            // small board -- the rig's quiet cycles measure 0.0019 that way, above the
            // 0.001 an event has to settle under, so nothing ever settles.
            int motion_pixels = 0;
            int region_pixels = 0;
            if (measuredAgainstTheFrame())
            {
                motion_pixels = countNonZero(thresh);
                region_pixels = thresh.rows * thresh.cols;
            }
            else
            {
                const Region &region = regionFor(i, boards, thresh.size(), params);
                if (!region.known)
                    continue; // this camera has no scale, so it says nothing
                Mat inside;
                bitwise_and(thresh, region.mask, inside);
                motion_pixels = countNonZero(inside);
                region_pixels = region.pixels;
            }
            double motion_ratio = region_pixels > 0 ? (double)motion_pixels / region_pixels : 0.0;

            // Store all the motion data
            motion_data[i].motion_pixels = motion_pixels;
            motion_data[i].region_pixels = region_pixels;
            motion_data[i].motion_ratio = motion_ratio;
            motion_data[i].measured = true;
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

    MotionResult processMotion(const vector<Mat> &current_frames, const vector<Mat> &background_frames, const vector<BoardExtent> &boards, bool debug_mode, const MotionParams &params)
    {
        MotionResult result;

        // Get motion data from all cameras
        vector<MotionData> motion_data = detectMotion(current_frames, background_frames, boards, debug_mode, params);
        long long now = od_clock::now_ms();
        DartEventState state_in = current_state;

        // Calculate overall motion intensity (average across all cameras)
        double total_intensity = 0.0;
        int cameras_with_motion = 0;
        int cameras_answering = 0;

        for (size_t i = 0; i < motion_data.size(); i++)
        {
            // #798: a camera that produced no frame did not say "no motion".
            // #1339: nor did one with no board to measure its motion against. Both are
            // the same abstention and `measured` is now the one thing that says so.
            if (!motion_data[i].measured)
                continue;

            cameras_answering++;
            total_intensity += motion_data[i].motion_ratio;
            if (motion_data[i].motion_detected)
                cameras_with_motion++;
        }

        double current_intensity = cameras_answering > 0 ? total_intensity / cameras_answering : 0.0;

        // #1353: the entry figure. A dart splash is strong on one camera and near-noise
        // on the others (rig census: 0.014-0.089 against 0.0002-0.008 for one throw), so
        // an AVERAGE divides the one real signal by the camera count and a threshold
        // that would catch it sinks into the noise. The peak is the entry test; the
        // average keeps the settle test, where the quiet floor really is shared.
        double peak_intensity = 0.0;
        for (size_t i = 0; i < motion_data.size(); i++)
        {
            if (motion_data[i].measured && motion_data[i].motion_ratio > peak_intensity)
            {
                peak_intensity = motion_data[i].motion_ratio;
            }
        }

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
            // Look for motion spike that could indicate dart hit. #1353: on any ONE
            // measured camera's board, not on the average -- see peak_intensity above.
            if (peak_intensity > spikeThreshold(params))
            {
                current_state = DartEventState::SPIKE_DETECTED;
                event_start_time = now;
                fill(cameras_spiked.begin(), cameras_spiked.end(), false);
                intensity_history.clear();
                stable_frame_count = 0;

                // Mark cameras that are spiking
                for (size_t i = 0; i < motion_data.size(); i++)
                {
                    if (motion_data[i].motion_ratio > spikeThreshold(params))
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
                if (motion_data[i].motion_ratio > spikeThreshold(params))
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
            if (cameras_that_spiked >= minCamerasForEvent(params) &&
                current_intensity <= params.low_threshold)
            {
                current_state = DartEventState::STABILIZING;
                stable_frame_count = 1;
            }
            // Timeout if event takes too long or insufficient participation
            else if (event_duration > params.max_event_duration_ms ||
                     (event_duration > spike_window_ms && cameras_that_spiked < minCamerasForEvent(params)))
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
                if (current_intensity > spikeThreshold(params))
                {
                    // #1627: a spike while the event settles is more of the same motion,
                    // so the event goes on: it re-opens here, on this cycle, exactly as
                    // IDLE would re-open it on the next cycle if the motion were still
                    // above the spike threshold there. Before #1627 this line went to
                    // IDLE ("probably dart removal"), and that loses the whole event
                    // whenever the spike is a one- or two-cycle blip: nothing re-opens
                    // it, no window is made, and the motion it was about -- a takeout,
                    // on mocks/rig-20260922 visit 7 -- never reaches the dart vote. The
                    // next arrival's window then reads the fall from three darts to one
                    // as a takeout (#1518's CLEAN BY REVERSION) and re-bases the clean
                    // reference with the new dart standing in it. Measured there under
                    // OD_LOOK_BUDGET=1605 OD_SEEK_ALIGN=1618 (#1618's trace): camera 3
                    // read 0.0107 and 0.0426 of its board on the two cycles after the
                    // takeout settled, the average 0.0142 crossed 0.011, and v8.1's T1
                    // was baked into the empty board.
                    // When the motion persists this is IDLE's own re-entry one cycle
                    // early; only the blip case changes what happens.
                    // OD_SETTLE_SPIKE=discard restores the old line. So do #1358's and
                    // #1339's falsification switches, for the reason case COOLDOWN gives:
                    // a falsification run keeps the whole machine it was written against.
                    std::string ratios;
                    for (size_t i = 0; i < motion_data.size(); i++)
                    {
                        ratios += string(i ? " " : "") + "cam" + to_string(i + 1) + "=" +
                                  (motion_data[i].measured ? to_string(motion_data[i].motion_ratio) : string("-"));
                    }
                    const bool discard = settleSpikeDiscards() || settleTrigger() || measuredAgainstTheFrame();
                    log_info(string("I1627 SETTLE SPIKE cycle=") + to_string(od_clock::cycles().load()) +
                             " stable=" + to_string(stable_frame_count) +
                             " average=" + to_string(current_intensity) +
                             " threshold=" + to_string(spikeThreshold(params)) + " " + ratios +
                             (discard ? " -> IDLE: the event is discarded (the pre-#1627 line, pinned)"
                                      : " -> SPIKE_DETECTED: the event goes on"));
                    if (discard)
                    {
                        current_state = DartEventState::IDLE;
                    }
                    else
                    {
                        current_state = DartEventState::SPIKE_DETECTED;
                        event_start_time = now;
                        fill(cameras_spiked.begin(), cameras_spiked.end(), false);
                        intensity_history.clear();
                        stable_frame_count = 0;
                        for (size_t i = 0; i < motion_data.size(); i++)
                        {
                            if (motion_data[i].motion_ratio > spikeThreshold(params))
                            {
                                cameras_spiked[i] = true;
                            }
                        }
                    }
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
            // #1358: a spike inside the cooldown is a NEW dart, and this is where the
            // rig's missing darts went. An event only ever reaches END by settling --
            // `current_intensity <= low_threshold`, three orders of magnitude under a
            // spike -- so the motion this cooldown exists to ignore is already over
            // before the clock starts. What the clock then ignores is the next throw.
            // Measured on mocks/rig-20260918, 1800 cycles: four splashes of 0.018 to
            // 0.039 of one camera's own board -- throws, by #1353's census, which puts
            // a throw at 0.0135-0.089 and the noise ceiling at 0.0088 -- landed inside
            // a cooldown and opened no window at all. The board scored 17 darts where
            // 21 were thrown, and the four it lost are those four.
            //
            // So a fresh spike ends the cooldown and starts its own event, exactly as
            // IDLE would have. The cooldown still stands against a dart whose own
            // splash rings on for a few cycles, because that never gets here: the state
            // machine cannot leave STABILIZING until the room is quiet.
            //
            // Gated off under #1339's `OD_MOTION_DENOMINATOR=frame` for the same reason
            // it is gated off under #1358's own switch: the argument above is that a
            // spike is a spike ON A BOARD, and under the frame denominator there is no
            // board in the figure -- the thrower fills it, which is what #1339 measured.
            // A falsification switch must vary one thing, so that run keeps the whole of
            // the machine it was written to falsify.
            else if (peak_intensity > spikeThreshold(params) && !settleTrigger() && !measuredAgainstTheFrame())
            {
                current_state = DartEventState::SPIKE_DETECTED;
                event_start_time = now;
                fill(cameras_spiked.begin(), cameras_spiked.end(), false);
                intensity_history.clear();
                stable_frame_count = 0;
                for (size_t i = 0; i < motion_data.size(); i++)
                {
                    if (motion_data[i].motion_ratio > spikeThreshold(params))
                    {
                        cameras_spiked[i] = true;
                    }
                }
                log_debug("COOLDOWN: a spike of " + to_string(peak_intensity) + " on one camera's own board, " +
                          to_string(params.cooldown_period_ms - cooldown_elapsed) + "ms into the cooldown: a new dart event");
            }
            else if (debug_mode && current_intensity > spikeThreshold(params))
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
