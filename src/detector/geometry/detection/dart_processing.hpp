#pragma once

#include <opencv2/opencv.hpp>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "detector/geometry/camera_quorum.hpp"
#include "motion_processing.hpp"

using namespace cv;
using namespace std;

namespace dart_processing
{
    // Dart board state - exactly as you described
    enum class DartBoardState
    {
        CLEAN,  // No darts, matches background
        DART_1, // 1 dart on board
        DART_2, // 2 darts on board
        DART_3  // 3 darts on board
    };

    /**
     * #1389 falsification switch: OD_STATE_FLOOR=<n> pins the state vote's floor, and
     * pins it AGAINST the camera quorum, which is the only way to reproduce ADR-0081 §1
     * on one binary.
     *
     * The defect this issue removed was not a number that was wrong. It was three numbers
     * that DISAGREED: a calibration gate at 1, an event census at 1, and a vote at 2. A
     * board caught between them is admitted, opens dart windows, and can never move its
     * own state -- inert, while reporting itself healthy. With one constant there is
     * nothing left in the program that can be set to that combination, so the defect
     * becomes undemonstrable and the fix becomes a claim rather than a measurement. This
     * puts the disagreement back on request:
     *
     *     OD_CAMERA_QUORUM=1 OD_STATE_FLOOR=2 OD_STATE_QUORUM=absolute
     *
     * is exactly what #1318, #1353 and the pre-#1348 vote shipped, and
     * testers/phases1389/1389-floor.sh phase E runs the real binary under it and watches
     * the board be admitted, form windows and move its state not once.
     *
     * It is a PIN and nothing reads it on an ordinary run: unset, zero and anything that
     * is not a positive number all leave the floor where camera_quorum puts it. It is
     * deliberately not a second home for the number -- a pin that wins where it is set
     * and is absent everywhere else, which is the shape `HOSTING_PRICE_<SPORT>` has in
     * the application this detector reports to.
     */
    inline int stateFloorPin()
    {
        static const int pinned = []
        {
            const char *e = std::getenv("OD_STATE_FLOOR");
            if (e == nullptr || *e == '\0')
            {
                return 0;
            }
            const int n = std::atoi(e);
            return n > 0 ? n : 0;
        }();
        return pinned;
    }

    // Parameters for dart state detection
    struct DartParams
    {
        // background comparison parameters
        double background_diff_threshold = 20; // Minimum difference to consider a pixel changed

        // Frame processing parameters
        int blur_kernel_size = 5;  // Fill dart gaps
        int dilate_iterations = 3; // Control dart expansion
        int erode_iterations = 2;  // Control noise removal

        // Simple morphological operations
        int morph_kernel_size = 4; // Size of morphological kernel

        // Statbility frames
        int stability_frames = 6; // Frames needed to confirm state change / (3 cameras * 2 frames per camera)

        // #1350 hoisted the literal the state stage answers against, so the vote's
        // account below can name the number the code really read. #1354: this is the
        // FALLBACK now, used only when no camera on the board has a fitted board to
        // measure against -- the frame is then all there is, exactly as it always was.
        // The value is unchanged and its denominator is each camera's whole frame.
        double change_percent_threshold = 0.22; // % of a camera's own frame; the no-fitted-board fallback

        // #1354: the deciding figure where a board IS fitted, as a share of that
        // camera's own board -- #1339's move, one stage on, with #1345's instrumentation
        // as the ruler. The same constant answers both questions the vote asks: a board
        // whose CUMULATIVE change (vs the calibration background) is under it is CLEAN,
        // and an occupied board advances only when the FRESH change (vs the working
        // background -- what arrived since the last dart) is over it. Cumulative alone
        // was the false-takeout and false-advance machine: with darts on the board,
        // every window's cumulative figure re-argued the whole history.
        //
        // FITTED BY SWEEP over both fixtures (debian-12/OpenCV 4.6), scores per run:
        //
        //   value  rig darts scored            mocks round 1
        //   0.10   9 (and the OUTER bull)      S12 S7 S17 -- #796's hand-verified 36
        //   0.15   10                          S12 S20    -- the two small darts lost
        //   0.20   10                          S12 S20
        //   0.30   5                           S12 S20, and more lost after it
        //
        // The rig is flat across 0.10-0.20; the mocks' small darts demand 0.10, and
        // 0.10 is the only value that reproduces the one hand-verified round this
        // repository has. On the rig's ~197,000 px boards 0.10% is ~197 px; darts there
        // measured 348-15,556 px on the board (shadows inflate the big end), and an
        // empty window's residue measured 0 px on the rig, 13-271 px on the mocks.
        //
        // #1514: on mocks/rig-20260922 the empty-window residue is NOT near zero, and no
        // value of this constant can be. That fixture's calibration frames hold a dart
        // parked in the board (frame 0 of every camera shows it); it is pulled before
        // the first throw, so every settled window's cumulative diff carries its
        // silhouette for ever: 5,767-5,968 px on cam 1 (2.7% of its 215,925 px board),
        // 1,175-1,254 px on cam 2 (0.56%), 1,021-1,175 px on cam 3 (0.58%) -- measured
        // across all 30 completed windows of the whole clip (testers/i1514_run.sh
        // reproduces the census). The residue OVERLAPS the darts' own cumulative range
        // above (0.18-7.9% of the board on rig-20260918), so no threshold separates
        // them: raising this past 2.7% would swallow every one-dart board, and at any
        // value below it no camera on that fixture ever reads CLEAN, `goes_clean` never
        // reaches its quorum, no takeout is reconciled and the board wedges at DART_3
        // with detection stalled at 3 of 24. The repair is a reference that can recover
        // from a permanent scene change (issue #1514), not a move of this number.
        double board_change_percent_threshold = 0.10; // % of a camera's own fitted board

