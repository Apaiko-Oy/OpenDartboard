// #1518: the CLEAN reference must be able to recover from a scene that changed after
// calibration, and a dart-sized simultaneous fall of the cumulative figure must read as
// a takeout.
//
// The defect this measures is #1514's: the CLEAN test compared every window against the
// CALIBRATION background for ever, so a dart parked in the board at calibration
// (mocks/rig-20260922, frame 0 of every camera) left its silhouette in every later
// cumulative diff once it was pulled -- no camera ever read CLEAN again, no takeout
// reconciled, and the board wedged at DART_3 for 26 windows.
//
// Two halves, one binary:
//
//   ./i1518_reversion_check adopting      the tree's rule: the takeout is recognised as
//                                         a reversion, the reference adopts the scene at
//                                         the reconciled CLEAN, and the round after it
//                                         is ordinary
//   OD_CLEAN_REFERENCE=calibration \
//   ./i1518_reversion_check calibration   the pre-#1518 rule, restored by the pin: the
//                                         takeout reads as a THIRD dart and the board
//                                         wedges at DART_3 -- #1514's stall, reproduced
//                                         on this binary
//
// testers/i1518_check.sh runs both, then the mutation proof (the pinned binary asked the
// tree's questions), with the prediction stated there before the run.
//
// It drives processDartState itself through one synthetic session, three cameras of
// 200x200 frames with FITTED boards (the reversion vote only exists where a board is
// fitted, because it is a statement about the board's own pixels), one window per call:
//
//   calibration   backgrounds hold a parked dart P            (the poisoned bootstrap)
//   window 1      P has been pulled. Its silhouette RISES from nothing, so there is no
//                 fall to read and the pull publishes as a phantom dart -- under both
//                 rules. That is the phantom-pull publication #1518's report DEFERS by
//                 name; this check asserts it so the deferral is a measured fact rather
//                 than a hope, and so the day somebody repairs it this line fails and
//                 gets updated on purpose.
//   window 2      dart A lands                                -> one more dart, tip in A
//   window 3      A is taken out. The board is still over the CLEAN ceiling (P's
//                 silhouette), but the figure FELL by a dart's worth on every camera at
//                 once -- the reversion. The old rule reads the same window as a THIRD
//                 dart.
//   window 4      dart B lands -> the adopted reference makes it an ordinary first dart
//                 of a new round; the old rule is wedged at DART_3.
//
// Plus readsAsReversion alone, at its own boundaries: no previous window is no verdict,
// a fall smaller than the ceiling is none, a board under the ceiling is the ordinary
// CLEAN branch's case and not this one, a rise is nothing, and a camera with no board
// (ceiling 0) can never revert.
//
//   g++ -std=c++17 -O1 -I src -I src/utils -o i1518_reversion_check \
//       testers/i1518_reversion_check.cpp \
//       src/detector/geometry/detection/dart_processing.cpp $(pkg-config --cflags --libs opencv4)

#include <iostream>
#include <string>
#include <vector>
#include <opencv2/opencv.hpp>

#include "detector/geometry/detection/dart_processing.hpp"

using namespace cv;
using namespace dart_processing;

static int failures = 0;

static void say(bool ok, const std::string &what)
{
    std::cout << (ok ? "OK   " : "FAIL ") << what << std::endl;
    if (!ok)
    {
        failures++;
    }
}

// All three rectangles sit well inside the fitted board (an ellipse of 160x160 centred
// at 100,100, ~20,100 px, so the CLEAN ceiling is ~20 px). Each is ~576 px before the
// pipeline's morphology fattens it -- unmissable to the cumulative test, which is the
// point.
static const Rect kParkedP(88, 40, 24, 24);
static const Rect kDartA(140, 90, 24, 24);
static const Rect kDartB(60, 130, 24, 24);

static Mat plainBoard()
{
    return Mat::zeros(200, 200, CV_8UC3);
}

static Mat sceneWith(const std::vector<Rect> &darts)
{
    Mat frame = plainBoard();
    for (const Rect &dart : darts)
    {
        rectangle(frame, dart, Scalar(255, 255, 255), FILLED);
    }
    return frame;
}

static bool tipNear(const Point2f &tip, const Rect &dart, const std::string &what)
{
    const int margin = 14; // morphology fattens the blob a few pixels
    const bool inside = tip.x >= dart.x - margin && tip.x <= dart.x + dart.width + margin &&
                        tip.y >= dart.y - margin && tip.y <= dart.y + dart.height + margin;
    if (!inside)
    {
        std::cout << "     " << what << ": tip at (" << (int)tip.x << "," << (int)tip.y
                  << ") is not near (" << dart.x << "," << dart.y << ")+" << dart.width
                  << "x" << dart.height << std::endl;
    }
    return inside;
}

