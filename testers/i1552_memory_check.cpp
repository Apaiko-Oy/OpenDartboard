// #1552: a takeout whose cameras fall in DIFFERENT windows must still reconcile CLEAN,
// or the visit boundary is silently merged and every per-visit census after it compares
// darts across the boundary.
//
// The defect this measures is the residual #1518 left: its reversion vote lives and
// dies in its own window, and at the FIRST takeout of mocks/rig-20260922 -- the only
// one judged against the dirty calibration reference -- each camera's figure falls to
// its own residue at its own moment. Measured on the whole-clip runs of 2026-09-24
// (both calibration windows, one binary): no window ever held the two CLEANs the
// quorum needs, the takeout's own motion won a TIP-LESS advance in between (the
// phantom MISS), and the boundary between thrown visits 1 and 2 produced no END. The
// account, with the numbers, is on reversionMemoryWindows() in dart_processing.hpp.
//
// Two halves, one binary:
//
//   ./i1552_memory_check remembering       the tree's rule: the reversion vote is
//                                          remembered into the next two windows, so the
//                                          second camera's later fall completes the
//                                          quorum and the takeout reconciles
//   OD_REVERSION_MEMORY=off \
//   ./i1552_memory_check forgetting        the pre-#1552 rule, restored by the pin: the
//                                          split reversions never meet, the boundary is
//                                          merged, and the next visit's dart lands as a
//                                          THIRD dart of the same visit
//
// testers/i1552_check.sh runs both, then the mutation proof (the pinned binary asked
// the tree's questions), with the prediction stated there before the run.
//
// It drives processDartState itself through one synthetic session, three cameras of
// 200x200 frames with FITTED boards, one window per call, i1518_reversion_check's
// geometry (the parked dart P in every calibration background is what keeps every
// camera's residue OVER the CLEAN ceiling, so the ordinary under-the-ceiling CLEAN
// branch is out of reach -- the pre-first-adoption regime the real boundary is lost
// in):
//
//   window 1      dart A stands (P was pulled off-screen)  -> DART_1, tip in A
//   window 2      camera 1 sees the empty board: its figure FALLS dart-sized while
//                 still over the ceiling (P's silhouette) -- a reversion, one camera.
//                 Cameras 2 and 3 see the retriever: a blob over the 20,000 px contour
//                 cap, so they advance the board WITHOUT any tip -- the phantom MISS
//                 shape, under both rules.
//   window 3      camera 2's view empties one window later -- the measured split. Its
//                 reversion plus camera 1's REMEMBERED one is the quorum: the tree
//                 rule reconciles CLEAN here (the END); the forgetting rule counts one
//                 CLEAN, stays DART_2, and the boundary is merged.
//   window 4      dart B lands -> the tree rule reads an ordinary first dart of a new
//                 visit; the forgetting rule swallows B into the merged visit as a
//                 third dart.
//
// Plus the pure rules alone, at their boundaries: the memory horizon is the measured
// 2, a camera votes CLEAN by candidate or by live memory and by nothing else, and an
// advance clears the memory exactly when it carried a tip.
//
//   g++ -std=c++17 -O1 -I src -I src/utils -o i1552_memory_check \
//       testers/i1552_memory_check.cpp \
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

// P and A are i1518_reversion_check's rectangles, kept because that check already
// proves this geometry yields a tip inside A against the poisoned background. All
// three darts sit inside the fitted board (the 160x160 ellipse at 100,100; the CLEAN
// ceiling is ~20 px of its ~20,100). The arm covers the whole board -- and every dart
// position -- so its fresh figure is ONE contour over the 20,000 px cap: a state
// advance with no tip, which is what the real retriever's blob measures as.
static const Rect kParkedP(88, 40, 24, 24);
static const Rect kDartA(140, 90, 24, 24);
static const Rect kDartB(60, 130, 24, 24);
static const Rect kArm(5, 5, 190, 190);

static Mat plainBoard()
{
    return Mat::zeros(200, 200, CV_8UC3);
}