        // #1348: the vote's quorum, and the population it is measured against.
        //
        // `min_cameras_to_move_the_board` is the CORROBORATION rule and it is a floor: a
        // board never moves on one camera's word. That is not #1345's finding restated --
        // #1345's camera 1 voted DART_1 on the thrower's shoes and the cure was a
        // LOCATION (#1354: the deciding figure is the board's own share, and a camera
        // with no fitted board abstains), not a bigger majority. The two guards answer
        // different questions and neither implies the other: location says whether what a
        // camera saw is on the board, corroboration says whether one camera saying so is
        // enough.
        //
        // What #1348 adds is the population. The vote already excludes abstainers -- #798
        // for a camera that contributed no frame to the window, #1354 for one with no
        // fitted board while another has one -- so `moves_up` and `goes_clean` are counts
        // over the VOTERS, while the 2 they were compared against was absolute. One
        // abstainer silently turned "2 of 3" into unanimity, two made any state change
        // impossible -- takeouts included -- and nothing said so at any level. So the
        // quorum is a majority of the voters with the floor above it:
        //
        //   voters  1  2  3  4  5      quorum  2  2  2  3  3
        //
        // At three voters and under that is the shipped 2, which is why nothing either
        // fixture measures moves; above it, it is the half #1355 made reachable, where an
        // absolute 2 is a MINORITY of a four-camera board.
        // #1389: the floor is `camera_quorum::kCameras` and is no longer written here.
        // It was one of the three copies of this number ADR-0081 §2 is about, and it is
        // the copy the other two are measured against: everything below it is arithmetic
        // a board cannot reach. The field is kept so that a tester can still move it
        // under a fixed board -- #1348's whole argument for it -- and so that
        // OD_STATE_QUORUM=absolute has something to restore.
        int min_cameras_to_move_the_board =
            stateFloorPin() > 0 ? stateFloorPin() : camera_quorum::cameras(); // The floor: never one camera's word
        // #1348 falsification: the absolute count the vote used before it, restored under
        // a fixed board. Set from OD_STATE_QUORUM=absolute at run time.
        bool absolute_quorum = false;
    };

    /**
     * #1348: how many of this window's voters it takes to move the board.
     *
     * A majority of the population that actually voted, never fewer than the floor. Pure
     * and inline for the reason whyNoEventIsPossible is (#1338): the table is a thing a
     * tester holds without building the detector, and a constant that can be moved under
     * a fixed board is a gate that can be made to fail.
     */
    // #1348 falsification switch: OD_STATE_QUORUM=absolute restores the vote as it was
    // before this issue -- the absolute count, and a calibration that never asks the
    // vote's arithmetic. Defined in dart_processing.cpp, where the reason is written.
    bool stateQuorumIsAbsolute();

    inline int stateVoteQuorum(int voters, const DartParams &params = DartParams())
    {
        if (params.absolute_quorum)
        {
            return params.min_cameras_to_move_the_board;
        }
        const int majority = voters / 2 + 1;
        return majority > params.min_cameras_to_move_the_board
                   ? majority
                   : params.min_cameras_to_move_the_board;
    }

