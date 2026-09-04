#include "scorer.hpp"
#include "logging.hpp"
#include "utils.hpp"
#include "utils/debug.hpp"
#include "detector/detector_factory.hpp"
#include "communication/websocket_service.hpp"
#include "communication/score_queue.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <cstdlib>
#include "utils/od_clock.hpp"
#include "utils/od_fix.hpp"
#include "detector/geometry/detection/motion_processing.hpp"
#include <random>
#include <opencv2/opencv.hpp>
#include <algorithm>
#include <cstdlib>

using namespace std;
using namespace cv;

Scorer::Scorer(const string &model, int w, int h, int fps, const vector<string> &cams, bool debug_mode, const string &detector_type, bool setup_mode)
    : model_path(model), width(w), height(h), fps(fps), camera_sources(cams), debug_display(debug_mode), detector_type_name(detector_type), setup_mode(setup_mode)
{
    // Initialize score queue and WebSocket service
    score_queue_ = std::make_shared<ScoreQueue>();
    // #824: --setup publishes nothing. The decision, and the reason, is in main.cpp;
    // what it means here is that the service is never constructed, so its listener on
    // 13520 — the one this program opens on 0.0.0.0 in every build — is not opened
    // either. A setup run holds exactly one listener and it is on loopback.
    if (!setup_mode)
    {
        websocket_service_ = std::make_unique<WebSocketService>(score_queue_, 13520);
    }
    else
    {
        log_info("SETUP MODE: scores are not published; the WebSocket service is not started");
    }

    // Initialize cameras
    capture = camera::makeCaptureSource();
    if (!capture->open(camera_sources, width, height, fps))
    {
        log_error("Failed to initialize cameras");
        return;
    }

    // Create detector
    detector = DetectorFactory::createDetector(detector_type_name, debug_display, width, height, fps);

    // Acquire the frames the detector calibrates on. The detector is handed frames; it is
    // not handed the capture.
    log_info("Capturing frames for calibration...");
    vector<camera::Frame> calibration_frames = capture->readAveraged(30);

    // Initialize detector
    if (!detector->initialize(calibration_frames, capture->nominalFps()))
    {
        log_error("Failed to initialize detector.");
    }
    else
    {
        log_info("Detector initialized successfully");
    }
}

Scorer::~Scorer()
{
    stop();
}

void Scorer::stop()
{
    running = false;
}

void Scorer::sendResult(const DetectorResult &result)
{
    // Push to queue for WebSocket broadcasting
    score_queue_->push(result);

    // Keep logging for debug
    if (result.dart_detected && getenv("OD_CAPSEAM"))
    {
        log_info("CAPSEAM result timestamp_us=" + to_string(result.timestamp) + " score=" + result.score);
    }

    if (result.dart_detected)
    {
        log_info("SCORE: " + result.score +
                 " | Position: (" + to_string((int)result.position.x) + "," + to_string((int)result.position.y) + ")" +
                 " | Confidence: " + to_string(result.confidence) +
                 " | Camera: " + to_string(result.camera_index) +
                 " | Processing: " + to_string(result.processing_time_ms) + "ms");
    }

    if (debug_display && result.motion_detected)
    {
        log_debug("Motion detected on frame");
    }
}

