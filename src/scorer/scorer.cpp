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
#include "utils/board_sight.hpp"
#include "utils/od_fix.hpp"
#include "utils/signals.hpp"
#include "utils/geometry_fault.hpp"
#include "detector/geometry/detection/motion_processing.hpp"
#include <random>
#include <opencv2/opencv.hpp>
#include <algorithm>
#include <cstdlib>

using namespace std;
using namespace cv;

// ---- #899: the three numbers the sight-loss lifecycle is spelled with. ----
namespace
{
    // How long every camera has to be silent before the board calls it a sight loss
    // rather than a dropped frame. #798's CAPDROP is the dropped-frame instrument and it
    // fires on one cycle; this is a different observable and wants a different unit.
    // Three seconds is a couple of dozen cycles at any frame rate this program runs at,
    // and it is well inside #895's measured detection bound of 6.58 s, so the board has
    // already suspended scoring before the beat that reports it.
    constexpr long kSightLostAfterSeconds = 3;

    // The wait before the second attempt. The first is immediate, because the commonest
    // cause is a USB device that re-enumerated while the loop was reading and is already
    // back.
    constexpr long kFirstRetryBackoffSeconds = 2;

    // And the ceiling it doubles up to. A board that has been blind all night should
    // cost a line a minute and one open() a minute, not a hot loop against a device node
    // that is not there. This is the whole of "does repeated failure differ from the
    // first": it differs by how often it is tried and by a sentence that says how long
    // it has been going on, and it does not differ by becoming terminal -- a camera that
    // is not plugged in yet is exactly the state a retry improves.
    constexpr long kMaxRetryBackoffSeconds = 60;

    // Frames averaged for the calibration the recovery is judged on. The same 30 the
    // constructor calibrates with, deliberately: a witness taken from fewer frames than
    // the geometry it is being compared to would be a noisier measurement judged against
    // a quieter one, and every bit of that noise reads as movement.
    constexpr int kReviewFrames = 30;

    // ---- #1388 / ADR-0080: the retry budget, and the measurement it came from. ----
    //
    // A `Moved` verdict no longer ends the run. The rig is bolted to the wall and the
    // cameras are fixed to the rig (ADR-0079 §3, ADR-0080), so there is no re-aiming and
    // the commonest cause of a single disagreement is a bump -- somebody knocks the
    // frame, or a dart strikes it rather than the board, which happens DURING PLAY and is
    // therefore an expected event rather than an edge case. A transient disagrees once
    // and then agrees again; an assembly that has really shifted disagrees every time it
    // is asked. Asking again is the whole discriminator, and these two numbers are how
    // many times and how far apart.
    //
    // MEASURED, on both fixtures, by testers/phases1388/1388-budget.sh. What was measured
    // is the only observable the event has: the board's own witness measurement, taken
    // the way `attemptRecovery` takes it -- `geometry_calibration::calibrateSingleCamera`
    // on the mean of thirty consecutive frames -- against a calibration held from the
    // clean opening of the clip, once a second across a minute of darts being thrown at a
    // rig nobody touched. The figure that decides a budget is the LONGEST RUN OF
    // CONSECUTIVE DISAGREEING SAMPLES, because that is how long a board asking repeatedly
    // would go on being told the rig had moved when it had not:
    //
    //     mocks/cam_1.mp4              6 consecutive samples = 6.00 s   <-- the maximum
    //     mocks/cam_2.mp4              4                     = 4.00 s
    //     mocks/cam_3.mp4              3                     = 3.00 s
    //     mocks/rig-20260918/cam_1     2                     = 2.00 s
    //     mocks/rig-20260918/cam_2     1                     = 1.00 s
    //     mocks/rig-20260918/cam_3     1                     = 1.00 s
    //
    // Worth reading the shape of the worst one, because it is not the shape anybody would
    // have guessed: through all six of cam_1's seconds the BULL moved 1.00 px and the
    // doubles ring changed size by 39%. The disturbance this budget has to outlast is the
    // ellipse fit, not the bull -- `max_radius_change` is the term that fires during
    // ordinary play, and it is the one term in geometry_agreement.hpp with no measured
    // positive behind it. Across both fixtures it is 44 of the 45 disagreements a rig
    // nobody touched produced, at a stable ~39% on the shipped mocks and ~64% on the rig,
    // which is a different RING being fitted rather than a board changing size. That is
    // #1416 -- which measured it, and it is NOT a tolerance question. Two witnesses that
    // are not the radius (the fitted ring against the 50-bull, and the ring's own width as
    // a fraction of its own radius) name all 44 as the ellipse stage having fitted a
    // different ring, none as a camera; and no sample on either fixture has a radius
    // change anywhere between 3.29% and 38.26%, so every tolerance in that band returns
    // the same verdict and there is nothing in this constant to fit. It is #1423's
    // ground -- nothing in the calibration path can tell the doubles ring from the treble
    // -- and this is a budget, and the budget survives it either way.
    //
    // THE BUDGET IS A SPAN, AND THE TWO NUMBERS ARE HOW IT IS SPENT. An attempt is not
    // free: it reopens the cameras, reads thirty frames and re-calibrates every camera
    // that was scoring, so the measurements are further apart than the wait alone. That
    // was measured on a running board rather than reasoned about -- the same harness,
    // phase 3, reads the seconds-blind figure each attempt prints, and the four
    // measurements of a spent budget landed at 11, 15, 19, 23 on one run and at 12, 16,
    // 20, 23 on the next:
    //
    //     4 measurements, about 4 s apart (2 s of wait, 2 s of measuring three cameras)
    //     = an 11 to 12 s span from the first disagreement to the last
    //
    // Nearly twice the longest disturbance measured on either rig, and the margin is
    // deliberate, because 6.00 s is the longest disturbance seen in two minutes of
    // footage rather than the longest one there is. The harness asserts the span against the
    // disturbance, in seconds, on every run -- so a slower box, a fourth camera or a
    // moved constant fails here rather than in a pub.
    //
    // AND THE COST OF BEING WRONG IS NOT SYMMETRIC, which is why the margin goes this
    // way. A board spending this budget is a board that has already suspended scoring, so
    // an over-long budget costs recovery latency and nothing else; an over-short one
    // takes a board down for the rest of the evening because a dart hit the frame. ADR-
    // 0080's "leaves a shifted rig scoring for longer than it should" is the danger on
    // some future path where the board is asked mid-scoring; it is not the danger here,
    // and saying so is better than inheriting a caution that does not apply.
    //
    // The first is the disagreement itself, so three of the four are re-asks.
    constexpr int kMovedAttempts = 4;

