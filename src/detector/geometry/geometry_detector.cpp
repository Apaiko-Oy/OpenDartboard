#include <iostream>
#include <algorithm>
#include <cstdlib>
#include <ctime>

#include "geometry_detector.hpp"
#include "camera_quorum.hpp"
#include "calibration/board_look.hpp"
#include "calibration/geometry_calibration.hpp"
#include "detection/dart_processing.hpp"
#include "detection/score_processing.hpp"
#include "utils.hpp"
#include "utils/board_sight.hpp"

using namespace cv;
using namespace std;

// Constructor
GeometryDetector::GeometryDetector(bool debug_mode, int target_width, int target_height, int target_fps)
    : initialized(false), calibrated(false), debug_mode(debug_mode), target_width(target_width), target_height(target_height), target_fps(target_fps)
{
}

// Main process method - simplified to basic structure
DetectorResult GeometryDetector::process(const vector<camera::Frame> &frames)
{
    // The three vision stages read a camera's position in this vector as its identity, so
    // the images are handed down in their own slots; a camera that did not produce a frame
    // leaves an empty Mat where its image would be.
    const vector<Mat> images = camera::images(frames);

#ifdef DEBUG_VIA_VIDEO_INPUT
    if (!images.empty() && raw_streamer)
    {
        Mat combined_raw = debug::createCombinedFrame(images, "RAW");
        raw_streamer->push(combined_raw);
    }
#endif

    DetectorResult result;

    if (!calibrated || camera::validCount(frames) == 0)
    {
        return result;
    }

    // Process motion session - all motion logic is now handled in motion_processing.
    // #1339: motion is a fraction of the board rather than of the frame, and this is
    // where the board reaches it. Calibration fitted the outer edge of the double ring
    // per camera already; it is read out of `calibrations` in the same slot the camera's
    // image is in, so a camera that never fitted a board hands down `known` false and
    // abstains from the figure rather than being measured against a frame it barely
    // fills. Built once, because a calibration does not change under a running board.
    if (board_extents.size() != calibrations.size())
    {
        board_extents.assign(calibrations.size(), motion_processing::BoardExtent());
        for (size_t i = 0; i < calibrations.size(); i++)
        {
            board_extents[i].known = calibrations[i].sees_board && calibrations[i].ellipses.hasValidDoubles;
            board_extents[i].edge = calibrations[i].ellipses.outerDoubleEllipse;
        }
    }
    motion_processing::MotionResult motion_result = motion_processing::processMotion(images, background_frames, board_extents, debug_mode);

    // Process dart state detection
    // #1345: the same boards, for the same reason, one stage on. Until #1345 this stage
    // decided whether a camera had seen a dart from a percentage of the whole frame,
    // which is the denominator #1339 took out of the stage above it.
    dart_processing::DartStateResult dart_result = dart_processing::processDartState(images, background_frames, board_extents, motion_result.motion_finished, debug_mode);

    // Process scoring using the new scoring system
    score_processing::ScoreResult score_result = score_processing::processScore(background_frames, dart_result, calibrations, debug_mode);

    // Only return result if scoring system says it's valid (state changed)
    if (score_result.valid)
    {
        result.dart_detected = true;
        result.score = score_result.score;
        result.position = score_result.pixel_position;
        result.confidence = score_result.confidence;
        result.camera_index = score_result.camera_index;
        // #1186: the board-frame fields ride beside the pixels they were derived with.
        result.ring = score_result.ring;
        result.segment = score_result.segment;
        result.board_radius_known = score_result.board.has_radius;
        result.board_angle_known = score_result.board.has_angle;
        result.board_radius = score_result.board.radius;
        result.board_angle = score_result.board.angle;
        // The instant the frames behind this score were acquired, from the backend.
        result.timestamp = camera::newestInstantUs(frames);
    }

    return result;
}