    /**
     * #1348: why this board can never change state, or an empty string if it can.
     *
     * whyNoEventIsPossible's argument, one stage on and against the other population.
     * `cameras_that_can_vote` is how many cameras bring both a frame and -- where any
     * camera on the board has one -- a fitted board to measure against; it is the ceiling
     * on `moves_up` and on `goes_clean` for the life of the run, because a calibration
     * does not change under a running board. A board whose ceiling is under its own
     * quorum does not score rarely: it holds CLEAN for ever, and cannot see a dart taken
     * out either.
     *
     * This is the gap #1353 opened where it closed the other one. Moving
     * `min_cameras_for_event` to 1 made the event quorum reachable on one camera, and the
     * vote's 2 then became the binding arithmetic that nothing asked -- so a one-camera
     * board passed #1338's gate and beat READY while unable to leave CLEAN. #1348 is that
     * sentence, asked where its own constant lives.
     *
     * #1321's rule on the sentence: the count is stated against the threshold it fell
     * short of.
     */
    inline std::string whyNoStateChangeIsPossible(int camera_slots, int cameras_that_can_vote,
                                                  const DartParams &params = DartParams())
    {
        const int quorum = stateVoteQuorum(cameras_that_can_vote, params);
        if (cameras_that_can_vote < quorum)
        {
            return "only " + std::to_string(cameras_that_can_vote) + " of " +
                   std::to_string(camera_slots) +
                   " cameras can vote on what is on the board -- a camera needs a frame and a "
                   "fitted board to vote with -- and it takes " + std::to_string(quorum) +
                   " of them to move the board, so this board can neither call a dart nor "
                   "see one taken out";
        }
        return "";
    }

    /**
     * #1518: a board still over the CLEAN ceiling whose change just REVERTED by at least
     * a dart's worth -- the takeout, read from the direction of change rather than from
     * its size.
     *
     * The CLEAN test's cumulative diff is an absdiff against the reference, and an
     * absdiff has no sign: a dart standing in the board and the hole where a dart used
     * to stand are the same figure to it. On mocks/rig-20260922 that is the whole stall
     * (#1514) -- a dart parked in the board at calibration is pulled ~1 s in, every
     * later window's cumulative diff carries its silhouette (0.56-2.76% of the board
     * against the 0.10% ceiling), no camera ever reads CLEAN, no takeout reconciles and
     * the board wedges at DART_3 for 26 windows. #1514 refused every threshold move by
     * overlap, so the repair cannot be a size.
     *
     * What a takeout has that nothing else in either fixture has is a SIMULTANEOUS,
     * dart-sized FALL of the cumulative figure on a quorum of cameras: the darts leave
     * every camera's view in one window. Both halves are measured, on the whole of
     * rig-20260922's stalled trajectory (30 windows, OD_WINDOW_CENSUS=1, 2026-09-23):
     *
     *   - all 7 retrieval windows (w07 w11 w15 w19 w23 w27 w30) have >= 2 cameras whose
     *     cumulative board figure fell by >= that camera's own CLEAN ceiling at once
     *     (falls of 620 to 27,397 px against ceilings of 182-215 px);
     *   - ZERO of the other 22 windows have more than ONE such camera. The thrower's
     *     shadow produces enormous single-camera falls (cam 3: -133,021 px, w08; cam 1:
     *     -12,953 px, w07's neighbour w03) -- which is why one camera's reversion is a
     *     VOTE and never a verdict: the same quorum that stops one camera calling a
     *     dart (#1348) stops one shadow calling a takeout.
     *
     * The falling camera votes CLEAN; the reconciled CLEAN then re-bases every camera's
     * reference to this window's settled frames (the adoption in dart_processing.cpp),
     * so the next round is measured against the scene as it now is and the ordinary
     * under-the-ceiling CLEAN test works again. On a healthy fixture the vote never
     * fires: a takeout there lands UNDER the ceiling, which votes CLEAN one branch
     * earlier (measured on rig-20260918: no window has 2 simultaneous over-ceiling
     * falls; see the pull request).
     *
     * The fall's floor is the camera's own CLEAN ceiling -- the number that already
     * defines "a dart-sized amount of board change" (board_change_percent_threshold,
     * whose census is above) -- deliberately not a new constant: the smallest measured
     * true-takeout fall is 620 px (2.9x the ceiling) and the largest thing that must
     * not trip a quorum is handled by the quorum, not by this floor.
     *
     * THE CANDIDATE THIS WON AGAINST, refused by measurement the #1514 way: an
     * add-versus-remove discriminator reading the PICTURES -- per changed region, the
     * mean absolute deviation of the region's pixels from the median of its own
     * unchanged surround, asked of the current frame and of the reference; the image
     * holding the object should deviate more. On synthetic figures it separates
     * perfectly; on the real fixture it does not separate AT ALL: the board's own
     * texture (wedge boundaries, wires, print) puts both scores at 45-102 with the
     * object's contribution buried -- the pull window read 1.28-1.95x
     * (reference/current, 3 cameras) while ordinary ADDITION windows read up to 1.80x
     * in the same direction (w02 cam 1: 81.6 vs 45.2). No margin separates 1.95 from
     * 1.80. Measured 2026-09-23, one whole-clip run, 90 camera-windows; the probe and
     * its numbers are in #1518's report.
     *
     * `previous_pixels < 0` means there is no previous window to fall from, and that is
     * NO verdict -- which is also why the pull of the parked dart itself (window #1 of
     * rig-20260922, a RISE from zero) is out of this function's reach: the phantom score
     * it publishes is deferred, by name, in #1518's report.
     *
     * Pure and inline for the reason whyNoEventIsPossible is (#1338): a tester holds the
     * rule without building the detector, and testers/i1518_check.sh does.
     */
    inline bool readsAsReversion(int previous_pixels, int current_pixels, int ceiling_pixels)
    {
        return previous_pixels >= 0 && ceiling_pixels > 0 &&
               current_pixels >= ceiling_pixels &&
               previous_pixels - current_pixels >= ceiling_pixels;
    }