    // Flat, and NOT the doubling backoff the `Unreadable` path uses. That backoff exists
    // because a camera that will not open may not open for hours and the board must not
    // spend the night in a hot loop; this is the opposite state -- the cameras are open,
    // answering, and have produced a measurement -- and the question is whether one
    // bounded disturbance has passed. Doubling would spend the budget's last attempt a
    // minute after the bump, which measures nothing the first four seconds did not.
    constexpr long kMovedWaitSeconds = 2;
}

Scorer::Scorer(const string &model, int w, int h, int fps, const vector<string> &cams, bool debug_mode, const string &detector_type,
               const ScoreSocketSettings &socket)
    : model_path(model), width(w), height(h), fps(fps), camera_sources(cams), debug_display(debug_mode), detector_type_name(detector_type)
{
    // Initialize score queue and WebSocket service
    score_queue_ = std::make_shared<ScoreQueue>();
    // #1187: the socket's bind address and token come from main. #812: debug_display
    // is --debug, and decides whether this listener serves the saved camera frames as
    // well as the scores.
    websocket_service_ = std::make_unique<WebSocketService>(score_queue_, socket, debug_display);

    // #1388 / ADR-0080 §4: a persistent `Moved` recorded by an earlier run of this board.
    //
    // FIRST, BEFORE THE CAMERAS ARE OPENED, because what is being refused is the
    // ADOPTION and the adoption is the calibration. A board that opened its cameras and
    // calibrated and then declined to score would have measured a new geometry, written
    // it to the cache and logged it as this board's -- and the whole of ADR-0080 §3 is
    // that a rig which has shifted on its bolts does not get re-measured by the machine
    // that is standing on it. So this board looks at nothing.
    //
    // It takes #895's vigil, which is what `detector` being null means here: it stays up,
    // beats ERROR, and says which camera moved and by how much, every time somebody
    // reads the log. It does not exit, because an exited board under Restart=always is
    // exactly the loop this record exists to break.
    const string moved_before = geometry_fault::held();
    if (!moved_before.empty())
    {
        log_error("BOARD HOLDING A GEOMETRY FAULT: " + moved_before +
                  ". An earlier run measured the rig as being somewhere other than where it "
                  "was calibrated, and the disagreement did not go away. This board will not "
                  "open a camera or calibrate until somebody has looked at the rig: the frame "
                  "has moved relative to the board and a detector cannot put it back. Check "
                  "that the frame is still bolted where it was, then clear this with "
                  "--clear-geometry-fault.");
        board_sight::recordFault(moved_before);
        board_sight::faulted() = true;
        return;
    }

    // Initialize cameras
    capture = camera::makeCaptureSource();
    if (!capture->open(camera_sources, width, height, fps))
    {
        log_error("Failed to initialize cameras");
        // #892: a camera that will not open is a board that cannot see, and it is the
        // one thing a beat can say that silence cannot -- a machine that is running and
        // blind is a different errand from a machine that is off.
        // #1321: and the vigil should say which of the two it was, so it is recorded
        // here, where it is known, rather than guessed at four hundred lines away.
        string tried;
        for (const string &source : camera_sources)
        {
            tried += (tried.empty() ? "" : ", ") + source;
        }
        board_sight::recordFault("the cameras did not open (tried: " + tried + ")");
        board_sight::faulted() = true;
        return;
    }
    board_sight::camerasOpen() = true;

    // Create detector
    detector = DetectorFactory::createDetector(detector_type_name, debug_display, width, height, fps);

    // Acquire the frames the detector calibrates on. The detector is handed frames; it is
    // not handed the capture.
    //
    // #1445: and a way to be handed another one, which is the same sentence rather than an
    // exception to it. `capture` stays here; what crosses the boundary is a function that
    // answers with frames of the type the line below already produces, and a detector can
    // do nothing with it but ask for another picture -- it cannot open, close, re-aim or
    // even name a camera. The offer is made before `initialize` because looking again is
    // part of calibrating, not something that happens to a calibrated board: nothing may
    // reach for this once the geometry is sealed, and `lookAgainAtRefusedCameras` refuses
    // to run if anything tries (ADR-0080 §2).
    log_info("Capturing frames for calibration...");
    detector->offerFurtherLooks([this]
                                { return capture->read(); });
    vector<camera::Frame> calibration_frames = capture->readAveraged(30);

    // Initialize detector
    if (!detector->initialize(calibration_frames, capture->nominalFps()))
    {
        log_error("Failed to initialize detector.");
        // #892: calibration is the second half of being able to see. A detector that
        // did not calibrate is as blind as a camera that did not open, and READY says
        // "calibration is valid" as well as "frames are arriving".
        // #1321: geometry_calibration records the camera and the count that fell short
        // as it meets them, so the usual case is that this is already written. The
        // fallback is for a detector that failed with nothing to say -- no frame to
        // calibrate on at all.
        board_sight::recordFault("the detector did not calibrate on the frames the cameras gave");
        board_sight::faulted() = true;
    }
    else
    {
        log_info("Detector initialized successfully");
        board_sight::calibrated() = true;
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
        // #1186: the board-frame fields are logged by score_processing on their own BOARD
        // line, with whether the wedge was measured, so the SCORE line above stays
        // byte-for-byte what every control in the research chain was extracted from.
    }

    if (debug_display && result.motion_detected)
    {
        log_debug("Motion detected on frame");
    }
}

