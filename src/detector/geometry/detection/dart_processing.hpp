#pragma once

#include <opencv2/opencv.hpp>
#include <cstdio>
#include <string>
#include <vector>

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
        // account below can name the number the code really read. The VALUE is unchanged.
        // Its DENOMINATOR is each camera's whole frame -- not the board -- which is
        // #1345's open question one stage downstream of #1339: a threshold and its
        // denominator move together, so whoever moves either states both.
        double change_percent_threshold = 0.22; // % of a camera's own frame that must differ from the background to call a dart
    };

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
        Point2f tip_position = Point2f(-1, -1);    // Position of dart tip if found
        Point2f center_position = Point2f(-1, -1); // Center of biggest dart shape
        bool tip_found = false;                    // Was tip found in this frame
        bool frame_available = true;               // #798: did this camera contribute any frame to the window
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
     * the two counts beside the 2 either of them needed, and the threshold the figures
     * are read against -- named from DartParams, not retyped, so a moved constant moves
     * this sentence with it.
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
        for (const CameraDetectionResult &r : camera_results)
        {
            if (r.frame_available && r.board_pixels > 0)
            {
                board_share_known = true;
            }
        }
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
               " read CLEAN, either takes 2 to move the board, so it stays " +
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