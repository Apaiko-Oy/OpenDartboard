#include "scorer.hpp"
#include "logging.hpp"
#include "utils.hpp"
#include "detector/detector_factory.hpp"
#include "communication/websocket_service.hpp"
#include "communication/score_queue.hpp"
#include "communication/turnaus_client.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <cstdlib>
#include "utils/od_clock.hpp"
#include "utils/od_fix.hpp"
#include "utils/signals.hpp"
#include "detector/geometry/detection/motion_processing.hpp"
#include <random>
#include <opencv2/opencv.hpp>
#include <algorithm>
#include <cstdlib>

using namespace std;
using namespace cv;

Scorer::Scorer(const string &model, int w, int h, int fps, const vector<string> &cams, bool debug_mode, const string &detector_type)
    : model_path(model), width(w), height(h), fps(fps), camera_sources(cams), debug_display(debug_mode), detector_type_name(detector_type)
{
    // Initialize score queue and WebSocket service
    score_queue_ = std::make_shared<ScoreQueue>();
    // #812: debug_display is --debug. It decides whether this listener serves the
    // saved camera frames as well as the scores.
    websocket_service_ = std::make_unique<WebSocketService>(score_queue_, 13520, debug_display);

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

void Scorer::attachTurnaus(std::unique_ptr<TurnausClient> client)
{
    turnaus_ = std::move(client);
}

void Scorer::sendResult(const DetectorResult &result)
{
    // Push to queue for WebSocket broadcasting
    score_queue_->push(result);

    // #822: and hand the same result to the outbound client. offer() takes a mutex,
    // pushes onto a deque and returns -- no socket, no file, no allocation the network
    // can stall. A board whose Turnaus is unreachable spends the same time here as one
    // whose Turnaus answers, and a board that was never paired spends less.
    if (turnaus_)
    {
        turnaus_->offer(result);
    }

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
    if (!detector->isInitialized())
    {
        log_error("Detector not initialized - cannot run");
        return;
    }

    // Start WebSocket service
    websocket_service_->start();

    // #822: and the outbound client, if there is one. It starts its own worker thread;
    // an unpaired board starts nothing and says so once.
    if (turnaus_)
    {
        turnaus_->start();
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
        // #825: the flag the signal handler set, observed here. This is the exit path
        // the program did not have: the loop leaves, run() returns, main returns, and
        // every destructor in the program runs with the threads already joined.
        // Granularity is one cycle -- a signal arriving while capture->read() is
        // blocked on a device is seen when that read returns.
        if (int sig = signals::shutdownRequested())
        {
            log_warning("Received signal " + to_string(sig) +
                        ", finishing the cycle in flight and shutting down...");
            running = false;
            break;
        }
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
        if (camera::validCount(frames) > 0)
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