    // Per-camera detection result
    struct CameraDetectionResult
    {
        DartBoardState detected_state = DartBoardState::CLEAN;
        int total_changed_pixels = 0;              // Total changed pixels
        double change_ratio = 0.0;                 // Percentage of changed pixels
        int total_pixels = 0;                      // Total pixels in frame
        // #1345: the same changed pixels counted again inside this camera's own fitted
        // board. They decide nothing -- `change_ratio` above is what the state stage
        // answers with, over the whole frame, as it always was. They are here because
        // without them the figure cannot be read: a camera can clear 0.22% of its frame
        // with every one of those pixels off the board, and on mocks/rig-20260918 the one
        // camera that clears it does exactly that. `board_pixels` is 0 when this camera's
        // board was never fitted, which means unknown rather than none.
        int board_changed_pixels = 0;
        int board_pixels = 0;
        // #1354: what arrived since the LAST dart, inside the board -- the working-diff's
        // board share, which is what an advance is decided on where a board is fitted.
        // -1 where it was not computed (a CLEAN board, or no fitted board).
        int fresh_board_pixels = -1;
        Point2f tip_position = Point2f(-1, -1);    // Position of dart tip if found
        Point2f center_position = Point2f(-1, -1); // Center of biggest dart shape
        bool tip_found = false;                    // Was tip found in this frame
        bool frame_available = true;               // #798: did this camera contribute any frame to the window
        // #1354: this camera has no fitted board while another camera does, so it has no
        // denominator to decide with and it abstains from the vote -- answering from the
        // frame instead is how the rig's camera 1 voted DART_1 on the thrower's shoes,
        // six windows out of six (#1345).
        bool abstained_no_board = false;
    };

    // Result of dart state detection
    struct DartStateResult
    {
        DartBoardState current_state = DartBoardState::CLEAN;  // Current dartboard state
        DartBoardState previous_state = DartBoardState::CLEAN; // Previous dartboard state
        bool state_changed = false;                            // Did the state change this frame
        int confidence_frames = 0;                             // How many frames we've been confident in this state
        vector<CameraDetectionResult> camera_results;          // Results from each camera
    };

    // get name of ENUM. Inline here since #1350, so the window account below -- and the
    // tester that holds it -- can name a state without linking the detector.
    inline string getDartBoardStateName(DartBoardState state)
    {
        switch (state)
        {
        case DartBoardState::CLEAN:
            return "CLEAN";
        case DartBoardState::DART_1:
            return "DART_1";
        case DartBoardState::DART_2:
            return "DART_2";
        case DartBoardState::DART_3:
            return "DART_3";
        default:
            return "UNKNOWN";
        }
    }