// Main initialization method
/**
 * #1363: the operator's orientation anchors, applied to whatever calibration this start
 * holds -- measured fresh or read from the cache, which is why this is a function called
 * on both paths rather than a block inside one of them, and applied AFTER the cache is
 * written, so the file keeps pure measurement and the statement lives in configuration.
 *
 * OD_CAMERA_WEDGES is a comma list, one entry per camera in camera order: the wedge
 * NUMBER at the bottom of that camera's image, read off the setup view once; 0 or blank
 * for a camera the operator does not anchor ("9,0,3" anchors cameras 1 and 3). It
 * exists because the clip-wire heuristic's premise is a board's four visible mounting
 * clips, and on the maintainer's Winmau Blade 6 over a black surround the finder sees
 * ONE clip where both classification branches demand exactly four -- so no camera could
 * anchor, and every dart of the first live scoring run published as the asserted 20.
 * The cameras are fixed to the rig's frame (ADR-0079), so the anchor is a fact an
 * operator can state once and the board can hold.
 *
 * A measured anchor wins over a stated one, a wedge no board carries is refused by
 * name, and a camera with no south wire to index from keeps no anchor at all.
 */
void GeometryDetector::applyConfiguredAnchors()
{
    const char *env = getenv("OD_CAMERA_WEDGES");
    if (!env || !*env)
    {
        return;
    }
    const string spec(env);
    for (size_t i = 0; i < calibrations.size(); i++)
    {
        const int wedge = orientation_processing::configuredSouthWedge(spec, (int)i);
        if (wedge == 0)
        {
            continue;
        }
        if (wedge < 0)
        {
            log_error("ORIENTATION: the OD_CAMERA_WEDGES entry for camera " + to_string(i + 1) +
                      " is not a number a dartboard carries; that camera stays unanchored");
            continue;
        }
        DartboardCalibration &calibration = calibrations[i];
        if (calibration.orientation.anchored)
        {
            log_info("ORIENTATION: camera " + to_string(i + 1) + " is already anchored by its own "
                     "measurement; the configured wedge " + to_string(wedge) + " is not applied");
            continue;
        }
        const int wires = (int)calibration.wires.wireEndpoints.size();
        const int index = orientation_processing::wedge20WireFromSouthWedge(
            calibration.orientation.southWireIndex, wedge, wires);
        if (index < 0)
        {
            log_warning("ORIENTATION: camera " + to_string(i + 1) + " has no south wire to anchor "
                        "the configured wedge " + to_string(wedge) + " against");
            continue;
        }
        calibration.orientation.wedge20WireIndex = index;
        calibration.orientation.wedgeNumber = wedge;
        calibration.orientation.cameraPosition = orientation_processing::CameraPosition::CONFIGURED;
        calibration.orientation.anchored = true;
        log_info("ORIENTATION: camera " + to_string(i + 1) + " anchored by configuration: wedge " +
                 to_string(wedge) + " at its image south, so the 20 is wire index " + to_string(index) +
                 " of " + to_string(wires));
    }
}

