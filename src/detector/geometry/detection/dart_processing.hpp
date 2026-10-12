#pragma once

#include <opencv2/opencv.hpp>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "detector/geometry/camera_quorum.hpp"
#include "motion_processing.hpp"
#include "shaft_axis.hpp"

using namespace cv;
using namespace std;

namespace dart_processing
{
    /**
     * #1652: the fresh-diff cleanup -- CLOSE and OPEN with a k x k rectangle, then CLOSE
     * and OPEN with a (k/2) x (k/2) one -- in one place, so the detector and the testers
     * run the same chain.
     *
     * `centred = false` is the chain as it always was: `morphologyEx` at OpenCV's default
     * anchor. For an even k that anchor is (k/2, k/2), the element spans -k/2 .. k/2-1,
     * and dilate and erode both use that same, unreflected element, so every CLOSE and
     * every OPEN translates what it keeps by +1 px in x and in y. At the rig's k = 4 the
     * four passes move every mask (+4, +4) px (#1649, shaft_axis::supportChainShiftPx).
     *
     * `centred = true` (OD_MASK_UNSHIFT=on) runs each pair's second operation at the
     * reflected anchor (k-1-k/2, k-1-k/2). OpenCV's even-kernel CLOSE is exactly the true
     * closing by the element translated by (+1, +1), and the reflected anchor removes
     * that translation and nothing else, so this chain's output is the old chain's
     * output moved by (-4, -4) px, pixel for pixel away from the image border
     * (testers/i1652_mask_check.cpp asserts both). An odd kernel (3 or 5) would also be
     * shift-free but is a different element, and changes which gaps close and which
     * specks open -- the same check measures by how much.
     */
    inline void cleanFreshMask(cv::Mat &m, int k, bool centred)
    {
        const cv::Mat k1 = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(k, k));
        const cv::Mat k2 = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(k / 2, k / 2));
        if (!centred)
        {
            cv::morphologyEx(m, m, cv::MORPH_CLOSE, k1); // Close small gaps in darts
            cv::morphologyEx(m, m, cv::MORPH_OPEN, k1);  // Open small noise
            cv::morphologyEx(m, m, cv::MORPH_CLOSE, k2); // Close smaller gaps in darts
            cv::morphologyEx(m, m, cv::MORPH_OPEN, k2);  // Open smaller noise
            return;
        }
        auto pass = [&m](const cv::Mat &e, bool close)
        {
            const cv::Point a(e.cols / 2, e.rows / 2);
            const cv::Point reflected(e.cols - 1 - a.x, e.rows - 1 - a.y);
            if (close)
            {
                cv::dilate(m, m, e, a);
                cv::erode(m, m, e, reflected);
            }
            else
            {
                cv::erode(m, m, e, a);
                cv::dilate(m, m, e, reflected);
            }
        };
        pass(k1, true);
        pass(k1, false);
        pass(k2, true);
        pass(k2, false);
    }

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
        // #1685: the window in milliseconds of the motion clock, counted unless
        // OD_WINDOW_UNIT=cycles: 6 cycles x 33.3 ms. The window averages the settled board
        // over a fifth of a second; as a count it held the next dart's arrival on a board
        // whose cycle takes 200 ms or more. On such a board it averages fewer frames, and
        // one frame when a cycle is longer than the window (od_clock::window_reached).
        int stability_ms = 200;

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
     * earlier. Measured on the whole of rig-20260918 (25 windows, same census, same
     * day): not one window holds even ONE camera falling dart-sized while still over
     * the ceiling, zero reversion votes were cast in the run, and its i1484 census is
     * byte for byte #1515's table -- 18 of 21 detected, 13 correct of 18, 6 ENDs.
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

    /**
     * #1552: a reversion CLEAN vote is REMEMBERED into the next two windows, because the
     * cameras a takeout needs do not always fall in ONE window -- and before the first
     * reference adoption they measurably do not.
     *
     * #1518's quorum asks for two cameras falling dart-sized IN THE SAME WINDOW. Every
     * takeout AFTER the first reference adoption satisfies that on mocks/rig-20260922
     * (all six later takeouts in both whole-clip runs reconcile in one window). The
     * FIRST takeout does not, and cannot: until the first adoption each camera's CLEAN
     * reference is the calibration picture, which on this fixture is wrong by a
     * different amount per camera (a parked dart, the thrower mid-frame), so each
     * camera's figure falls to its own residue at its own moment -- when the retriever
     * leaves ITS view. Measured on the two whole-clip runs of 2026-09-24
     * (runs-od-wt-1551, opening and dev windows, one binary):
     *
     *   - opening window: camera 1 fell 17793 -> 5840 px and voted CLEAN, outvoted 2-1
     *     by the takeout's own motion reading as an arrival (published: MISS, no tip on
     *     any camera); camera 3 fell 122223 -> 1820 px TWO windows later, outvoted 2-1
     *     by the next visit's first dart arriving in the same window. No window held two
     *     reversions; the first END came only at the SECOND takeout (camera 1
     *     18769 -> 5816 and camera 2 10227 -> 1217, one window).
     *   - dev window (2 admitted cameras, so quorum 2 is unanimity): three consecutive
     *     windows each split 1-1 -- cam3 CLEAN 133323 -> 2677 vs cam2 DART_2; cam2
     *     CLEAN 10894 -> 2524 vs cam3 DART_3 (79.461% of frame: the retriever); cam3
     *     CLEAN 134932 -> 2238 vs cam2 DART_3.
     *
     * In both runs the boundary between thrown visits 1 and 2 therefore produced no
     * END, the next visit's darts were appended to the first (two of them swallowed by
     * the DART_3 cap), and every per-visit census from there on compared darts across a
     * boundary -- issue #1552's whole subject.
     *
     * So a camera that cast a reversion vote stays a CLEAN voter for the next
     * `reversionMemoryWindows()` windows: the memory horizon is 2 because the measured
     * splits are 1 window (opening, cam1 -> cam3) and 2 windows (dev, cam3 -> cam2),
     * and a longer memory only widens the false-END coincidence window for nothing
     * either fixture shows. The memory is cleared by a reconciled CLEAN (the takeout it
     * was evidence FOR has been served) and by an advance that carried at least one
     * found tip -- a dart was really called, so the board is not clean. An advance with
     * NO tip anywhere does not clear it, deliberately: that is the takeout's own motion
     * winning the vote (the retriever's blob is over the 20,000 px contour cap, so it
     * can advance the state but never yields a tip; or its "tip" is a #1535 re-report),
     * and it is exactly the window the memory has to survive. Measured: the phantom
     * MISS between the two split reversions carried zero tips in both runs.
     *
     * THE COST, named with its dart: when the next visit's first throw lands in the
     * same window as the last camera's fall, CLEAN now wins that window (the vote
     * already prefers CLEAN at quorum), the END is published, and that dart goes
     * unpublished into the adopted reference -- on rig-20260922 that is the thrown 12,
     * today published as S12 ACROSS the merged boundary. One dart against every
     * boundary from there on; the two darts the DART_3 cap swallows today (t9, t8)
     * come back as the next visit's. On a scene whose reference is right the rule is
     * inert by construction: rig-20260918's whole clip casts zero reversion votes, so
     * there is no memory to count (#1518's census).
     *
     * FALSE-END BOUND, measured rather than hoped: a false END needs two lone
     * reversions within the memory horizon with no tipped advance between. In the two
     * whole-clip rig-20260922 runs every lone reversion outside the first boundary
     * either sits in the window that reconciled CLEAN anyway, or is followed by a
     * tipped advance before any second reversion (opening: camera 1's lone fall two
     * windows before T10, cleared by T10's tip).
     *
     * THE CENSUS THIS SHIPPED WITH (2026-09-24, one dev binary, whole clips; the tree
     * rule measured over THREE consecutive runs apiece, every run line-identical to
     * its siblings modulo the Processing-ms wall clock; the pinned column is
     * OD_REVERSION_MEMORY=off on the same binary):
     *
     *   fixture / window       pinned (pre-#1552)                tree rule, 3x
     *   rig-20260922 opening   21 det, 7 seen, 6 END, 1 merged   22 det, 8 of 8 seen, 7 END, 0 merged
     *   rig-20260922 dev       17 det, 6 seen, 6 END, 2 merged   18 det, 7 seen, 7 END, 1 merged
     *   rig-20260918           19 det, 7 of 7, 6 END, 0 merged   the SAME stream, byte for byte
     *
     * Opening window correctness 1 of 21 -> 8 of 22: visits 4-7 align exactly (visit 4
     * is the first fully correct trio this fixture has produced), and visits 2-3 carry
     * the suppressed 12 as a one-dart shift. The dev window's remaining merged
     * boundary is thrown visit 2 itself -- all three of its darts fall to the
     * 2-admitted-camera calibration (#1551) and the visit vanishes whole, which the
     * census now reports as segmentation-or-undetected rather than as "the run ended";
     * its published visits 2-7 are thrown visits 3-8, spot-checked: published visit 2
     * is S20 D20 S1 against thrown 20 d20 1, exactly. On rig-20260918 the memory
     * fired once and changed no vote's outcome: the tree and pinned streams are
     * identical, so the rule is measured inert on the healthy fixture.
     *
     * Pure and inline for the reason whyNoEventIsPossible is (#1338): a tester holds
     * the rule without building the detector, and testers/i1552_memory_check.cpp does.
     * OD_REVERSION_MEMORY=off restores the unremembered vote, so before/after is two
     * runs of one binary.
     */
    inline int reversionMemoryWindows()
    {
        return 2;
    }

    /**
     * #1552: whether this camera is a CLEAN voter in this window -- by its own candidate,
     * or by the reversion it cast within the memory horizon. The memory deliberately
     * outranks the camera's own non-CLEAN candidate: the candidate it would otherwise
     * bring is the retriever's motion or the next visit's first dart, and both are the
     * evidence the split boundary is lost to (the account above).
     */
    inline bool votesCleanThisWindow(DartBoardState candidate, int reversion_memory_left)
    {
        return candidate == DartBoardState::CLEAN || reversion_memory_left > 0;
    }

    /**
     * #1552: whether an advance the vote just reconciled clears the reversion memory.
     * A tipped advance is a dart really called -- the board is occupied and the memory
     * is stale. A tip-less advance is the takeout's own motion (the blob over the
     * contour cap, or a #1535 re-report), which is the window the memory exists to
     * survive; both runs' phantom MISS carried zero tips.
     */
    inline bool advanceClearsReversionMemory(bool any_tip_found)
    {
        return any_tip_found;
    }

    /**
     * turnaus#1793: a dart that lands after the board has counted three.
     *
     * The board counts arrivals itself, CLEAN -> DART_1 -> DART_2 -> DART_3, and before
     * #1793 a camera at DART_3 STAYED at DART_3 whatever arrived: its candidate equalled
     * the board's state, so the vote counted it as a stay and the window published
     * nothing. When the board had counted a dart nobody threw (a phantom, a knock) and the
     * player removed it in Turnaus ("There was no dart there", turnaus#1723), the round
     * had room again and the visit's real third dart was never pushed.
     *
     * Under OD_PAST_THREE=on a camera at DART_3 whose fresh figure clears the floor (or
     * the rim floor, #1689) votes an arrival past three, the vote counts it as an up vote,
     * and a quorum calls a dart: the board stays DART_3 (there is no fourth state, and the
     * takeout still reconciles from it) and the window publishes like any advance. Turnaus
     * decides whether the round has room: it counts the dart when a withdrawal left room
     * and answers DROPPED to a fourth dart into a full round (turnaus#1281).
     *
     * Pure and inline (#1338's reason): testers/i1793_round_check.cpp replays a round on
     * these without building the detector.
     */
    inline bool votesArrivalPastThree(DartBoardState camera_previous, bool fresh_arrival, bool switch_on)
    {
        return switch_on && fresh_arrival && camera_previous == DartBoardState::DART_3;
    }

    /** turnaus#1793: whether a camera's vote is an up vote against the board's state. */
    inline bool votesUp(DartBoardState detected, DartBoardState board, bool arrived_past_three)
    {
        return detected > board || (arrived_past_three && board == DartBoardState::DART_3);
    }

    /**
     * turnaus#1793: the vote's three rules, as dart_processing.cpp applies them. Returns the
     * reconciled state; `past_three` is set when a quorum called an arrival on a board
     * already at DART_3 -- the one called dart that leaves the state where it was.
     */
    inline DartBoardState reconcileVote(DartBoardState board, int goes_clean, int moves_up, int quorum,
                                        bool &past_three)
    {
        past_three = false;
        if (goes_clean >= quorum)
        {
            return DartBoardState::CLEAN; // Rule 3: a quorum thinks CLEAN
        }
        const bool quorum_moves = moves_up >= quorum;
        if (quorum_moves)
        {
            if (board == DartBoardState::DART_3)
            {
                past_three = true; // reachable only through votesUp's past-three clause
                return board;
            }
            return static_cast<DartBoardState>(static_cast<int>(board) + 1); // Rule 1: a quorum moves up
        }
        return board; // Rule 2: stay put
    }

    /**
     * turnaus#1793: whether the vote called a dart in this window -- an advance, or an
     * arrival past three. What follows a called dart (the working backgrounds, the reported
     * tips, the reversion memory, the kept frames) reads this rather than `final > previous`.
     */
    inline bool windowCalledADart(DartBoardState previous, DartBoardState current, bool past_three)
    {
        return current > previous || (past_three && current == DartBoardState::DART_3);
    }

    /** turnaus#1793: whether score_processing publishes this window (an END, or a dart). */
    inline bool windowPublishes(DartBoardState previous, DartBoardState current, bool past_three)
    {
        return previous != current || windowCalledADart(previous, current, past_three);
    }

    /**
     * turnaus#1820: how close, in the reference camera's pixels, a first dart after a
     * reversion END must be published to a dart of the visit that END closed to be that
     * dart, still in the board, read again as it is pulled.
     *
     * WHERE THE FIGURE COMES FROM. Live on casual board 20 (2026-10-11 00:57) the two
     * re-reads were 0.0 px (S7: the same three lines, the same solve) and 9.2 px (the S4,
     * first solved from two lines at 15.6 mm sigma, re-solved from three) from the darts
     * they repeated, and 01:00:42/:44's pair were 0.0 and 0.0 px. 12 px is the census's
     * REREAD_PX (testers/i1789_truth_census.py, #1819), kept so the census and the hold
     * name the same darts. It is NOT what separates a re-read from a real dart thrown into
     * the same group: board 20's thrower lands 1.4..11.7 px from the visit before ten
     * times in the session (same camera), every one after an END that reconciled under the
     * CLEAN ceiling. The reversion END is that separation (RereadMemory::armed).
     */
    inline double rereadRadiusPx()
    {
        return 12.0;
    }

    /**
     * turnaus#1820: how long after the reversion END a repeat is still a pull rather than a
     * throw.
     *
     * WHERE THE FIGURE COMES FROM. Board 20's log, END to SCORE line. The four windows that
     * were only a dart being pulled came 2,174 ms (01:00:42 S16), 2,410 ms (00:57:23 S7),
     * 4,292 ms (01:00:44 T4) and 4,494 ms (00:57:25 S13) after their END. Two more repeats
     * followed a reversion END and were real throws the thrower corrected, whose windows
     * also held a late pull: 00:56:04's T13 (8.5 px, 7,276 ms; typed S7) and 00:59:20's
     * D20 (4.2 px, 7,840 ms; corrected to a miss). Holding those would lose the throw where
     * publishing them costs a correction, so the horizon sits between 4,494 and 7,276 ms.
     * Six points, one thrower: the figure is the log's, not a law.
     */
    inline long long rereadHorizonMs()
    {
        return 6000;
    }

    /** turnaus#1820: one dart as the board published it -- the reference camera and its pixel. */
    struct PublishedPixel
    {
        int camera = -1;
        cv::Point2f at = cv::Point2f(-1.0f, -1.0f);
    };

    /**
     * turnaus#1820: the distance from `at` to the nearest of `closed` published through
     * the SAME camera, or -1 when there is none. A position is a pixel in one camera's
     * image, so a dart published through another camera is not compared at all; a MISS
     * (camera -1 or a negative pixel) has no position and repeats nothing.
     */
    inline double nearestSameCameraPx(int camera, cv::Point2f at, const std::vector<PublishedPixel> &closed)
    {
        if (camera < 0 || at.x < 0.0f || at.y < 0.0f)
            return -1.0;
        double best = -1.0;
        for (const PublishedPixel &p : closed)
        {
            if (p.camera != camera || p.at.x < 0.0f || p.at.y < 0.0f)
                continue;
            const double d = std::hypot((double)(p.at.x - at.x), (double)(p.at.y - at.y));
            if (best < 0.0 || d < best)
                best = d;
        }
        return best;
    }

    /**
     * turnaus#1820: what the board remembers of the visit a takeout closed, to recognise
     * that visit's darts being pulled after the END.
     *
     * A reversion END (#1518) reconciles on a FALL, not on reaching the CLEAN ceiling, so
     * it can come with darts still in the board, and #1518 then adopts those frames as
     * the clean reference. Pulling a dart afterwards is a change against that reference
     * at the dart's own pixels, and the vote reads it as an arrival there. Only a
     * reversion END can adopt darts, so only a reversion END arms the hold; and a dart
     * still in the board is pulled before the next throw, so the hold is disarmed by the
     * first dart the next visit publishes.
     *
     *   published(camera, at)      a dart was published (MISS included): it joins this
     *                              visit, and a first dart after the END disarms the hold
     *   ended(by_reversion, ms)    an END at `ms`: this visit becomes the closed one, armed
     *                              or not
     */
    struct RereadMemory
    {
        std::vector<PublishedPixel> visit;  // the darts the open visit has published
        std::vector<PublishedPixel> closed; // the darts of the visit the last END closed
        bool armed = false;                 // that END was a reversion END, and nothing has published since
        long long ended_ms = -1;            // when it was

        void published(int camera, cv::Point2f at)
        {
            PublishedPixel p;
            p.camera = camera;
            p.at = at;
            visit.push_back(p);
            armed = false;
        }

        void ended(bool by_reversion, long long now_ms)
        {
            closed = visit;
            visit.clear();
            armed = by_reversion && !closed.empty();
            ended_ms = now_ms;
        }
    };

    /**
     * turnaus#1820: the rule. A dart is a re-read, held unpublished, when the hold is armed
     * (the last END reconciled by reversion and the new visit has published nothing), the
     * board is advancing from CLEAN, it comes within `horizon_ms` of that END, and it is
     * within `radius` of a dart of the closed visit through the same camera.
     */
    inline bool isARereadAfterTakeout(const RereadMemory &memory, DartBoardState previous, int camera,
                                      cv::Point2f at, long long now_ms, double radius = rereadRadiusPx(),
                                      long long horizon_ms = rereadHorizonMs())
    {
        if (!memory.armed || previous != DartBoardState::CLEAN || memory.ended_ms < 0)
            return false;
        const long long since = now_ms - memory.ended_ms;
        if (since < 0 || since >= horizon_ms)
            return false;
        const double d = nearestSameCameraPx(camera, at, memory.closed);
        return d >= 0.0 && d <= radius;
    }

    /**
     * turnaus#1781: the share of a camera's board above which a voting camera's FRESH
     * change is a body (the thrower's arm at a takeout), not a dart.
     *
     * WHERE THE FIGURE COMES FROM. The fixtures: OD_BODY_CENSUS=1 over the seven bakeoff
     * replays (capture clock, OD_WINDOW_UNIT=cycles, OD_SPIKE_THRESHOLD=0.006
     * OD_LONE_CAMERA=on; 2026-10-11) prints every camera that voted every called dart.
     * The largest fresh figure is 9.96% of a board -- rig-20260922 opening, window 1,
     * camera 1, 21,508 of 215,925 px, v1.2's S16 read against the calibration picture
     * that still holds the parked dart (#1514) -- and the next largest 7.67%; every
     * rig-20260918 and rig-20260929 dart is under 6.7%. The live arm: camera 2's
     * cumulative figure in the D11 window was 69,193 px (the END's CLEAN BY REVERSION
     * fell FROM it), 32.0% of the same rig's camera-2 board on rig-20260929 (215,989
     * px; the live log prints no board size). Less two of that camera's largest
     * fixture darts (12,692 px each) for the visit's darts already on the board, the
     * arm's fresh figure is at least 43,809 px, 20.3%. 15% is 1.5x the largest fixture
     * dart and three-quarters of the smallest the live arm can have been.
     *
     * Pure and inline (#1338's reason): testers/i1781_body_check.cpp holds the rule
     * without building the detector.
     */
    inline double bodySizedFreshSharePercent()
    {
        return 15.0;
    }

    inline bool freshFigureIsBodySized(int fresh_pixels, int board_pixels,
                                       double share_percent = bodySizedFreshSharePercent())
    {
        return board_pixels > 0 && fresh_pixels > 0 &&
               100.0 * (double)fresh_pixels / (double)board_pixels >= share_percent;
    }

    /**
     * turnaus#1781: how soon after a takeout that reconciled with a reversion vote an
     * advance is refused. The live phantom S2's vote came ~976 ms after the END's (the
     * publications at 16:24:29.105 and 16:24:30.081): the arm withdrawing against a
     * clean reference re-based while it was still in frame (#1691's shape). On the
     * fixtures the soonest first dart after a reversion END is 4,433 ms (rig-20260922
     * opening, OD_BODY_CENSUS=1; rig-20260918, rig-20260929 and rig-20260922 dev
     * reconcile no takeout by reversion at all). 1,500 ms is half again the live
     * phantom's distance and a third of the soonest fixture dart's.
     */
    inline long long arrivalAfterTakeoutHorizonMs()
    {
        return 1500;
    }

    inline bool arrivalFollowsTakeoutTooSoon(long long ms_since_takeout,
                                             long long horizon_ms = arrivalAfterTakeoutHorizonMs())
    {
        return ms_since_takeout >= 0 && ms_since_takeout < horizon_ms;
    }

    /**
     * #1535: a camera re-reporting, for a NEW dart, a pixel it already reported for an
     * earlier dart of the same visit is not a second witness.
     *
     * THE FACT THIS SEPARATES ON, measured by #1505's edge probe on rig-20260918,
     * visit 4's third dart (thrown OFF the board, published S20@0.9): camera 2's "new"
     * tip (847,336) sat 2.2 px from the tip it had already reported for the PREVIOUS
     * dart (848,338) -- the thrown 20 standing in the board -- while the fresh diff's
     * actual change was 82 px away across empty space (I1492TIP tipGap=82, tipPiece=1:
     * the chosen point was not on the piece the centroid was measured from). That
     * re-report plus camera 3's parallax projection made two agreeing "S20" strings,
     * and agreement earned 0.9 for a dart that missed the board. #1505 measured that
     * no vote rule can reach this -- the vote contains no MISS reading to admit -- so
     * the separator is which OBJECT the tip was found on, asked here, in the tip
     * machinery, per camera.
     *
     * A candidate is a re-report when BOTH hold:
     *
     *   - it lies within `near_px` of a tip this camera already reported for an
     *     earlier dart of this visit (the vote accepted that dart; the memory is reset
     *     at the reconciled CLEAN, beside the working backgrounds -- #1349's point);
     *   - the fresh figure's own nearest point is at least `elsewhere_px` away
     *     (`gap_to_fresh_figure` is the distance from the candidate to the nearest
     *     point of the largest fresh-diff contour -- I1492TIP's tipGap, computed on
     *     every call since #1535).
     *
     * The second condition is what spares a LEGITIMATE dart landing beside an earlier
     * one: its tip is a point of the fresh figure (or of a shaft fragment a few px
     * from it), so its gap stays small whatever its distance to the earlier tip.
     *
     * THE CENSUS the thresholds are read off (2026-09-23, #1505's probe over the whole
     * of rig-20260918, one binary under OD_TIP_IDENTITY=off so it is the UNGUARDED
     * machinery being measured; 48 accepted tips over 19 darts, 28 of them with an
     * earlier same-camera tip in their visit -- the denominator every number below is
     * out of):
     *
     *   - TWO tips sit within 3 px of an earlier one: the needle (v4 d3 cam 2,
     *     near=2.2 px, gap=82 px) and v5 d2 cam 3 (near=2.2 px, gap=128 px) -- the
     *     same mechanism on a scored dart: camera 3 re-reported its previous dart's
     *     tip as "T15" while cameras 1 and 2 agreed on the correct S4, so removing it
     *     changes nothing and the phantom witness is silenced there too.
     *   - The closest LEGITIMATE adjacency is 27.8 px (v5 d3 cam 1), and its gap is
     *     13 px -- inside the fresh figure's own fragment reach, spared by both
     *     conditions at once. The next is 52.8 px (v7 d2 cam 1), gap 0.
     *   - Legitimate first-tip gaps run 0..172 px (fragmenting darts carry their tip
     *     on a shaft fragment), which is why the gap alone decides nothing and the
     *     rule is an AND.
     *
     * So `near_px = 12` sits 5.5x above the needles' 2.2 and 2.3x under the closest
     * legitimate adjacency (27.8 px), and `elsewhere_px = 40` sits 3.1x above the
     * largest gap on any near-adjacent legitimate tip (13 px) and 2.05x under the
     * nearer needle's 82. A tip must fail BOTH margins at once to be eaten.
     * mocks/rig-20260922 is measured at the outcome level (the probe cannot calibrate
     * its cameras -- its fixed seek lands on the parked dart; the real binary
     * calibrates 3/3), whole clip, one binary, both modes, 2026-09-24: the rule fires
     * exactly ONCE, on visit 1's second dart, and the firing is the mechanism itself.
     * Camera 2's "new" tip at (718,212) sat 1 px from the tip it had already reported
     * for the first dart, with the fresh change 42 px away, and it was the LONE
     * witness: under OD_TIP_IDENTITY=off that re-report published S5@0.7 against a
     * thrown 16 (the ghost of dart 1 as camera 2 read it), and under the rule the
     * dart publishes MISS with no witness at all -- a wrong score became an honest
     * abstention. Everything else is line-identical: 21 of 24 detected both ways,
     * 1 of 21 correct both ways, no correct dart moved.
     *
     * LIMITATION, stated with its mechanism: a dart landing with its tip within 12 px
     * of an earlier tip AND detected only as change 40+ px away (its own tip region
     * swallowed by the earlier dart's silhouette in the fresh diff) would abstain that
     * camera honestly -- one witness fewer, not a phantom. True tip-on-tip adjacency
     * is indistinguishable from a re-report by construction here, because the fresh
     * diff cannot show change where the scene did not change; the cross-camera
     * geometry that could tell them apart is #1512's landing-position intersection.
     *
     * Pure and inline for the reason whyNoEventIsPossible is (#1338): a tester holds
     * the rule without building the detector, and testers/i1535_identity_check.cpp
     * does. OD_TIP_IDENTITY=off restores the unguarded machinery (tipIdentityIsOff
     * below), so before/after is two runs of one binary.
     */
    inline bool isAReReportOfAnEarlierTip(cv::Point2f candidate,
                                          double gap_to_fresh_figure,
                                          const std::vector<cv::Point2f> &tips_reported_this_visit,
                                          double near_px = 12.0,
                                          double elsewhere_px = 40.0)
    {
        if (gap_to_fresh_figure < elsewhere_px)
        {
            return false;
        }
        for (const cv::Point2f &earlier : tips_reported_this_visit)
        {
            if (norm(candidate - earlier) <= near_px)
            {
                return true;
            }
        }
        return false;
    }

    /**
     * #1648: a camera whose only "new" tip this window is a #1535 re-report of an
     * earlier dart of this visit, AND whose cumulative board figure FELL since the last
     * completed window, is watching a departure -- it votes CLEAN instead of advancing.
     *
     * Why it is needed. Until the first reconciled CLEAN adopts a scene, the clean
     * reference is the calibration picture, and on mocks/rig-20260922's opening window
     * that picture holds the parked 8 (v1.1). Once the thrower pulls every dart the
     * empty board differs from that reference by the 8's hole, so the takeout's
     * cumulative figure falls by LESS than a dart (camera 2 1373->1208, camera 3
     * 1256->1084, against ceilings of 215 and 182 px) and #1518's readsAsReversion
     * cannot fire. The fresh change against the working background is the departing
     * darts, and the tip picked from it sits on a tip already reported this visit:
     * camera 2's (718,212), 1 px from S16's. #1535 made that camera abstain from
     * SCORING; it still voted DART_n+1, so the board advanced on a takeout.
     *
     * Both halves are required. A re-report alone is also what #1535 measured on
     * arrivals (a new dart whose fresh figure the tip was not picked from); on an
     * arrival the cumulative figure RISES by the new dart. A fall alone is what the
     * thrower's shadow does to one camera. The pair, and still only a vote: the quorum
     * decides, as it does for every other candidate.
     *
     * `previous_pixels < 0` (no completed window yet) is no verdict. The default since
     * #1662; OD_TAKEOUT_REREPORT=off pins it off (takeoutReReportIsDeparture below). Pure and inline
     * for the reason readsAsReversion is: testers/i1648_check.cpp holds it.
     */
    inline bool readsAsReReportedDeparture(bool tip_is_rereport, int previous_pixels, int current_pixels)
    {
        return tip_is_rereport && previous_pixels >= 0 && current_pixels < previous_pixels;
    }

    /**
     * #1678: a camera whose fresh change stayed UNDER the floor in the scoring area but
     * whose figure is nonetheless a dart standing in the board -- a corroborating witness
     * for another camera's advance, never an advance of its own.
     *
     * Why it is needed. The floor is counted inside the scoring area (#1354's deciding
     * share; Region::mask ends at the double's outer wire) while the tip is searched in
     * the physical board (#1364's tip_mask). A dart in the double ring, seen side-on,
     * puts its tip inside the scoring area and its shaft and flight OUTSIDE it, so the
     * deciding share clips all but the tip. Measured on mocks/rig-20260929 window #21
     * (visit 6's D5, capture clock, opening window, OD_LONE_CENSUS=1): camera 1's fresh
     * change is 179 px in the scoring area against a 215 px floor but 1,366 px in the
     * physical board, and that figure fits a valid axis (extent 110 px) with its tip at
     * (377,264) -- the very tip camera 1 reports for D5 one window later. Camera 3:
     * 61 px against 183, 671 px physical, a valid axis, tip at (869,159). Camera 2 alone
     * cleared the floor (12,692 px), so the vote refused the dart and the next window
     * published D5 and S1 as one dart.
     *
     * Every clause is load-bearing against the one negative control the three fixtures
     * hold, mocks/rig-20260918's visit 4.3, a MISS with the same vote shape (one camera
     * up, none CLEAN, window #15): camera 1 cleared the floor with 12,419 px while
     * cameras 2 and 3 have 274 and 723 px in the physical board, both fitting valid short
     * axes -- and 0 px in the scoring area, so neither's tip is in it. A dart that scores
     * has its tip in the scoring area from every camera's view; what that miss left in
     * the physical ring does not. So:
     *   - the physical-board figure clears the SAME floor the deciding share uses;
     *   - it fits a valid shaft axis (the gated fit, #1511) and yields a tip;
     *   - that tip lies inside the scoring area;
     *   - and it is not a #1535 re-report of a tip this camera already gave this visit.
     * The vote then counts it toward the quorum only beside at least one camera that
     * cleared the floor itself, and only when no voter reads CLEAN (the caller's half).
     * Pure and inline for the reason readsAsReversion is.
     */
    inline bool subFloorCameraCorroborates(int fresh_physical_pixels, int floor_pixels, bool axis_valid,
                                           bool tip_found, bool tip_in_scoring_area, bool tip_is_rereport)
    {
        return fresh_physical_pixels >= floor_pixels && floor_pixels > 0 && axis_valid && tip_found &&
               tip_in_scoring_area && !tip_is_rereport;
    }

    // #1648's switch, the default since #1662: readsAsReReportedDeparture is on unless
    // OD_TAKEOUT_REREPORT=off, the pin that leaves the #1535 re-report an abstention that
    // still votes to advance (the default before #1662). "departure", the old opt-in, means
    // the default. Defined in dart_processing.cpp, where the other pins live.
    bool takeoutReReportIsDeparture();

    // #1535 falsification switch: OD_TIP_IDENTITY=off restores the tip machinery as it
    // was before #1535 -- every found tip is reported, a re-report of an earlier dart's
    // pixel included. Defined in dart_processing.cpp, where the other pins live.
    bool tipIdentityIsOff();

    // Per-camera detection result
    struct CameraDetectionResult
    {
        DartBoardState detected_state = DartBoardState::CLEAN;
        int total_changed_pixels = 0;              // Total changed pixels
        double change_ratio = 0.0;                 // Percentage of changed pixels
        int total_pixels = 0;                      // Total pixels in frame
        // #1687: the three figures above are over the camera's window area rather than
        // the frame (dart_processing.cpp, windowCrop). Only a camera with a fitted board
        // is cropped, and such a camera decides on its board counts, not on these.
        bool figure_on_area = false;
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
        // #1678: the same fresh change counted inside the PHYSICAL board (Region::tip_mask,
        // the mask the tip is searched in) -- what a rim dart leaves once its shaft and
        // flight are no longer clipped at the double wire. -1 where not computed.
        int fresh_physical_pixels = -1;
        // #1678: for a camera whose fresh change stayed UNDER the floor, the tip and axis
        // its sub-floor figure would have yielded -- observed only (OD_LONE_CENSUS=1 or
        // OD_LONE_CAMERA=on), never published: `axis` above keeps its refusal.
        bool sub_floor_observed = false;
        bool sub_floor_tip_found = false;
        Point2f sub_floor_tip = Point2f(-1, -1);
        shaft_axis::AxisObservation sub_floor_axis;
        // #1678: subFloorCameraCorroborates' verdict on the above, computed wherever the
        // observation is; the vote reads it only under OD_LONE_CAMERA=on.
        bool sub_floor_corroborates = false;
        Point2f tip_position = Point2f(-1, -1);    // Position of dart tip if found
        Point2f center_position = Point2f(-1, -1); // Center of biggest dart shape
        bool tip_found = false;                    // Was tip found in this frame
        // #1511: the new dart's fitted shaft axis for this camera, or a refusal saying
        // by name why this window's fresh figure holds no usable line. #1555: THIS IS
        // NOW WHAT THE BOARD PUBLISHES FROM -- these axes, transported to the board
        // plane and intersected there (#1512), name the dart wherever the solve stands,
        // and `tip_position` publishes only where the solver refused by name. The line
        // here read "nothing in this repository reads it to decide anything yet" for
        // four issues, and it stopped being true when #1555's census chose the geometric
        // path. A VALID AXIS IS STILL DISTINCT FROM A VALID
        // VISIBLE TIP: an occluded tip with a readable shaft is a valid axis with
        // `tip_found == false`, never a fabricated endpoint, and a found tip on a
        // figure that is not a line is `tip_found` with the axis refusing. The line is
        // in this camera's own image pixels, the space board_model::fitBoardToCamera
        // consumes; shaft_axis.hpp owns the coordinate and distortion statement.
        shaft_axis::AxisObservation axis;
        bool frame_available = true;               // #798: did this camera contribute any frame to the window
        // #1354: this camera has no fitted board while another camera does, so it has no
        // denominator to decide with and it abstains from the vote -- answering from the
        // frame instead is how the rig's camera 1 voted DART_1 on the thrower's shoes,
        // six windows out of six (#1345).
        bool abstained_no_board = false;
        // #1689: this camera's fresh change cleared the floor only out to the rim, so it
        // voted the arrival and offered no tip or axis ("rim only").
        bool rim_only = false;
        // turnaus#1821: this camera voted the arrival and was counted toward the scoring-area
        // quorum (#1707's moves_up_in_scoring), by its own figure or as a #1678 corroborator.
        bool cleared_scoring_floor = false;
        // turnaus#1793: this camera was at DART_3 and its fresh figure cleared the floor
        // (OD_PAST_THREE=on only): an up vote although its candidate equals the board's.
        bool arrived_past_three = false;
    };

    // Result of dart state detection
    struct DartStateResult
    {
        DartBoardState current_state = DartBoardState::CLEAN;  // Current dartboard state
        DartBoardState previous_state = DartBoardState::CLEAN; // Previous dartboard state
        bool state_changed = false;                            // Did the state change this frame
        int confidence_frames = 0;                             // How many frames we've been confident in this state
        vector<CameraDetectionResult> camera_results;          // Results from each camera
        // #1707: the board advanced only because rim-only cameras voted: the cameras that
        // cleared the floor in the scoring area were fewer than the quorum. Such a dart is
        // at or beyond the double ring on every camera but the ones that carried it.
        bool rim_carried = false;
        // turnaus#1793: the vote called a dart on a board already at DART_3, so
        // current_state == previous_state and the window still publishes (windowPublishes).
        bool past_three = false;
        // turnaus#1820: this window reconciled CLEAN with at least one reversion vote in it
        // (#1518, or #1552's memory of one): a takeout's END whose cameras were still over
        // their CLEAN ceiling when #1518 adopted their frames as clean.
        bool reversion_end = false;
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
                       " (" + figure + (camera_results[i].figure_on_area ? " of its window area" : "") + ")";
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

    /**
     * turnaus#1820: take back the dart `called` advanced the board by, after the scorer
     * refused to publish it (OD_REREAD_HOLD). The board's state goes back to the state
     * the window opened on, and the tips the advance recorded for #1535 are taken off the
     * record. The working backgrounds stay on this window's frames, as #1690's held
     * re-base leaves them: what the refused window saw is on the board now (here: a dart
     * that is no longer in it), so it must not be the next dart's fresh figure.
     */
    void withdrawCalledDart(const DartStateResult &called);

} // namespace dart_processing