// #1274: the question run() asks below, asked out loud so main can ask it too. A board
// that cannot see takes the fault vigil, and the vigil does not start the score socket --
// so this is also the answer to "will this board have a socket to announce".
bool Scorer::canSee() const
{
    return detector && detector->isInitialized();
}

void Scorer::run()
{
    // #895: the null check that was missing, and -- more to the point -- the thing that
    // happens instead of a return.
    //
    // `detector` is null when the constructor's camera branch took its early return, and
    // dereferencing it here is the SIGSEGV #892 measured (PROGRAM_RC=139) while trying to
    // produce a blind board. The calibration branch leaves a detector that says it is not
    // initialised, and that one did not crash -- it fell through this return, unwound
    // main and exited quietly, which is the same information loss without the core file.
    // Both are one condition and both get one answer.
    //
    // Adding `!detector` and keeping the return would have traded a crash for a silent
    // do-nothing: the program would still stop, still stop beating, and the Station's
    // screen would still degrade to `silent` after the server's own window -- telling a
    // pub the board stopped answering when what happened is that a camera did not open.
    // Those two want different remedies and #823 spent a slice making the screen say the
    // right one.
    //
    // So the object is constructed and this thread stays. See runFaultVigil().
    //
    // #1274: the condition is canSee(), because main asks the same question before it
    // announces the board. Two spellings of it could drift apart, and the failure that
    // drift makes is a board announced on the network with nothing listening.
    if (!canSee())
    {
        log_error("Detector not initialized - cannot run");
        runFaultVigil();
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
    // #1338: the detector's own census, not a second count of the sources this Scorer was
    // asked to open. Those are different numbers whenever a camera fails, and printing the
    // source count here is what put `Scorer running with 3 cameras` under `Initial
    // calibration completed successfully on 1 of 3 cameras` in the run this was filed on.
    const string census = detector ? detector->scoringWith() : string();
    log_info("Scorer running with " + (census.empty()
                                           ? to_string(camera_sources.size()) + " camera sources, census unknown"
                                           : census));
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

    // ---- #899: the board's sight, and what it takes to get it back. ----
    // See the docblock above attemptRecovery() for the decision this is the mechanism
    // of. The state is three numbers and one flag, all local to this loop, because the
    // whole of it lives and dies with one run of the scoring thread.
    auto last_sight = chrono::steady_clock::now();
    auto next_attempt_at = last_sight;
    bool scoring_suspended = false;
    int recovery_attempts = 0;
    long backoff_seconds = kFirstRetryBackoffSeconds;
    // #1388: consecutive `Moved` verdicts in the episode being recovered from. The
    // budget, counted. Reset by agreement and by nothing else.
    int disagreements = 0;

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
        const bool saw_something = camera::validCount(frames) > 0;

        // ---- #1282: the footage ended, which is not the board going blind ----
        //
        // Every source is a file and every one of them has run out. Only mock input can
        // reach this; a rig cannot, because a camera does not end (camera::CaptureSource
        // ::footageEnded is false for any source that is a device). So this is NOT #895's
        // vigil and does not touch it: a board that cannot see still suspends scoring,
        // still retries for ever and still never exits.
        //
        // Before this the run fell through to the blind path instead, and two things
        // happened. Its first three seconds wrote four ERROR lines per iteration with
        // nothing pacing them -- about 200 MB of stdout in one Windows release run -- and
        // then attemptRecovery() reopened the sources, which rewinds a file to frame 0, so
        // the clip played again and the whole thing repeated for as long as anybody let it.
        if (capture->footageEnded())
        {
            auto loop_ms = chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now() - loop_started).count();
            log_info("END OF FOOTAGE: every file source has reached its end after " +
                     to_string(cycles) + " cycles (" + to_string(loop_ms) +
                     " ms). This board was given recordings, and a recording ends; the run ends with it.");
            for (size_t c = 0; c < last_pos_ms.size(); c++)
            {
                log_info("END OF FOOTAGE cam " + to_string(c) + " last pos_ms=" + to_string(last_pos_ms[c]));
            }
            running = false;
            break;
        }

        // #899: a camera answering again is not permission to score. While the board is
        // suspended it reads frames and does nothing with them -- it does not process
        // them, it does not send a result, and it does not count the cycle towards
        // READY. The frames are read anyway because reading is how the recovery finds
        // out the cameras are back, and because a slot that stops being read stops
        // reporting CAPDROP for the camera that is still missing.
        // #1388 / ADR-0080 §2: nothing adopts fresh geometry mid-run, in any path.
        //
        // Asked here, before the frames are processed, of every cycle of every run --
        // including the cycles after a recovery, which is the whole reason it exists. The
        // detector holds one line describing the geometry it calibrated on, taken at the
        // end of initialize(); this is that line compared with the geometry it is about
        // to score the next dart with. `reviewGeometry` deliberately keeps its fresh
        // calibrations as locals, so on a correct board the two can never differ -- and
        // that is the point. A guard nothing can trip is not evidence, so the harness
        // trips it: testers/phases1388/1388-budget.sh makes a scratch copy of the tree in
        // which reviewGeometry assigns the fresh calibration over the held one, and the
        // board it builds says the sentence below instead of scoring.
        //
        // The board STOPS rather than logging and carrying on, for #899's reason: a board
        // scoring on geometry nobody confirmed is the silent failure the whole of this
        // lifecycle exists to make impossible, and it is worse, not better, when the
        // geometry came from the board itself.
        const string breach = detector ? detector->geometryBreach() : string();
        if (!breach.empty())
        {
            log_error("BOARD ADOPTED GEOMETRY MID-RUN: " + breach +
                      ". No path in this program may replace the calibration a board is "
                      "scoring with while it is running -- a geometry that has not been "
                      "confirmed is a board putting darts in the wrong wedge with every "
                      "control still looking like darts (ADR-0080 section 2). It refuses to "
                      "score rather than go on.");
            board_sight::recordFault("the geometry was replaced while the board was running - " + breach);
            board_sight::faulted() = true;
            if (websocket_service_)
            {
                websocket_service_->stop();
            }
            running = false;
            break;
        }

        if (saw_something && !scoring_suspended)
        {
            last_sight = chrono::steady_clock::now();
            // #892: the one observation READY rests on, taken where it is made. A cycle
            // that read no valid frame does not count, so a board whose cameras have
            // stopped answering stops earning the word within one beat -- and it says
            // ERROR rather than going silent, because it is still there to say it.
            board_sight::framesSeen().fetch_add(1, std::memory_order_relaxed);
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

        // ---- #899: sight lost, and the road back ----
        const auto now = chrono::steady_clock::now();
        if (!scoring_suspended && !saw_something)
        {
            const long blind_for = (long)chrono::duration_cast<chrono::seconds>(now - last_sight).count();
            if (blind_for >= kSightLostAfterSeconds)
            {
                scoring_suspended = true;
                recovery_attempts = 0;
                disagreements = 0;
                backoff_seconds = kFirstRetryBackoffSeconds;
                next_attempt_at = now;
                log_error("BOARD SIGHT LOST: no camera has answered for " + to_string(blind_for) +
                          " seconds. Scoring is suspended from here and stays suspended until the "
                          "cameras come back AND the calibration this board holds is confirmed "
                          "against what they can see. The beat says ERROR throughout.");
            }
        }

        if (scoring_suspended && now >= next_attempt_at)
        {
            const long blind_for = (long)chrono::duration_cast<chrono::seconds>(now - last_sight).count();
            const GeometryReview review = attemptRecovery(++recovery_attempts, blind_for);

            if (review.verdict == GeometryReview::Verdict::Unchanged)
            {
                // #1388: including after one or more disagreements. This is ADR-0080 §2
                // and it is the half of #899 that had to survive the amendment whole:
                // what resumes is the HELD calibration. The fresh ones taken to reach
                // this line were witnesses, every one of them was a local inside
                // reviewGeometry, and the seal checked at the top of this loop is what
                // makes that a fact rather than an intention.
                if (disagreements > 0)
                {
                    log_warning("BOARD SETTLED: the geometry disagreed on " +
                                to_string(disagreements) + " of " + to_string(kMovedAttempts) +
                                " measurement(s) and then agreed again. On a rig that is bolted "
                                "to the wall a disagreement that goes away is a knock -- a dart "
                                "into the frame, somebody against it -- and the rig is back "
                                "where it was bolted. Scoring resumes on the calibration this "
                                "board started with. Nothing was adopted.");
                }
                disagreements = 0;
                scoring_suspended = false;
                last_sight = chrono::steady_clock::now();
            }
            else if (review.verdict == GeometryReview::Verdict::Moved)
            {
                // ---- #1388 / ADR-0080 §1: the retry budget, spent here. ----
                //
                // The count is of CONSECUTIVE disagreements, and it is reset by an
                // `Unchanged` above rather than by anything here. `Unreadable` neither
                // spends the budget nor resets it, which is the one case worth saying out
                // loud: a camera that stopped answering again in the middle of the budget
                // is not evidence either way about where the rig is, and treating it as a
                // disagreement would fault a board for a USB cable.
                disagreements++;
                if (disagreements < kMovedAttempts)
                {
                    log_warning("BOARD GEOMETRY: measurement " + to_string(disagreements) +
                                " of " + to_string(kMovedAttempts) + " disagrees. Asking again in " +
                                to_string(kMovedWaitSeconds) + " seconds, because a bolted rig that "
                                "disagrees once has been knocked and a bolted rig that has moved "
                                "disagrees every time it is asked.");
                    next_attempt_at = chrono::steady_clock::now() + chrono::seconds(kMovedWaitSeconds);
                }
                else
                {
                    // Persistent. The assembly has shifted relative to the board, the held
                    // geometry is wrong, the fresh geometry is right, and the board still
                    // does not adopt it (ADR-0080 §3). What is new is that the refusal
                    // outlives this process: without the record, Restart=always plus
                    // #1330's default brings the board straight back up calibrating on
                    // the rig as it now is, which is the adoption #899 refused arriving
                    // through the unit file.
                    const string account = review.account;
                    if (geometry_fault::record(account))
                    {
                        log_error("GEOMETRY FAULT RECORDED at " + geometry_fault::path() +
                                  ": this board will refuse to calibrate on restart until an "
                                  "operator clears it with --clear-geometry-fault.");
                    }
                    board_sight::recordFault("the cameras came back and the board is not where it was - " + account);
                    board_sight::faulted() = true;
                    log_error("BOARD MOVED: " + account +
                              ". The cameras answer and the geometry this board was scoring with is no "
                              "longer true of them, so it refuses to score rather than put darts in the "
                              "wrong wedge. Put the camera back where it was, or restart the detector so "
                              "it calibrates on the rig as it is now.");
                    log_error("BOARD MOVED: the disagreement persisted through all " +
                              to_string(kMovedAttempts) + " measurements " + to_string(kMovedWaitSeconds) +
                              " seconds apart, so it is not a knock the rig has settled from. The frame "
                              "has moved relative to the board and somebody has to look at it.");
                    // #895's argument one layer down: a board that cannot see must not go
                    // on serving a score socket that looks alive. main's mDNS announcement
                    // is not withdrawn from here -- that is #1274's surface and it is
                    // decided before run() is called.
                    if (websocket_service_)
                    {
                        websocket_service_->stop();
                    }
                    running = false;
                    break;
                }
            }
            else
            {
                next_attempt_at = chrono::steady_clock::now() + chrono::seconds(backoff_seconds);
                backoff_seconds = min(backoff_seconds * 2, (long)kMaxRetryBackoffSeconds);
            }
        }

        if (scoring_suspended)
        {
            // A suspended cycle reads three cameras that answer nothing and does no work
            // at all, so it costs about a millisecond and the loop would otherwise run at
            // a kilohertz against a device node that is not there -- a thousand CAPDROP
            // lines a second into a log somebody has to read afterwards. A fifth of a
            // second is the same pace #895's vigil settled on, and is still twenty times
            // finer than the shortest backoff.
            this_thread::sleep_for(chrono::milliseconds(200));
        }
    }

    // #899: a board that stopped because its geometry could not be confirmed does not
    // return to main and exit -- that is the silence #895 spent a slice replacing. It
    // takes the same vigil a board that never came up takes, so the pub's screen goes on
    // being told to look at the computer and the log goes on saying which camera moved.
    if (board_sight::faulted().load())
    {
        log_info("Scorer stopped");
        runFaultVigil();
        return;
    }

    log_info("Scorer stopped");
}