bool GeometryDetector::initialize(const vector<camera::Frame> &calibration_frames, double capture_fps)
{
#ifdef DEBUG_VIA_VIDEO_INPUT
    // #812: the raw camera feed. It was behind the build define alone, so a dev
    // build opened it with no --debug on the command line. Both, now.
    if (debug_mode)
        raw_streamer = make_unique<streamer>(8081, capture_fps);
    cv::Mat startup_img_raw(target_height, target_width, CV_8UC3, cv::Scalar::all(0));
    cv::putText(startup_img_raw, "Raw Cameras", {50, 100}, cv::FONT_HERSHEY_SIMPLEX, 1.2, {0, 255, 0}, 2);
#endif

    vector<Mat> initial_frames = camera::images(calibration_frames);

    // #1330: the call is here rather than commented out, and what it does is decided by
    // --reuse-calibration. The line above it had been a comment since before #1317, so the
    // board wrote a file on every successful calibration and read it never: it paid the
    // eight and a half seconds on every start and got nothing for the file, and the latent
    // std::string in the file was latent only because of a comment character.
    //
    // It can be read now because utils/cache.hpp's header and the two static_asserts under
    // DartboardCalibration are between it and the pointer that used to be in it. It is not
    // read by DEFAULT because the file cannot say whose geometry it is -- measured in
    // cache.hpp, five starts of testers/i1318_run.sh in one directory, four of them
    // scoring through the first one's board. A camera nudged since the file was written is
    // a calibration that is wrong and looks right, which is the class of fault ADR-0055
    // says must not be able to look trustworthy, and nothing here can see it. So when an
    // operator does ask, the board says on that start that it did not look at the picture
    // and how old the geometry it is scoring with is.
    calibrations = cache::geometry::load(initial_frames);
    if (!calibrations.empty())
    {
        uint64_t written = 0;
        for (const auto &calibration : calibrations)
            written = max(written, calibration.timestamp);
        const uint64_t now = (uint64_t)time(nullptr);
        const long long age_minutes = written > 0 && now > written ? (long long)((now - written) / 60) : -1;

        log_info("Using the cached calibration for " + to_string(calibrations.size()) +
                 " cameras: this start did not look at the board" +
                 (age_minutes >= 0 ? ", and the geometry it is scoring with was measured " +
                                         to_string(age_minutes) + " minutes ago"
                                   : "") +
                 ". Delete cache/ to calibrate again.");

        // Load background frames
        background_frames = cache::geometry::loadBackgroundFrames();

        // #1363: the operator's anchors apply to a cached calibration exactly as to a
        // fresh one -- the cache holds measurement, the statement lives in configuration.
        applyConfiguredAnchors();

        // set initialized and calibrated
        initialized = true;
        calibrated = true;
        return true;
    }

    if (camera::validCount(calibration_frames) > 0)
    {
        log_info("Performing immediate calibration...");

        calibrations = geometry_calibration::calibrateMultipleCameras(
            initial_frames,
            debug_mode,
            target_width,
            target_height);

        // #1318, standing on 74be46f rather than reverting it. That commit replaced
        // `calibrated = !calibrations.empty()` -- one object per non-empty frame counted
        // as success -- with "one calibration per camera that produced a frame, and
        // `hasValidDoubles` on every one". The half of it that matters is kept whole and
        // moved to where the evidence is: `hasValidDoubles` is now one of the things
        // board_look asks of each camera, a camera that fails it is refused BY NAME at
        // the moment it is looked at, and a refused camera abstains from scoring for the
        // life of the run. So a board can no longer calibrate on nothing usable.
        //
        // What is deliberately different is the quantifier. `all_of` over every camera
        // is what made the rig in #1318 report `BOARD FAULTED: this board is running and
        // cannot see` while two of its three cameras were pointed at the dartboard and
        // had calibrated cleanly at 68 and 103 boundary points -- true, and about the
        // webcam, and unsayable from the message. One camera that cannot see is now one
        // camera that is named and set aside.
        //
        // Two smaller notes on what is not carried over. `calibrations.size() ==
        // validCount(...)` cannot be asked any more and does not need to be: a slot is
        // kept for every camera including the ones that produced no frame, precisely so
        // that score_processing's calibrations[i] stays the camera at position i, and a
        // slot with no frame is a camera that sees nothing and is counted as such.
        // And `wires.wireEndpoints.size() >= 16` was dropped rather than moved, because
        // it was `std::array<Point2f, 20>` and therefore the constant 20 and always true.
        // #1317 has since given that count a real value and put the question where the
        // evidence is, the way #1318 did with hasValidDoubles: calibrateSingleCamera
        // refuses a camera whose wire stage found fewer than
        // wire_processing::kWiresRequired boundaries, by name and with the count, and a
        // refused camera does not set sees_board. So it is asked here too now, through
        // the same `seeing` count as everything else, and there is still nothing in this
        // function that needs to know what a wire is.
        //
        // #1338 STANDS ON BOTH AND CHANGES NEITHER QUANTIFIER. What it adds is the
        // question neither commit asked: whether enough cameras ANSWERED. #1318 dropped
        // `calibrations.size() == validCount(...)` for the right reason -- a slot is kept
        // for every camera -- but the count of cameras that produced a frame went with
        // it, and nothing else in the program was asking. On the maintainer's rig on
        // 2026-09-18 two of three cameras delivered no frames (#1319), camera 1 calibrated
        // cleanly, and this gate said yes: `1 of 3`, `Scorer running with 3 cameras`, and
        // a READY beat to Turnaus from a board in which no dart could ever be scored.
        //
        // The two questions are about different things and are both asked, separately:
        //
        //   SEEING   how many cameras have geometry to score a tip against. One is
        //            enough -- #1318's `cameras_that_must_see`, unchanged and deliberate.
        //            A camera with no dartboard in its picture abstains for the life of
        //            the run and the others carry on without it.
        //
        //   ANSWERING how many cameras produced a frame at all. This is not a count of
        //            objects and not a second opinion about sight: it is a PREREQUISITE
        //            for the count below it. A silent camera's background is empty for
        //            the whole run, so it can never spike and never vote.
        //
        //   VOTING   how many cameras have BOTH -- a frame and a fitted board. #1348:
        //            this is the population both quorums in the detection chain are
        //            really about, and until it was counted here neither was measured
        //            against it. Since #1339 a camera with no fitted board abstains from
        //            the motion figure, so this is the ceiling on `cameras_that_spiked`
        //            against `min_cameras_for_event`; since #1354 it abstains from the
        //            dart-state vote, so it is also the ceiling on `moves_up` and
        //            `goes_clean` against the quorum in dart_processing.hpp. Each
        //            threshold is asked in the file its own constant lives in.
        //
        // THE SENTENCE THAT USED TO BE HERE said a partial calibration may still score
        // because "three answering cameras of which one sees the board can form an event
        // on the three and score it on the one". #1339 made that false -- a camera with
        // no board has no scale to take a ratio against and abstains, so the event can
        // only ever be formed on the ONE -- and #1353, by moving `min_cameras_for_event`
        // to 1, made the correction cost nothing: that board still calibrates, on
        // arithmetic that is now true.
        //
        // What #1353 also did was move the binding arithmetic one stage on with nothing
        // asking it. A board with one voting camera forms events happily and cannot move
        // its own state -- the vote takes 2 -- so it holds CLEAN for ever and cannot see
        // a takeout either, and it passed this gate and beat READY. That is #1348's
        // title, and `whyNoStateChangeIsPossible` is it asked out loud. It is asked HERE,
        // beside the other one, because both are claims about the board for the life of
        // the run; the per-window case, where a camera stops contributing frames
        // mid-round, is accounted for in that window's own STATE VOTE line.
        //
        // So a partial calibration MAY still score and that is still the decision -- what
        // it may not do is claim health it has not got, and this is where that claim is
        // made: `initialize` returning false is what sets `board_sight::faulted()` in
        // Scorer's constructor, which is what makes the beat ERROR rather than READY.
        //
        // NOTE for a reviewer: with the vote's floor at 2, `cameras_that_must_see = 1` no
        // longer decides anything by itself -- a board with one voting camera is refused
        // by the vote's arithmetic before this constant is reached. #1338's author
        // flagged that constant as the thing somebody might want to reverse; #1348
        // reverses its EFFECT without touching it, and the place to argue with that is
        // the floor in DartParams, where the reason is written.
        //
        // #1389: and that is what this constant now says. It is `camera_quorum::cameras()`
        // -- two -- read here, by the dart event's census in motion_processing and by the
        // state vote's floor in dart_processing, so the three cannot drift apart again.
        // The value moved from 1 to 2 and nothing about which boards are admitted moved
        // with it: `voting` is counted inside `sees_board`, so voting >= 2 implies
        // seeing >= 2, and every board this line now refuses was already refused by the
        // vote's arithmetic below. That is ADR-0081's "this makes the admission gate say
        // what the rest of the code already enforces", and it is why the change is safe
        // to make and worth making -- the gate that decides is the gate that says so.
        const int cameras_that_must_see = camera_quorum::cameras();
        int seeing = 0;
        int voting = 0;
        // #1389 / ADR-0081 §3: why each camera cannot vote, in that camera's own slot,
        // empty where it can. The words are board_look's (#1318, and #1392 after it) and
        // are not retyped: `NoFrame` sends somebody to the USB bus and to #1319, and a
        // refused ring sends them to the aim and the lighting. "A message saying only two
        // of three has told nobody anything."
        vector<string> why_each_camera;
        for (const auto &calibration : calibrations)
        {
            const bool can_vote = calibration.sees_board && calibration.ellipses.hasValidDoubles;
            if (calibration.sees_board)
            {
                seeing++;
                // The same two questions BoardExtent::known is built from in process().
                if (calibration.ellipses.hasValidDoubles)
                {
                    voting++;
                }
            }
            string reason = board_look::refusal(calibration.look);
            if (reason.empty() && !can_vote)
            {
                // board_look admitted this camera and the ellipse stage did not fit its
                // doubles ring, so it has no scale to measure a ratio against and it
                // abstains from both quorums (#1339, #1354). Saying nothing here would
                // leave a camera named in the count and absent from the reasons.
                reason = "is looking at the dartboard but its doubles ring was not fitted, "
                         "so it has nothing to measure a dart against and abstains";
            }
            why_each_camera.push_back(reason);
        }

        const int camera_slots = (int)calibrations.size();
        const int answering = (int)camera::validCount(calibration_frames);
        const string no_event_possible = motion_processing::whyNoEventIsPossible(camera_slots, voting);
        // #1348's falsification: under OD_STATE_QUORUM=absolute this gate is not asked at
        // all, which is what it was before this issue, so the board that cannot move its
        // own state calibrates and beats READY again on this same binary.
        const string no_state_change_possible =
            dart_processing::stateQuorumIsAbsolute()
                ? string()
                : dart_processing::whyNoStateChangeIsPossible(camera_slots, voting);

        const string too_few_see =
            camera_quorum::whyTooFewCamerasSee(camera_slots, seeing, cameras_that_must_see);
        calibrated = too_few_see.empty() && no_event_possible.empty() &&
                     no_state_change_possible.empty();

        // #1338: said once, by the thing that decided it, so that Scorer has a census to
        // repeat rather than a camera count of its own to disagree with.
        scoring_with = to_string(seeing) + " of " + to_string(camera_slots) + " cameras" +
                       (answering < camera_slots
                            ? " (" + to_string(camera_slots - answering) + " produced no frame)"
                            : (seeing < camera_slots
                                   ? " (" + to_string(camera_slots - seeing) + " not looking at the dartboard)"
                                   : ""));

        if (calibrated)
        {
            // Save frames as background (for dart detection)
            background_frames.clear();
            for (const auto &frame : initial_frames)
                background_frames.push_back(frame.clone());

            log_info("Initial calibration completed successfully on " + to_string(seeing) +
                     " of " + to_string((int)calibrations.size()) + " cameras");

            // #1338: a board that is scoring with fewer cameras than it has says so once,
            // here, where the number was decided. It is a WARN rather than an ERROR
            // because this board really can score -- see the gate above -- and it is not
            // silence because "two of your three cameras are not contributing" is the
            // sentence that gets a cable looked at before the evening rather than after.
            //
            // #1348: both thresholds, each against the population it is really measured
            // against -- the cameras that have a frame AND a board. The line used to name
            // one threshold against the ANSWERING count, which on a board with a silent
            // camera and a blind one was a ratio between two different populations, and
            // it said nothing at all about the vote, which is the arithmetic that stops a
            // degraded board scoring first.
            if (seeing < camera_slots || answering < camera_slots)
            {
                log_warning("Scoring on " + scoring_with + ", not on all " + to_string(camera_slots) +
                            "; " + to_string(voting) + " of them have both a frame and a fitted board, "
                            "a dart event needs a spike on at least " +
                            to_string(motion_processing::MotionParams().min_cameras_for_event) +
                            " of those and moving the board takes " +
                            to_string(dart_processing::stateVoteQuorum(voting)) + " of them");
            }

            // Save calibration for future use
            if (cache::geometry::save(calibrations))
            {
                log_debug("Saved calibration");
            }

            // Save background frames for dart detection
            if (cache::geometry::saveBackgroundFrames(background_frames))
            {
                log_debug("Saved background frames");
            }

            // #1363: after the save, so the cache keeps pure measurement and every
            // start re-applies the operator's statement from configuration.
            applyConfiguredAnchors();

            initialized = true;
            calibrated = true;
        }
        else if (seeing == 0)
        {
            // #1389: `seeing == 0` rather than `seeing < cameras_that_must_see`, and the
            // change is forced rather than cosmetic. With the floor at two, one camera
            // looking at the dartboard would have been refused HERE, by a sentence saying
            // "none of the 3 cameras is looking at a dartboard" -- which would be a false
            // statement about a board with one good camera. This branch keeps the case it
            // was written for; a board that has SOME sight and not enough of it is
            // refused below, where the shortfall can be counted and every camera named.
            // #1318: not "the calibration failed" any more -- every camera was
            // calibrated and every one of them was refused, each on its own line above.
            const string none_named = camera_quorum::namingEachCamera(why_each_camera);
            log_error("Initial calibration failed: none of the " + to_string((int)calibrations.size()) +
                      " cameras is looking at a dartboard. " + none_named);
            // #1321's sentence, in case the per-camera refusals above recorded nothing
            // -- they will have, unless every camera produced no frame at all. #1389
            // carries the reasons into the fault detail too, so the vigil's BOARD FAULTED
            // line says which camera failed on what rather than only how many did.
            board_sight::recordFault("none of the " + to_string((int)calibrations.size()) +
                                     " cameras is looking at a dartboard. " + none_named);
            initialized = false;
            calibrated = false;
        }
        else
        {
            // #1338: the cameras that answered are looking at the dartboard and there are
            // not enough of them for a dart event to be formed at all. This is the state
            // the issue was filed about, and the difference from the branch above is the
            // whole point of the two being separate branches: nothing is wrong with the
            // aim or the lighting, and telling somebody to go and look at where a camera
            // is pointed would send them to the wrong end of the room. The remedy is the
            // cable, the hub or the bandwidth.
            // #1348: two arithmetics, one branch, and the first non-empty one is the
            // reason. A board short of the event quorum is short of the vote's too --
            // they are counted off the same population -- so ordering them is a choice
            // about which sentence reads first, and the earlier stage's does.
            // #1348: two arithmetics, one branch, and the first non-empty one is the
            // reason. #1389 added a third and reordered them, and the reorder has a
            // reason rather than a preference. Before #1389 the two thresholds were
            // different numbers (1 and 2), so on a one-camera board only the vote's
            // sentence fired and the order never arose. They are now ONE number against
            // ONE population, so on every board short of the floor all of them fire at
            // once -- and then the choice is purely which sentence tells the reader more.
            // The vote's names both consequences, a dart that cannot be called AND a
            // takeout that cannot be seen; the motion stage's names one. #1348's rule was
            // "the earlier stage's reads first" and its own reason was that the stages
            // were different tests; they are not any more.
            //
            // `no_event_possible` still wins where it is the three-slot fact, because on
            // a board that is not three slots the vote's sentence is empty.
            const string &why = !no_state_change_possible.empty() ? no_state_change_possible
                                : !no_event_possible.empty()      ? no_event_possible
                                                                  : too_few_see;
            // ADR-0081 §3: the count is the less useful half. Every camera is named with
            // its own board_look reason on the same line, so the refusal sends somebody
            // to the USB bus or to the aim rather than to a ratio.
            const string named = camera_quorum::namingEachCamera(why_each_camera);
            log_error("Initial calibration failed: " + why + ". " + named);
            board_sight::recordFault(why + ". " + named);
            initialized = false;
            calibrated = false;
        }
    }
    else
    {
        log_error("No initial frames captured for calibration");
        board_sight::recordFault("no camera produced a frame to calibrate on");
        initialized = false;
        calibrated = false;
    }

    return initialized;
}