int main(int argc, char **argv)
{
    const std::string mode = argc > 1 ? argv[1] : "adopting";
    if (mode != "adopting" && mode != "calibration")
    {
        std::cout << "FAIL mode '" << mode << "' is not one this check knows (adopting, calibration)"
                  << std::endl;
        return 1;
    }
    const bool adopting = mode == "adopting";

    // ---- the rule alone, at its boundaries --------------------------------------------
    say(!readsAsReversion(-1, 500, 100),
        "reversion: no previous window is no verdict, whatever the figures");
    say(readsAsReversion(1000, 500, 100),
        "reversion: a dart-sized fall on a board still over the ceiling is one");
    say(!readsAsReversion(1000, 950, 100),
        "reversion: a fall smaller than the ceiling is not");
    say(!readsAsReversion(1000, 50, 100),
        "reversion: a board UNDER the ceiling is the ordinary CLEAN branch's case, not this one");
    say(!readsAsReversion(100, 500, 100),
        "reversion: a rise is nothing");
    say(!readsAsReversion(500, 400, 0),
        "reversion: a camera with no fitted board (ceiling 0) can never revert");

    DartParams params;
    params.stability_frames = 1; // one call is one window

    // The poisoned bootstrap: the calibration backgrounds hold the parked dart.
    const Mat poisoned = sceneWith({kParkedP});
    const std::vector<Mat> backgrounds = {poisoned.clone(), poisoned.clone(), poisoned.clone()};

    // Fitted boards, because the reversion vote is a statement about a board's own
    // pixels and a camera with no fitted board never makes one.
    std::vector<motion_processing::BoardExtent> boards(3);
    for (auto &extent : boards)
    {
        extent.known = true;
        extent.edge = RotatedRect(Point2f(100, 100), Size2f(160, 160), 0);
    }

    // ---- window 1: the parked dart is pulled ------------------------------------------
    {
        const Mat pulled = plainBoard();
        const std::vector<Mat> frames = {pulled.clone(), pulled.clone(), pulled.clone()};
        DartStateResult r = processDartState(frames, backgrounds, boards, true, false, params);
        say(r.previous_state == DartBoardState::CLEAN && r.current_state == DartBoardState::DART_1,
            std::string("window 1: the pull publishes as a phantom dart") +
                (adopting ? " -- the silhouette RISES from nothing, so there is no fall to read; "
                            "the phantom is DEFERRED by name in #1518's report"
                          : " (pinned), as it did before #1518"));
    }

    // ---- window 2: dart A lands --------------------------------------------------------
    {
        const Mat seen = sceneWith({kDartA});
        const std::vector<Mat> frames = {seen.clone(), seen.clone(), seen.clone()};
        DartStateResult r = processDartState(frames, backgrounds, boards, true, false, params);
        say(r.previous_state == DartBoardState::DART_1 && r.current_state == DartBoardState::DART_2,
            "window 2: dart A advances the board -- a RISE is never a reversion");
        say(r.camera_results[1].tip_found && tipNear(r.camera_results[1].tip_position, kDartA, "window 2, camera 2"),
            "window 2: camera 2's tip is in dart A, the newest change");
    }

    // ---- window 3: dart A is taken out -------------------------------------------------
    // The window the old rule can never reconcile: against the calibration background
    // the empty board still shows P's silhouette, over the ceiling for ever.
    {
        const Mat pulled = plainBoard();
        const std::vector<Mat> frames = {pulled.clone(), pulled.clone(), pulled.clone()};
        DartStateResult r = processDartState(frames, backgrounds, boards, true, false, params);
        if (adopting)
        {
            say(r.previous_state == DartBoardState::DART_2 && r.current_state == DartBoardState::CLEAN,
                "window 3: the takeout reconciles CLEAN -- the figure fell by a dart's worth on a "
                "quorum of cameras while still over the ceiling");
            say(r.camera_results[0].detected_state == DartBoardState::CLEAN &&
                    r.camera_results[1].detected_state == DartBoardState::CLEAN &&
                    r.camera_results[2].detected_state == DartBoardState::CLEAN,
                "window 3: every camera's candidate was the reversion's CLEAN");
        }
        else
        {
            say(r.current_state == DartBoardState::DART_3,
                "window 3 (pinned): the takeout reads as a THIRD dart -- fresh change over a "
                "working background that still held A, with CLEAN arithmetically unreachable");
        }
    }

    // ---- window 4: a new round, dart B alone -------------------------------------------
    {
        const Mat seen = sceneWith({kDartB});
        const std::vector<Mat> frames = {seen.clone(), seen.clone(), seen.clone()};
        DartStateResult r = processDartState(frames, backgrounds, boards, true, false, params);
        if (adopting)
        {
            say(r.previous_state == DartBoardState::CLEAN && r.current_state == DartBoardState::DART_1,
                "window 4: the next round is ordinary, CLEAN -> DART_1 -- the adopted reference "
                "holds the scene, so B is a first dart and not a fourth");
            say(r.camera_results[1].tip_found && tipNear(r.camera_results[1].tip_position, kDartB, "window 4, camera 2"),
                "window 4: camera 2's tip is in dart B, read against the adopted reference");
        }
        else
        {
            say(r.current_state == DartBoardState::DART_3,
                "window 4 (pinned): the board is wedged at DART_3 -- #1514's stall, on this binary");
        }
    }

    std::cout << (failures == 0 ? "ALL OK" : std::to_string(failures) + " FAILURES") << std::endl;
    return failures == 0 ? 0 : 1;
}