void Scorer::run()
{
    // #824: a detector that could not calibrate is fatal to scoring and is exactly the
    // state a person opening --setup is in — the board has not been found yet, which is
    // why they want to look. So setup runs the loop anyway and shows the frames; only
    // detection is skipped until the detector says it is ready.
    if (!detector->isInitialized() && !setup_mode)
    {
        log_error("Detector not initialized - cannot run");
        return;
    }
    if (!detector->isInitialized())
    {
        log_warning("SETUP MODE: the detector did not initialise (no calibration). "
                    "Showing the camera feeds anyway - that is what --setup is for.");
    }

    if (setup_mode)
    {
        // The address is not named here and cannot be: see od_socket.hpp.
        setup_streamer_ = make_unique<streamer>(8081, fps > 0 ? fps : 15);
        log_info("SETUP VIEW: open http://127.0.0.1:8081/ in a browser on this machine. "
                 "Ctrl+C stops it and releases the cameras.");
    }
    else
    {
        // Start WebSocket service
        websocket_service_->start();
    }

    running = true;
    // #815: the stream's own period, if camera.hpp already took one, wins over --fps.
    if (!od_fix::fpsFromStream())
    {
        od_clock::frame_period_ms() = 1000.0 / (double)(fps > 0 ? fps : 15);
    }
    log_info("MOTION CLOCK: " + string(od_clock::mode_name()) +
             " frame_period_ms=" + to_string(od_clock::frame_period_ms()));
    log_info("MOTION FIX: " + string(od_fix::selected()) +
             " stability=" + to_string(od_fix::stability()) +
             " spikewin=" + to_string(od_fix::spikewin()) +
             " warnsplit=" + to_string(od_fix::warnsplit()) +
             " safety=" + to_string(od_fix::safety()) +
             " shutdown=" + to_string(od_fix::shutdownFix()));
    // #816: the break of #815 defect 1 puts an event's whole life in STABILIZING,
    // which is the one state max_event_duration_ms is not tested in. Refuse rather
    // than run a machine whose safety timeout cannot fire.
    if (od_fix::breakIsUnguarded())
    {
        log_error("MOTION FIX REFUSED: stability restores the break that keeps an event "
                  "in STABILIZING, and max_event_duration_ms is tested only in "
                  "SPIKE_DETECTED. Add safety to OD_MOTION_FIX. To reproduce #815's "
                  "runs as they were measured, set OD_UNGUARDED_BREAK=1 and say so.");
        exit(78);
    }
    log_info("Scorer running with " + to_string(camera_sources.size()) + " cameras");
    log_info("Using detector: " + detector_type_name);
    cout << "-------------------------------------" << endl;

    // ---- harness, not upstream: one cycle budget, so two runs stop on the same frame.
    // #803, #802 and #811/#815 each wrote one of these; this is the one they became. ----
    const char *max_cycles_env = getenv("OD_MAX_CYCLES");
    const long max_cycles = max_cycles_env ? atol(max_cycles_env) : 0;
    long cycles = 0;
    auto loop_started = chrono::steady_clock::now();
    // The last stream/acquisition position each camera reported, so the budget line can
    // say which frame the run stopped on. #803 read it off the VideoCapture; behind the
    // seam it is the Frame's own.
    vector<long> last_pos_ms;

    while (running)
    {
        if (max_cycles > 0 && cycles >= max_cycles)
        {
            auto loop_ms = chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now() - loop_started).count();
            cout << "[i803] cycle budget reached: cycles=" << cycles
                 << " loop_ms=" << loop_ms
                 << " ensure_calls=" << odfs::ensure_calls.load()
                 << " ensure_failures=" << odfs::ensure_failures.load() << endl;
            for (size_t c = 0; c < last_pos_ms.size(); c++)
            {
                cout << "[i803] cam " << c << " pos_ms=" << last_pos_ms[c] << endl;
            }
            log_info("HARNESS cycle budget reached: " + to_string(cycles));
            log_info("CYCLE BUDGET REACHED: " + to_string(cycles) + " cycles");
            log_info("TIMEOUT CENSUS: safety=" + to_string(od_clock::timeouts(0).load()) +
                     " spike_window=" + to_string(od_clock::timeouts(1).load()));
            motion_processing::dumpTrace();
            running = false;
            break;
        }
        cycles++;

        // start a clock to measure FPS
        auto start_time = chrono::steady_clock::now();

        // 1. Capture frames
        vector<camera::Frame> frames = capture->read();
        last_pos_ms.assign(frames.size(), -1);
        for (size_t c = 0; c < frames.size(); c++)
        {
            last_pos_ms[c] = (long)frames[c].pos_ms;
        }
        // #824: the setup view, pushed before anything else looks at the frames and
        // whether or not the detector is ready. What it shows is the raw feeds side by
        // side and nothing drawn on them — see the note in main.cpp on why the
        // calibration overlay is a different question and is not this.
        if (setup_streamer_ && camera::validCount(frames) > 0)
        {
            Mat setup_view = debug::createCombinedFrame(camera::images(frames), "SETUP");
            if (!setup_view.empty())
                setup_streamer_->push(setup_view);
        }

        if (camera::validCount(frames) > 0 && detector->isInitialized())
        {
            // 2. Process frames by the detector
            DetectorResult result = detector->process(frames);
            // 3. Send result if something detected
            if (result)
            {
                // before seending it; lets add the time it took to process
                auto end_time = chrono::steady_clock::now();
                auto processing_time = chrono::duration_cast<chrono::milliseconds>(end_time - start_time).count();
                result.processing_time_ms = static_cast<int>(processing_time);
                sendResult(result);
            }
        }

    }

    log_info("Scorer stopped");
}