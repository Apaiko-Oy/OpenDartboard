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
#include "calibration/geometry_agreement.hpp"

using namespace cv;
using namespace std;

// ---- #1445: the board looks more than once, and the two numbers that say how often ----
namespace
{
    // MEASURED, on both fixtures, by testers/phases1445/1445-looks.sh. See the table
    // below; the constants are set from it and the harness re-derives it on every run, so
    // a re-shot fixture or a moved constant fails here rather than in a pub. This is
    // #1388's shape and it is deliberately the same shape: a budget nobody measured is the
    // one thing this slice must not produce.
    //
    // WHAT A LOOK IS. One frame, read from the camera, calibrated with the same
    // `calibrateSingleCamera` the first pass used. NOT another thirty-frame average, and
    // the measurement is why rather than a preference -- see the table. The averaged frame
    // is not a better picture of the board, it is a picture of thirty pictures, and a
    // bright edge present in any of them survives the mean with a thirtieth of its
    // contrast while a wire boundary that moved between them is smeared. That is a reading
    // no single frame gave, and it is the reading this issue is about.
    //
    // WHAT WAS MEASURED, per clip: the run of CONSECUTIVE looks that do not read a whole
    // ring, which is how long a board looking repeatedly would go on being refused by a
    // camera that can be calibrated. It is the direct analogue of #1388's longest run of
    // consecutive disagreeing samples, and it answers the same question -- how long a
    // disturbance this budget has to outlast.
    //
    // MEASURED 2026-09-20 on the 4-core box at load 1.9-3.7, with the integration sweep
    // finished and nothing else running, by testers/phases1445/1445-looks.sh: the averaged
    // frame each camera really calibrates on -- composed the way readAveraged(30) composes
    // one, from the seek DEBUG_SEEK_VIDEO puts that camera's slot at -- and then eighty
    // consecutive single frames after it.
    //
    //                          averaged   single frames    longest run of consecutive
    //                             frame   reading twenty   refused looks, 5 cycles apart
    //     mocks/cam_1                20          55 of 80   2
    //     mocks/cam_2                20          55 of 80   3
    //     mocks/cam_3                20          72 of 80   1
    //     rig-20260918/cam_1         20          29 of 80   4
    //     rig-20260918/cam_2         20          80 of 80   0
    //     rig-20260918/cam_3         21 REFUSED  42 of 80   5   <-- the maximum
    //
    // THE FIRST ROW OF THAT LAST LINE IS THE WHOLE ISSUE, and it reproduces on a quiet box:
    // #1442's twenty-one was measured at load 12-15 and flagged as possibly a figure the
    // load produced. It is not. For a FILE source `read()` takes the next frame and no
    // clock is consulted, so readAveraged(30) over a mock is the mean of thirty consecutive
    // frames and which thirty is decided by the seek alone -- the number is arithmetic, and
    // it came back 21 at load 1.9. That camera then reads exactly twenty on 42 of the 80
    // single frames composing and following that average. The average really is the worse
    // picture.
    //
    // WHY FIVE CYCLES APART rather than adjacent. Adjacent frames are not independent
    // readings, and the measurement says so: at a spacing of one the longest run of
    // consecutive refused looks is 18 (mocks/cam_2), at three it is 7, at five it is 5 and
    // at ten it is 3. Five is where the run stops falling steeply, and spending twelve
    // looks on twelve adjacent frames would measure very little more than one look.
    //
    // WHY TWELVE. Five is the longest run any camera of either fixture produced at this
    // spacing, so twelve outlasts it 2.4 times over -- the same margin #1388 gave itself
    // (an 11-to-12 s span against a 6.00 s disturbance) and for the same reason: five is
    // the longest run seen in this footage, not the longest run there is. The whole budget
    // spans 12 x 5 = 60 frames, which is 2.0 s at the mocks' 30 fps and 4.0 s at the 15 fps
    // a rig is more likely to run at.
    //
    // ONE CAMERA OF SIX IS IN THE POPULATION, and that is worth saying rather than hiding:
    // the budget's maximum and the issue's subject are the same camera. The other five rows
    // are what stops it being a constant chosen for one clip -- rig/cam_1 reads nineteen on
    // half its frames and would have wanted four looks had its average been refused, which
    // is the second-longest run and is measured on a camera this retry never touches.
    //
    // THE COST OF BEING WRONG IS NOT SYMMETRIC, and the margin goes the same way #1388's
    // does. A look is one frame read and one calibration, and it is spent ONLY on a camera
    // that has already been refused -- a board whose cameras all calibrated on the
    // averaged frame does not read a single extra frame and does not reach this code. An
    // over-long budget therefore costs start-up seconds on a board that is already in
    // trouble; an over-short one sets a healthy camera aside for the evening, which is the
    // fault this issue was filed on.
    constexpr int kFurtherLooks = 12;