/**
 * #899: the board is asked whether it is still looking at what it calibrated on.
 *
 * Called by Scorer after a sight loss has been survived and the cameras have been
 * reopened, on a fresh average of frames. What comes back is a verdict and the numbers it
 * was reached by; `calibrations` is not touched, and the board goes on scoring with the
 * geometry it earned at start or it stops scoring at all.
 *
 * WHICH CAMERAS ARE ASKED. Only the ones that were scoring: #1318 lets a camera that is
 * not looking at a dartboard abstain for the life of the run, and a slot that abstained
 * has no held geometry to compare a fresh picture against. A camera that has not come
 * back yet -- an empty frame in its slot -- abstains here too, because "this camera is
 * still missing" is a fact about the recovery and not about whether the board moved.
 *
 * WHAT IT TAKES TO SAY `Unchanged`. One camera that was scoring, is back, still sees a
 * dartboard, and agrees. That is #1318's `cameras_that_must_see = 1` read forward rather
 * than a second, stricter quorum invented here: a board that can score on one camera can
 * be vouched for by one camera. The refusal is the other quantifier on purpose -- ANY
 * camera that disagrees is a `Moved`, and it returns on the first one, because one
 * camera that has been shifted is enough to put a dart in the wrong wedge.
 */
GeometryReview GeometryDetector::reviewGeometry(const vector<camera::Frame> &frames)
{
    const vector<Mat> images = camera::images(frames);
    const geometry_agreement::Limits limits;

    int witnesses = 0;
    int still_missing = 0;
    int no_longer_sees = 0;
    string last_account;

    for (size_t i = 0; i < calibrations.size(); i++)
    {
        if (!calibrations[i].sees_board)
        {
            // #1318: this camera was not scoring, so there is nothing it can vouch for.
            continue;
        }
        if (i >= images.size() || images[i].empty())
        {
            still_missing++;
            continue;
        }

        DartboardCalibration fresh = geometry_calibration::calibrateSingleCamera(images[i], (int)i, false);
        if (!fresh.sees_board)
        {
            // The camera is answering and there is no dartboard in the picture. That is
            // not agreement and it is not a measured move either; it is a camera that
            // cannot be used as a witness, and it is said by name because a lens that
            // somebody has turned to face the room looks exactly like this.
            no_longer_sees++;
            log_warning("GEOMETRY REVIEW: camera " + to_string(i + 1) +
                        " is answering but no longer sees a dartboard - " +
                        board_look::refusal(fresh.look));
            continue;
        }

        const geometry_agreement::Movement movement =
            geometry_agreement::measure(calibrations[i], fresh);
        const string account = geometry_agreement::account((int)i, movement, limits);

        if (geometry_agreement::hasMoved(movement, limits))
        {
            log_error("GEOMETRY REVIEW: " + account);
            return GeometryReview{GeometryReview::Verdict::Moved, account};
        }

        log_info("GEOMETRY REVIEW: " + account);
        witnesses++;
        last_account = account;
    }

    if (witnesses == 0)
    {
        return GeometryReview{GeometryReview::Verdict::Unreadable,
                              "no camera that was scoring can vouch for the board yet (" +
                                  to_string(still_missing) + " still not answering, " +
                                  to_string(no_longer_sees) + " answering without a dartboard in the picture)"};
    }

    return GeometryReview{GeometryReview::Verdict::Unchanged,
                          to_string(witnesses) + " of " + to_string((int)calibrations.size()) +
                              " cameras vouch for the board being where it was, the last of them " +
                              last_account};
}