/**
 * #895: a board that is up and cannot see stays up and says so.
 *
 * WHY A LOOP AND NOT A RETURN, AND NOT A THROWING CONSTRUCTOR EITHER.
 *
 * The word for a camera that will not open is `ERROR` (board_sight.hpp's ladder), and
 * `ERROR` is only reachable by a process that is still running to send it. A board that
 * exits sends nothing, and the server's only remaining reading is the silence window --
 * which is safe, because silence is always safe, and weaker than the truth, because it
 * cannot distinguish a computer that is switched off from one that is watching nothing.
 *
 * The other shape considered was a constructor that refuses to build -- a factory
 * returning nothing, or a throw -- and telling main. It was rejected because it puts the
 * vigil in main: main would need its own loop observing the same shutdown flag, and its
 * own ownership of the TurnausClient for the length of it. #822 put the client inside
 * Scorer deliberately so that #825's unwinding of main -- ~Scorer -- is what stops and
 * joins its threads. Splitting the fault case out would make the beat's owner depend on
 * whether a camera opened. One owner is better than two, so the object is built in a
 * state it can report honestly rather than not built at all.
 *
 * What this deliberately does NOT do:
 *  - It does not start the 13520 WebSocket service. That listener is the pub's local
 *    surface for scores and saved frames (#812), and a blind board serving an empty score
 *    socket is ADR-0055's own worry one layer down: something that looks alive in front of
 *    a board that sees nothing. Nothing started it on this path before, so nothing is lost.
 *    #1274: and because it does not, a board on this path is not announced on the network
 *    either. main asks canSee() before it publishes #1189's service file, and withdraws
 *    any file an earlier run left. Until #1274 the announcement was published above the
 *    Scorer, so a dark board taken to this vigil advertised a score socket that this
 *    function never opens, and a phone that found the board by it connected to nothing.
 *  - It does not retry the cameras. A supervisor restarting the process is the remedy
 *    that exists for a board that never came up, and making that fault recoverable is a
 *    different issue from making it reportable.
 *    #899: still true of THIS function, and no longer true of the program. A board that
 *    came up and then lost every camera retries where it stands, in the scoring loop, and
 *    only arrives here when the retry has been tried and refused -- see attemptRecovery().
 *    Why the construction-time fault was deliberately left out of that is written there
 *    too, and the short version is #1274: whether the board is announced on the network
 *    is decided in main before run() is called.
 *  - It does not call exit(). It leaves by the same `return` the scoring loop leaves by,
 *    so #825's exit path -- main unwound, threads joined, destructors run -- is the exit
 *    path here too, and `Scorer stopped` is logged on this route as well.
 */