    // How many capture cycles pass between one look and the next. Consecutive frames are
    // not independent readings -- whatever the camera is reading that a board does not
    // have is usually still there a frame later -- so spending the budget on twelve
    // adjacent frames measures very little more than one. This is the spacing the census
    // below was taken at.
    constexpr int kFramesBetweenLooks = 5;

    /**
     * OD_CALIBRATION_LOOKS=once restores the board this slice was filed on: one averaged
     * frame per camera and nothing after it, which is what every commit before #1445 did.
     * So the population this issue is about can be counted twice on ONE binary and the
     * difference is this function and nothing else (#1340).
     *
     * Anything but that exact word is ignored, so a typo looks again rather than silently
     * reading in the behaviour the issue was filed on. There is deliberately no word for
     * the reverse: looking again is what this stage now IS, not a mode it is in.
     */
    bool theBoardLooksOnlyOnce()
    {
        static bool v = []
        {
            const char *e = std::getenv("OD_CALIBRATION_LOOKS");
            return e && std::string(e) == "once";
        }();
        return v;
    }
}

void GeometryDetector::lookAgainAtRefusedCameras()
{
    // ADR-0080 §2, checked rather than asserted. `sealed_geometry` is taken at the end of
    // `initialize` and never written again, so a non-empty seal here means this board has
    // already decided what its geometry is -- and changing it afterwards is the adoption
    // #899 refused and #1388 built a refusal around. This branch can only be reached by
    // somebody moving the call, which is exactly the mistake worth making impossible.
    if (!sealed_geometry.empty())
    {
        log_error("LOOK AGAIN refused: this board has already sealed its geometry, and a "
                  "calibration taken now would be adopted mid-run (ADR-0080 §2)");
        return;
    }

    // Which cameras have an EMPTY slot, and which of those are worth another picture.
    //
    // A camera that produced no frame is not a camera that was looked at and refused, and
    // it is left alone here on #1338's distinction rather than by oversight. Nothing about
    // its aim or its lighting has been concluded, because nothing was seen; what is wrong
    // is a cable, a hub or the bandwidth it shares (#1319), and reading the same silence
    // twelve more times says nothing the first silence did not. #1372 wrote down that a
    // camera which comes back during the evening is #899's review's errand and not
    // `initialize`'s, and that is still true.
    vector<size_t> still_refused;
    for (size_t i = 0; i < calibrations.size(); i++)
    {
        if (calibrations[i].sees_board)
        {
            continue;
        }
        if (board_look::verdict(calibrations[i].look) == board_look::Refused::NoFrame)
        {
            continue;
        }
        still_refused.push_back(i);
    }
    if (still_refused.empty())
    {
        return;
    }

    if (theBoardLooksOnlyOnce())
    {
        log_warning("OD_CALIBRATION_LOOKS=once is set: the " + to_string((int)still_refused.size()) +
                    " camera(s) refused on this start's averaged frame are not looked at again, "
                    "as they were not before #1445");
        return;
    }
    if (!further_look)
    {
        // Nobody offered. A detector calibrating from a still, or from a test's fixture,
        // has no second picture to be handed and says so once rather than looking like a
        // budget that was spent.
        log_info("LOOK AGAIN: " + to_string((int)still_refused.size()) +
                 " camera(s) were refused on this start's averaged frame and nothing offered "
                 "this detector another look, so the first frame is the only evidence there is");
        return;
    }

    // #1445: a look is not a fault, and the fault record is where that has to be said.
    //
    // `board_sight::recordFault` is first-fault-wins and `calibrateSingleCamera` calls it
    // on every refusal it reaches. So without this, a camera refused on look 1 and
    // calibrated on look 2 would leave BOARD FAULTED holding a sentence about a camera
    // that is scoring -- and, worse, would hold the slot against the real fault that comes
    // later, which on this board is #1388's `Moved`. What the first pass recorded is kept
    // whole; what the looks record is dropped, and the gate below records the board's real
    // refusal, with every camera named (#1389), if there is one.
    const string fault_before_looking = board_sight::faultDetail();

    string names;
    for (size_t i : still_refused)
    {
        names += (names.empty() ? "" : ", ") + to_string((int)i + 1);
    }
    log_info("LOOK AGAIN: camera(s) " + names + " were refused on this start's averaged frame, "
             "which is one picture and not an evening. Up to " + to_string(kFurtherLooks) +
             " further looks, " + to_string(kFramesBetweenLooks) +
             " capture cycles apart, before any of them is set aside for the run");

    int looks_spent = 0;
    for (int look = 1; look <= kFurtherLooks && !still_refused.empty(); look++)
    {
        vector<camera::Frame> frames;
        for (int cycle = 0; cycle < kFramesBetweenLooks; cycle++)
        {
            frames = further_look();
        }
        looks_spent = look;

        const vector<Mat> images = camera::images(frames);
        vector<size_t> carried;
        for (size_t i : still_refused)
        {
            if (i >= images.size() || images[i].empty())
            {
                // The camera has gone quiet between the average and now. That is not a
                // refusal and it is not agreement either; it is a look that did not
                // happen, and it costs the budget nothing to say so.
                carried.push_back(i);
                continue;
            }

            DartboardCalibration fresh = geometry_calibration::calibrateSingleCamera(images[i], (int)i, false);
            if (!fresh.sees_board)
            {
                carried.push_back(i);
                continue;
            }

            // The only write in this function, and it is into a slot that held nothing a
            // board can be scored through: a refused calibration returns before the
            // perspective fit and before orientation, so there is no measurement here to
            // overwrite. This is a FIRST calibration for this camera, arriving late.
            calibrations[i] = fresh;
            log_info("LOOK AGAIN: camera " + to_string((int)i + 1) + " calibrated on look " +
                     to_string(look) + " of " + to_string(kFurtherLooks) +
                     ", so the averaged frame was a worse reading than this one and not a "
                     "camera that cannot be scored with");
        }
        still_refused = carried;
    }

    board_sight::faultDetail() = fault_before_looking;

    if (!still_refused.empty())
    {
        string left;
        for (size_t i : still_refused)
        {
            left += (left.empty() ? "" : ", ") + to_string((int)i + 1);
        }
        // #1389: the count is the less useful half, and the reason each camera was refused
        // is already on that camera's own ERROR line from every look it was given. What
        // this adds is the one thing those lines cannot say -- that it was asked more than
        // once and answered the same way, which is what tells a transient from a camera to
        // go and look at.
        log_error("LOOK AGAIN: camera(s) " + left + " were refused on the averaged frame and on all " +
                  to_string(looks_spent) + " further looks, so they are set aside for this run; "
                  "the reason is on each camera's own line above and is the thing to act on");
    }
}

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

    // #1372: WHERE THE GEOMETRY CAME FROM IS THE ONLY THING THIS FLAG DECIDES.
    //
    // Until #1372 the branch below was a second, shorter `initialize`: it loaded the file,
    // applied the anchors, set `calibrated = true` and RETURNED -- so a board that came up
    // on a cached calibration was asked neither of the two arithmetics further down. Not
    // whyNoEventIsPossible (#1338, as amended by #1348), and not whyNoStateChangeIsPossible
    // (#1348). A board restarted on a cache could therefore beat READY while unable to
    // score a single dart, which is the exact state #1338 exists to refuse, and nothing in
    // the program would have said a word about it.
    //
    // The repair is structural rather than a copy of the gate into this branch, and that is
    // the point of the issue rather than a preference. A copied rule is two rules that
    // agree today: #1353 moved `min_cameras_for_event` and #1348 had to chase the
    // consequence one stage on, and a second copy of the census here is exactly what that
    // chase would have missed. So this branch now produces `calibrations` and nothing else,
    // which is all the measuring branch below it produces either, and BOTH fall into one
    // gate that counts the cameras, asks both arithmetics, writes `scoring_with` and
    // records the fault. There is one `calibrated =` in this function.
    const bool from_cache = !calibrations.empty();
    if (from_cache)
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

        // #1372: a cached calibration for a camera that produced no frame THIS start is a
        // camera that did not calibrate this start, and it is written down as one here so
        // that the census below can be asked of `calibrations` alone -- the same question,
        // off the same field, whichever branch filled it in.
        //
        // The measuring branch gets this for free: calibrateMultipleCameras leaves
        // `sees_board` false for an empty frame and says so by name. The file cannot,
        // because it was written on a start where that camera was answering. Left alone it
        // is the one way the two populations could differ: a silent camera would carry a
        // cached board into `voting`, and both quorums would be measured against a camera
        // that can never spike and never vote, which is the very over-count #1348 separated
        // the three populations to stop.
        //
        // NOTE for a reviewer, and it is the thing to argue with: this writes a camera off
        // for the life of the run on the strength of the calibration frames, exactly as the
        // measuring branch does. A camera that is unplugged at start and plugged back in
        // during the evening is therefore not counted here -- #899's review is what brings
        // a board back, not this function.
        for (size_t i = 0; i < calibrations.size(); i++)
        {
            if (!calibrations[i].sees_board)
            {
                continue;
            }
            if (i >= initial_frames.size() || initial_frames[i].empty())
            {
                log_warning("Camera " + to_string(i + 1) + " produced no frame this start, so the "
                            "cached calibration for it cannot be scored with; that camera abstains");
                calibrations[i].sees_board = false;
            }
        }

        // #1363: the operator's anchors apply to a cached calibration exactly as to a
        // fresh one -- the cache holds measurement, the statement lives in configuration.
        applyConfiguredAnchors();

        // ---- #1372 instrumentation, and NOT a feature. The same shape as OD_DROP_CAM and
        // OD_BLIND_AFTER, and named so that nobody can read it as a choice an operator has
        // to make. It restores this branch to exactly what it was before this issue: the
        // board admitted on the strength of the file, with neither arithmetic asked.
        //
        // #1348 had a real flag to falsify against, OD_STATE_QUORUM. There is no real flag
        // here -- what #1372 changed is a control-flow fact, not a rule with a constant --
        // so the only way to show that the gate is what refuses the board is to make the
        // gate unreachable on this path and watch the same binary, on the same footage,
        // beat READY again. Without this, the harness's refusal is a claim about a build.
        const char *skip = getenv("OD_CACHE_SKIPS_THE_GATE");
        if (skip && string(skip) == "1")
        {
            log_warning("OD_CACHE_SKIPS_THE_GATE is set: this start is admitted on the cached "
                        "calibration alone, as it was before #1372, and no arithmetic has been "
                        "asked about whether it can score");
            initialized = true;
            calibrated = true;
            return true;
        }
    }

    // #1372: one gate, two sources. `from_cache` short-circuits the frame count because a
    // cache that loaded at all was already checked against this run's frames in
    // cache::geometry::load -- the camera count and every answering camera's resolution --
    // and the board it describes is a thing this board can be refused for, which is what
    // the gate is for.
    if (from_cache || camera::validCount(calibration_frames) > 0)
    {
        if (!from_cache)
        {
            log_info("Performing immediate calibration...");

            calibrations = geometry_calibration::calibrateMultipleCameras(
                initial_frames,
                debug_mode,
                target_width,
                target_height);

            // #1445: and a second picture for any camera that slot is still empty for.
            //
            // HERE, and the position is the argument rather than a detail. It is after
            // the first pass, so a camera that calibrated is already done and is not
            // looked at; it is before the gate below, so the census, both arithmetics and
            // `scoring_with` are all asked of the cameras this board really ended up
            // with; and it is before `sealed_geometry` is taken at the end of this
            // function, so there is no geometry yet for a later look to have departed
            // from. ADR-0080 §2 is about a board changing its mind after it has decided;
            // this is the deciding.
            //
            // It is on the MEASURING path only. A cached start did not look at the board
            // at all (#1372) and a second look it did not take could not be written into a
            // file it is not writing; a camera missing from a cache is the case #1372
            // handed to #899's review, and it still is.
            lookAgainAtRefusedCameras();
        }

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

        // #1372: the two refusals below are the same refusal, and they name their subject
        // so that an operator reading a log knows whether to go and look at the cameras or
        // to delete cache/. `board_sight::recordFault` is handed the arithmetic sentence
        // alone either way, so BOARD FAULTED reads identically on both paths -- the reason
        // a board cannot score is a fact about the board, not about where its numbers came
        // from.
        const string refusal_opening = from_cache
                                           ? "The cached calibration cannot be scored with: "
                                           : "Initial calibration failed: ";

        if (calibrated)
        {
            if (!from_cache)
            {
                // Save frames as background (for dart detection). #1372: the cached branch
                // already has its backgrounds out of the file, and the frames this start
                // took are not the frames its geometry was measured on.
                background_frames.clear();
                for (const auto &frame : initial_frames)
                    background_frames.push_back(frame.clone());
            }

            // #1372: the same verdict, named for where the geometry came from, because an
            // operator who passed --reuse-calibration needs to know that the board he is
            // about to throw at was admitted on a measurement no camera took tonight.
            log_info(string(from_cache ? "Cached calibration accepted on " : "Initial calibration completed successfully on ") +
                     to_string(seeing) + " of " + to_string((int)calibrations.size()) + " cameras");

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

            // #1372: the file is written by the start that measured it and by no other.
            // A cached start writing itself back would restamp geometry it never took --
            // and, since the loop above may have set `sees_board` false on a camera that
            // was merely quiet tonight, would write that camera off in the file too.
            if (!from_cache)
            {
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
                // start re-applies the operator's statement from configuration. The cached
                // branch applied them before this gate, off the same configuration; the
                // anchors decide orientation and no part of the census above.
                applyConfiguredAnchors();
            }

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
            // #1372's opening, #1389's naming: the two are about different halves of the
            // same sentence. Where the numbers came from is #1372's subject, and which
            // camera failed on what is #1389's, so the refusal carries both.
            log_error(refusal_opening + "none of the " + to_string((int)calibrations.size()) +
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
            // #1372's opening again. `recordFault` is deliberately NOT given it: the
            // reason a board cannot score is a fact about the board, not about whether
            // its numbers came out of cache/, so BOARD FAULTED reads identically on both
            // paths. #1389's per-camera naming does go into the fault detail, because
            // that IS a fact about the board.
            log_error(refusal_opening + why + ". " + named);
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

    // #1388: the geometry, sealed at the one moment this board is allowed to decide what
    // its geometry is. It is taken here rather than in the success branch above so that
    // it covers every path initialize can leave by -- fresh calibration, cached
    // calibration, and the three failures, which seal the empty geometry they are going
    // to fault on. A board that seals nothing is a board that could not be shown to have
    // adopted anything later, which is the one outcome this must not have.
    sealed_geometry = geometry_agreement::fingerprint(calibrations);
    log_info("GEOMETRY SEALED: " + sealed_geometry);

    return initialized;
}

/**
 * #1388: whether this detector is still scoring with the geometry it sealed.
 *
 * Asked once per scoring cycle by Scorer's loop, which is the only place that can act on
 * the answer. Rebuilding the line every cycle rather than caching a hash is deliberate
 * and it is not a measurable cost: it is a few dozen numbers formatted for three cameras,
 * against three frames of video decoded, differenced and scored in the same cycle. What
 * it buys is that the check reads the LIVE calibrations every time, so there is no second
 * copy of the geometry that could be updated in step with the first and agree with it.
 */
string GeometryDetector::geometryBreach() const
{
    if (sealed_geometry.empty())
    {
        // initialize() has not finished. There is nothing to have departed from.
        return "";
    }
    const string now = geometry_agreement::fingerprint(calibrations);
    if (now == sealed_geometry)
    {
        return "";
    }
    return "the geometry this board is scoring with is not the geometry it calibrated. "
           "It calibrated on [" +
           sealed_geometry + "] and it is now holding [" + now + "]";
}

/**
 * #899: the board is asked whether it is still looking at what it calibrated on.
 *
 * Called by Scorer after a sight loss has been survived and the cameras have been
 * reopened, on a fresh average of frames. What comes back is a verdict and the numbers it
 * was reached by; `calibrations` is not touched, and the board goes on scoring with the
 * geometry it earned at start or it stops scoring at all.
 *
 * #1388: "`calibrations` is not touched" is now enforced rather than stated. The fresh
 * calibration below is a local, it is read by `measure` and it goes out of scope; the
 * seal taken at the end of `initialize` is what makes that a fact a test can fail on
 * instead of a sentence in a comment. See `geometryBreach` and ADR-0080 §2.
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