static Mat sceneWith(const std::vector<Rect> &shapes)
{
    Mat frame = plainBoard();
    for (const Rect &shape : shapes)
    {
        rectangle(frame, shape, Scalar(255, 255, 255), FILLED);
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

static bool noTipAnywhere(const DartStateResult &r)
{
    for (const CameraDetectionResult &cam : r.camera_results)
    {
        if (cam.tip_found)
        {
            return false;
        }
    }
    return true;
}

int main(int argc, char **argv)
{
    const std::string mode = argc > 1 ? argv[1] : "remembering";
    if (mode != "remembering" && mode != "forgetting")
    {
        std::cout << "FAIL mode '" << mode << "' is not one this check knows (remembering, forgetting)"
                  << std::endl;
        return 1;
    }
    const bool remembering = mode == "remembering";

    // ---- the pure rules alone, at their boundaries --------------------------------------
    say(reversionMemoryWindows() == 2,
        "pure: the memory horizon is 2 windows -- the measured splits are 1 (opening window) "
        "and 2 (dev window), and no longer");
    say(votesCleanThisWindow(DartBoardState::CLEAN, 0),
        "pure: a CLEAN candidate votes CLEAN with no memory at all");
    say(!votesCleanThisWindow(DartBoardState::DART_2, 0),
        "pure: a non-CLEAN candidate with no memory is not a CLEAN vote");
    say(votesCleanThisWindow(DartBoardState::DART_2, 1),
        "pure: live memory outranks the camera's own non-CLEAN candidate -- the candidate "
        "is the retriever or the next visit's first dart, the evidence the boundary is lost to");
    say(votesCleanThisWindow(DartBoardState::DART_3, 2),
        "pure: memory at full horizon counts the same way");
    say(advanceClearsReversionMemory(true),
        "pure: an advance that carried a tip is a dart really called, and clears the memory");
    say(!advanceClearsReversionMemory(false),
        "pure: a TIP-LESS advance is the takeout's own motion and does not clear it -- the "
        "phantom MISS carried zero tips in both measured runs");

    DartParams params;
    params.stability_frames = 1; // one call is one window

    // The poisoned bootstrap: every calibration background holds the parked dart, so
    // every camera's settled residue stays OVER the CLEAN ceiling for the life of the
    // pre-adoption regime -- the reversion vote is the only CLEAN either rule can reach.
    const Mat poisoned = sceneWith({kParkedP});
    const std::vector<Mat> backgrounds = {poisoned.clone(), poisoned.clone(), poisoned.clone()};

    std::vector<motion_processing::BoardExtent> boards(3);
    for (auto &extent : boards)
    {
        extent.known = true;
        extent.edge = RotatedRect(Point2f(100, 100), Size2f(160, 160), 0);
    }

    // ---- window 1: dart A stands ---------------------------------------------------------
    {
        const Mat seen = sceneWith({kDartA});
        const std::vector<Mat> frames = {seen.clone(), seen.clone(), seen.clone()};
        DartStateResult r = processDartState(frames, backgrounds, boards, true, false, params);
        say(r.previous_state == DartBoardState::CLEAN && r.current_state == DartBoardState::DART_1,
            "window 1: dart A advances the board");
        say(r.camera_results[1].tip_found && tipNear(r.camera_results[1].tip_position, kDartA, "window 1, camera 2"),
            "window 1: camera 2's tip is in dart A");
    }

    // ---- window 2: the split begins ------------------------------------------------------
    // Camera 1's view empties: a dart-sized fall while still over the ceiling -- one
    // reversion. Cameras 2 and 3 hold the retriever, a single contour over the 20,000 px
    // cap: they advance the board and no camera has a tip -- the phantom, under both
    // rules, and the window the memory must survive.
    {
        const Mat emptied = plainBoard();
        const Mat retriever = sceneWith({kArm});
        const std::vector<Mat> frames = {emptied.clone(), retriever.clone(), retriever.clone()};
        DartStateResult r = processDartState(frames, backgrounds, boards, true, false, params);
        say(r.previous_state == DartBoardState::DART_1 && r.current_state == DartBoardState::DART_2,
            "window 2: the takeout's own motion wins a 2-1 advance over camera 1's lone "
            "reversion -- the phantom, under both rules");
        say(r.camera_results[0].detected_state == DartBoardState::CLEAN,
            "window 2: camera 1's candidate was the reversion's CLEAN");
        say(noTipAnywhere(r),
            "window 2: the advance carried NO tip on any camera -- the retriever's blob is "
            "over the contour cap, which is why a tip-less advance must not clear the memory");
    }

    // ---- window 3: the second camera falls, one window later -----------------------------
    {
        const Mat emptied = plainBoard();
        const Mat retriever = sceneWith({kArm});
        const std::vector<Mat> frames = {emptied.clone(), emptied.clone(), retriever.clone()};
        DartStateResult r = processDartState(frames, backgrounds, boards, true, false, params);
        say(r.camera_results[1].detected_state == DartBoardState::CLEAN &&
                r.camera_results[0].detected_state != DartBoardState::CLEAN,
            "window 3: only camera 2's CANDIDATE is CLEAN -- whatever reconciles here is "
            "carried by memory, not by a second simultaneous fall");
        if (remembering)
        {
            say(r.previous_state == DartBoardState::DART_2 && r.current_state == DartBoardState::CLEAN,
                "window 3: camera 2's reversion plus camera 1's REMEMBERED one is the quorum -- "
                "the takeout reconciles and the END is published where the boundary really is");
        }
        else
        {
            say(r.current_state == DartBoardState::DART_2,
                "window 3 (pinned): one CLEAN vote, quorum never met -- the boundary is merged, "
                "which is issue #1552's whole subject");
        }
    }

    // ---- window 4: the next visit's dart B -----------------------------------------------
    {
        const Mat seen = sceneWith({kDartB});
        const std::vector<Mat> frames = {seen.clone(), seen.clone(), seen.clone()};
        DartStateResult r = processDartState(frames, backgrounds, boards, true, false, params);
        if (remembering)
        {
            say(r.previous_state == DartBoardState::CLEAN && r.current_state == DartBoardState::DART_1,
                "window 4: dart B is an ordinary FIRST dart of a new visit, read against the "
                "adopted reference");
            say(r.camera_results[1].tip_found && tipNear(r.camera_results[1].tip_position, kDartB, "window 4, camera 2"),
                "window 4: camera 2's tip is in dart B");
        }
        else
        {
            say(r.current_state == DartBoardState::DART_3,
                "window 4 (pinned): dart B lands as a THIRD dart of the merged visit -- the "
                "misattribution every census downstream inherits");
        }
    }

    std::cout << (failures == 0 ? "ALL OK" : std::to_string(failures) + " FAILURES") << std::endl;
    return failures == 0 ? 0 : 1;
}