void Scorer::runFaultVigil()
{
    // The beat is already running: main starts the client before the Scorer is built
    // (#892), so the ladder has been answering INITIALISING since before the cameras
    // were tried, and board_sight::faulted() -- set by whichever constructor branch
    // failed -- turns the next beat into ERROR. Nothing has to be sent from here.
    //
    // #1321: what it says is the sentence recorded where the fault was observed, not a
    // choice of two offered to the reader. A message that names both halves of the
    // ladder and commits to neither is the same as no message: whoever is standing at
    // the machine still has to find out which.
    const string detail = board_sight::faultDetail().empty()
                              ? string("this board cannot see, and nothing said why")
                              : board_sight::faultDetail();

    log_error("BOARD FAULTED: " + detail + ". It stays up and beats ERROR rather than "
                                           "exiting, so the Station's screen says 'go and look at the computer' "
                                           "instead of 'the board stopped answering'. Check the cameras and "
                                           "restart the detector.");

    running = true;
    auto last_reminder = chrono::steady_clock::now();

    while (running)
    {
        if (int sig = signals::shutdownRequested())
        {
            log_warning("Received signal " + to_string(sig) +
                        ", shutting down a faulted board...");
            running = false;
            break;
        }

        // A log somebody tails should go on saying it, and once a minute is often enough
        // that a reader knows the process is alive and rare enough to be free.
        auto now = chrono::steady_clock::now();
        if (chrono::duration_cast<chrono::seconds>(now - last_reminder).count() >= 60)
        {
            last_reminder = now;
            log_error("BOARD FAULTED: still blind, still beating ERROR -- " + detail + ".");
        }

        // Nothing here is on a deadline; the beat has its own thread and its own
        // interval. A fifth of a second is a shutdown latency nobody notices and a cost
        // that does not appear in any per-cycle figure.
        this_thread::sleep_for(chrono::milliseconds(200));
    }

    log_info("Scorer stopped");
}

