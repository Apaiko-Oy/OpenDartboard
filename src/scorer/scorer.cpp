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
    log_info("Capturing frames for calibration...");
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

    // ---- #899: the board's sight, and what it takes to get it back. ----
    // See the docblock above attemptRecovery() for the decision this is the mechanism
    // of. The state is three numbers and one flag, all local to this loop, because the
    // whole of it lives and dies with one run of the scoring thread.
    auto last_sight = chrono::steady_clock::now();
    auto next_attempt_at = last_sight;
    bool scoring_suspended = false;
    int recovery_attempts = 0;
    long backoff_seconds = kFirstRetryBackoffSeconds;

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

        // #899: a camera answering again is not permission to score. While the board is
        // suspended it reads frames and does nothing with them -- it does not process
        // them, it does not send a result, and it does not count the cycle towards
        // READY. The frames are read anyway because reading is how the recovery finds
        // out the cameras are back, and because a slot that stops being read stops
        // reporting CAPDROP for the camera that is still missing.
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
            const GeometryReview::Verdict verdict = attemptRecovery(++recovery_attempts, blind_for);

            if (verdict == GeometryReview::Verdict::Unchanged)
            {
                scoring_suspended = false;
                last_sight = chrono::steady_clock::now();
            }
            else if (verdict == GeometryReview::Verdict::Moved)
            {
                running = false;
                break;
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
GeometryReview::Verdict Scorer::attemptRecovery(int attempt, long blind_seconds)
{
    log_warning("BOARD SIGHT RECOVERY: attempt " + to_string(attempt) + " after " +
                to_string(blind_seconds) + " seconds blind - reopening " +
                to_string(camera_sources.size()) + " camera(s)");

    if (!capture->open(camera_sources, width, height, fps))
    {
        log_warning("BOARD SIGHT RECOVERY: the cameras still will not open. Scoring stays "
                    "suspended and this will be tried again.");
        return GeometryReview::Verdict::Unreadable;
    }

    vector<camera::Frame> review_frames = capture->readAveraged(kReviewFrames);
    if (camera::validCount(review_frames) == 0)
    {
        log_warning("BOARD SIGHT RECOVERY: the cameras opened and produced no frame. Scoring "
                    "stays suspended and this will be tried again.");
        return GeometryReview::Verdict::Unreadable;
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
        return GeometryReview::Verdict::Unchanged;

    case GeometryReview::Verdict::Moved:
        // Terminal, and terminal on purpose. Nothing a retry can do makes a moved camera
        // un-moved, and the one thing that must not happen is this board scoring again
        // on geometry that has been contradicted.
        board_sight::recordFault("the cameras came back and the board is not where it was - " +
                                 review.account);
        board_sight::faulted() = true;
        log_error("BOARD MOVED: " + review.account +
                  ". The cameras answer and the geometry this board was scoring with is no "
                  "longer true of them, so it refuses to score rather than put darts in the "
                  "wrong wedge. Put the camera back where it was, or restart the detector so "
                  "it calibrates on the rig as it is now.");
        // #895's argument one layer down: a board that cannot see must not go on serving
        // a score socket that looks alive. main's mDNS announcement is not withdrawn from
        // here -- that is #1274's surface and it is decided before run() is called.
        if (websocket_service_)
        {
            websocket_service_->stop();
        }
        return GeometryReview::Verdict::Moved;

    default:
        log_warning("BOARD SIGHT RECOVERY: " + review.account +
                    ". Scoring stays suspended and this will be tried again.");
        return GeometryReview::Verdict::Unreadable;
    }
}
