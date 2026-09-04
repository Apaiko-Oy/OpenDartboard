#include "scorer.hpp"
#include "logging.hpp"
#include "utils.hpp"
#include "detector/detector_factory.hpp"
#include "communication/websocket_service.hpp"
#include "communication/score_queue.hpp"
#include <iostream>
#include <thread>
#include <chrono>
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
    websocket_service_ = std::make_unique<WebSocketService>(score_queue_, 13520);

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
    if (!detector->isInitialized())
    {
        log_error("Detector not initialized - cannot run");
        return;
    }

    // Start WebSocket service
    websocket_service_->start();

    running = true;
    log_info("Scorer running with " + to_string(camera_sources.size()) + " cameras");
    log_info("Using detector: " + detector_type_name);
    cout << "-------------------------------------" << endl;

    // ---- harness, not upstream: one cycle budget, so two runs stop on the same frame.
    // #803 opened it, #802 wrote a second one over the seam; they are one here. ----
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