/**
 * #899: WHAT A BOARD DOES WHEN A CAMERA STOPS ANSWERING, AND WHY IT IS THIS.
 *
 * #895 made a camera failure reportable and deliberately not recoverable: somebody
 * unplugs a camera, plugs it back in, and the board goes on beating ERROR until it is
 * restarted. Three answers were available and they are genuinely different.
 *
 *   NEVER RETRY. What shipped. Honest, and it costs an evening when nobody is watching
 *   the screen.
 *
 *   RETRY AND RE-CALIBRATE. Recovers unattended, and it is the dangerous one. A camera
 *   that stopped answering was very likely TOUCHED -- a reseated plug, a nudged tripod.
 *   Calibration is a fact about where the cameras are, so a board that reopens its
 *   cameras and goes back to scoring has resumed on geometry that may no longer be true,
 *   and that failure is silent: the darts land in the wrong wedge and every control still
 *   looks like darts. Replacing the geometry instead of keeping it is no better -- see
 *   geometry_agreement.hpp -- because a calibration taken at nine in the evening is taken
 *   with darts in the board, and nothing compares it to anything.
 *
 *   RETRY AND REFUSE TO SCORE UNTIL A PERSON CONFIRMS THE BOARD HAS NOT MOVED. Safe, and
 *   it needs a screen affordance that does not exist.
 *
 * WHAT IS IMPLEMENTED IS THE THIRD ONE WITH THE MACHINE AS THE WITNESS. The board retries,
 * and it refuses to score until the question "has the board moved" has been ANSWERED --
 * but it is answered by measurement rather than by asking somebody who was not in the
 * room. The board takes a fresh calibration, compares it to the one it has been scoring
 * with, and throws the fresh one away: agreement is evidence that nothing was touched, so
 * the held geometry is still true and scoring may resume on it; disagreement is evidence
 * that something was touched and the board does not know what, so it faults for good.
 *
 * The machine is the better witness of the two, and that is not a convenience argument.
 * A person at the board is asked "did anyone move anything?" and answers about what they
 * remember. The board is asked "is camera 2's bull where it was?" and answers with a
 * number. #1318 and #1320 are what made that answer worth having -- before them a camera
 * that was not looking at a dartboard still produced a confident calibration, so two
 * confident wrong geometries could have agreed with each other. A camera that can no
 * longer see a board is now refused BY NAME rather than compared, and a bull with no
 * trustworthy centre is refused before it is ranked, so "these two calibrations agree" is
 * a sentence about a dartboard now instead of a sentence about two ellipse fits.
 *
 * THE RULE UNDERNEATH BOTH HALVES: A BOARD NEVER SCORES ON GEOMETRY IT HAS NOT JUST
 * CONFIRMED. At start the confirmation is the calibration itself. After a sight loss it
 * is this comparison. There is no third case in which scoring resumes.
 *
 * WHAT THIS DELIBERATELY DOES NOT DO.
 *
 *  - It does not retry a board that never came up. A construction-time fault -- cameras
 *    that would not open at start, a calibration that failed on the first frames -- still
 *    goes straight to #895's vigil and stays there. That half is SAFER than this one,
 *    because a board with no geometry has nothing stale to resume on, and it is left out
 *    for a mechanical reason rather than a nervous one: #1274 decides whether the board
 *    is announced on the network from scorer.canSee(), in main, BEFORE run() is called.
 *    A board that recovered inside the vigil would open a score socket that main has
 *    already decided not to announce, and re-publishing the announcement afterwards is
 *    main's surface, not this one. It is worth doing and it is a different slice.
 *
 *  - It does not invent a fifth word for the beat. #892 fixed the vocabulary at four and
 *    the server's reading of it; a board that has lost its sight and is trying to get it
 *    back is running and blind, which is ERROR, and a board that is refusing to score
 *    until it is confirmed is also running and blind. Saying anything warmer than ERROR
 *    while the geometry is unconfirmed is the exact failure ADR-0055 calls the one that
 *    must be impossible.
 *
 *  - It does not tell the SERVER that it recovered. It tells the log, loudly, with the
 *    numbers. An operator who saw ERROR and now sees READY has no way from the beat alone
 *    to tell a fixed board from a board that gave up and lied, and closing that needs a
 *    field in the beat and a Turnaus that reads it -- another repository and another
 *    decision.
 */