    /**
     * #1350: the sentence a completed window leaves at INFO when its vote changed
     * nothing.
     *
     * A window that scores already speaks at INFO -- "Consensus score", the takeout, the
     * SCORE line -- and a window that is refused used to leave one empty line, which is
     * how the only failure the rig shows tonight (#1345) was invisible at normal level.
     * So this answers EMPTY when the state moved, and the caller logs it exactly when it
     * is not empty: the scored path's INFO output stays byte for byte what the research
     * chain's controls were extracted from.
     *
     * The shape is #1321's: every camera's candidate beside the figure it answered with,
     * the two counts beside the number either of them needed, and the threshold the
     * figures are read against -- named from DartParams, not retyped, so a moved constant
     * moves this sentence with it.
     *
     * #1348: that number is now computed rather than typed, from the VOTERS this window
     * had, and the voters are recounted here from the same two abstention flags the vote
     * counts them from rather than being passed in -- so the sentence cannot name a
     * quorum the vote did not use. A window whose voters cannot reach their own quorum
     * says that too, because "1 moved up and 0 read CLEAN, either takes 2" is a true
     * sentence about a board that was never going to move at all.
     *
     * Pure and inline for the reason whyNoEventIsPossible is (#1338): a tester holds the
     * sentence to the vote without building the detector.
     */
    inline string refusedWindowAccount(const vector<CameraDetectionResult> &camera_results,
                                       DartBoardState previous_state,
                                       DartBoardState final_state,
                                       int moves_up,
                                       int goes_clean,
                                       const DartParams &params = DartParams())
    {
        if (final_state != previous_state)
        {
            return "";
        }
        char figure[32];
        string cameras;
        bool board_share_known = false;
        int voters = 0;
        for (const CameraDetectionResult &r : camera_results)
        {
            if (r.frame_available && r.board_pixels > 0)
            {
                board_share_known = true;
            }
            // #1348: the same two exclusions the vote makes, #798's and #1354's.
            if (r.frame_available && !r.abstained_no_board)
            {
                voters++;
            }
        }
        const int quorum = stateVoteQuorum(voters, params);
        for (size_t i = 0; i < camera_results.size(); i++)
        {
            if (!cameras.empty())
            {
                cameras += ", ";
            }
            cameras += "camera " + to_string(i + 1);
            if (!camera_results[i].frame_available)
            {
                cameras += " abstained (no frames this window)";
                continue;
            }
            if (camera_results[i].abstained_no_board)
            {
                // #1354: named beside the voters, so a board quietly down to one fitted
                // camera reads as what it is rather than as a camera that saw nothing.
                cameras += " abstained (no fitted board to vote with)";
                continue;
            }
            snprintf(figure, sizeof(figure), "%.3f", camera_results[i].change_ratio);
            cameras += " said " + getDartBoardStateName(camera_results[i].detected_state) +
                       " (" + figure + ")";
            // #1345: and how much of that figure was on the board it is about. Appended
            // after the figure rather than inside it, so the figure reads as it always
            // did; omitted entirely when the board was never fitted, because 0 px on an
            // unknown board would read as evidence and is not.
            if (camera_results[i].board_pixels > 0)
            {
                cameras += camera_results[i].board_changed_pixels == 0
                               ? ", none of it inside its own board"
                               : ", " + to_string(camera_results[i].board_changed_pixels) +
                                     " px of it inside its own board";
            }
        }
        snprintf(figure, sizeof(figure), "%.3f", params.change_percent_threshold);
        return "STATE VOTE: " + to_string(moves_up) + " moved up and " + to_string(goes_clean) +
               " read CLEAN, either takes " + to_string(quorum) + " of the " + to_string(voters) +
               " cameras that voted to move the board" +
               // #1348: and if the voters could never have reached it, that is the fact
               // about this window, not the counts above it. Said here because this is
               // where a window accounts for itself; the board-level case -- a ceiling
               // under the quorum for the life of the run -- is refused at calibration by
               // whyNoStateChangeIsPossible instead.
               (voters < quorum ? ", which those " + to_string(voters) + " could not have reached" : "") +
               ", so it stays " +
               getDartBoardStateName(final_state) + ": " + cameras +
               "; a figure is the % of that camera's own frame that changed, and " +
               figure + " is where a camera calls a dart" +
               // #1345: the clause above each camera's figure is what makes the figure
               // readable. The denominator is the whole frame, so a camera can clear the
               // threshold on the room around the board; on mocks/rig-20260918 the only
               // camera that clears it has none of its changed pixels on the board.
               (board_share_known ? ", which is a share of the frame and not of the board" : "");
    }

    /**
     * Process dart state detection using background comparison on all 3 cameras.
     *
     * #1345: `boards` carries each camera's fitted board, in that camera's slot, and
     * this stage only ever OBSERVES it. Nothing here decides on it -- `change_ratio` is
     * still changed pixels over the whole frame against `change_percent_threshold`,
     * exactly as before -- and it is read so that the account of a refused window can
     * say how much of each camera's figure was on the board at all. That number decides
     * nothing precisely so that it can be trusted as evidence for the issue that moves
     * the denominator.
     */
    DartStateResult processDartState(
        const vector<Mat> &current_frames,
        const vector<Mat> &background_frames,
        const vector<motion_processing::BoardExtent> &boards,
        bool movement_finished = false,
        bool debug_mode = false,
        const DartParams &params = DartParams());

} // namespace dart_processing