GeometryReview Scorer::attemptRecovery(int attempt, long blind_seconds)
{
    log_warning("BOARD SIGHT RECOVERY: attempt " + to_string(attempt) + " after " +
                to_string(blind_seconds) + " seconds blind - reopening " +
                to_string(camera_sources.size()) + " camera(s)");

    if (!capture->open(camera_sources, width, height, fps))
    {
        log_warning("BOARD SIGHT RECOVERY: the cameras still will not open. Scoring stays "
                    "suspended and this will be tried again.");
        return GeometryReview{GeometryReview::Verdict::Unreadable, "the cameras still will not open"};
    }

    vector<camera::Frame> review_frames = capture->readAveraged(kReviewFrames);
    if (camera::validCount(review_frames) == 0)
    {
        log_warning("BOARD SIGHT RECOVERY: the cameras opened and produced no frame. Scoring "
                    "stays suspended and this will be tried again.");
        return GeometryReview{GeometryReview::Verdict::Unreadable, "the cameras opened and produced no frame"};
    }

    const GeometryReview review = detector->reviewGeometry(review_frames);

    switch (review.verdict)
    {
    case GeometryReview::Verdict::Unchanged:
        // The one place in this program where a board goes from not scoring to scoring
        // without having been started. It says so in one line, with the measurement, so
        // that a log tailed the next morning tells a board that was fixed from a board
        // that quietly resumed.
        log_info("BOARD RECOVERED: the cameras are back after " + to_string(blind_seconds) +
                 " seconds and the board has not moved - " + review.account +
                 ". Scoring resumes on the calibration this board started with; the "
                 "calibration just taken was a witness and has been discarded.");
        return review;

    case GeometryReview::Verdict::Moved:
        // #1388 / ADR-0080 §1: no longer terminal here, and no longer terminal ANYWHERE
        // in this function. A disagreement is a measurement, and what a board does about
        // a measurement taken once is the caller's decision -- the run loop counts them,
        // waits, and asks again, because a bolted rig that disagrees once has been bumped
        // and a bolted rig that disagrees every time has moved. The sentence that used to
        // be here said "nothing a retry can do makes a moved camera un-moved", which is
        // true of a moved camera and is the thing this cannot tell from a bumped one
        // without asking twice.
        //
        // It is still said out loud on every attempt, at ERROR, with the numbers: a board
        // that disagreed twice and recovered on the third ask is a board somebody should
        // know about, and ADR-0080 §5 sends that to the heartbeat rather than to a
        // surface of its own.
        log_error("BOARD GEOMETRY DISAGREES: " + review.account +
                  ". The cameras answer and the geometry this board is scoring with is not "
                  "true of what they see. Scoring stays suspended and the board will measure "
                  "again before deciding whether this was a knock or a move.");
        return review;

    default:
        log_warning("BOARD SIGHT RECOVERY: " + review.account +
                    ". Scoring stays suspended and this will be tried again.");
        return GeometryReview{GeometryReview::Verdict::Unreadable, review.account};
    }
}
