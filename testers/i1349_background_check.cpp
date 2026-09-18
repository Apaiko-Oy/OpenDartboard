// #1349: one camera reading CLEAN must not cost the others their working background.
//
// The working background is the averaged frame at the last dart, and it is what lets the
// second dart's threshold image contain only the second dart. The shipped code wiped
// working_backgrounds [0], [1] and [2] -- every camera, by hard-coded index -- inside ONE
// camera's CLEAN-candidate branch, before the vote was counted. So a single camera
// flickering CLEAN mid-round (which #1345's log shows on every throw of the rig) sent the
// other cameras back to the full diff against the calibration background, a picture of
// every dart on the board, and detectTipAndCenter then found a tip of the union.
//
// This drives processDartState itself through one synthetic round, three cameras of
// 200x200 frames, one window per call (stability_frames = 1):
//
//   window 1  all three cameras see dart A            -> DART_1, tips in A
//   window 2  camera 0 flickers CLEAN (its frame is the plain background);
//             cameras 1 and 2 see A and B             -> DART_2, and the tips must be in
//             B -- on the shipped code camera 0's flicker wiped their working
//             backgrounds first, and B is drawn bigger than A precisely so that the
//             union's tip (the hull point furthest from the biggest piece's centre)
//             lands in A, the WRONG dart, making the defect visible as a coordinate
//   window 3  all three cameras see the plain background -> the takeout, CLEAN
//   window 4  all three see dart C alone              -> DART_1 again, tips in C: the
//             takeout (the reconciled CLEAN, not one camera's candidate) cleared every
//             working background, so no ghost of A or B is in the picture
//
//   g++ -std=c++17 -O1 -I src -I src/utils -o background_check testers/i1349_background_check.cpp \
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

// The three darts, as rectangles on a 200x200 frame. B is deliberately BIGGER than A:
// in the union A+B the biggest piece is then B, its centre is in B, and the hull point
// furthest from that centre is A's far corner -- so the shipped code's tip lands in A
// and the defect is a wrong coordinate rather than a subtlety.
static const Rect kDartA(140, 20, 24, 24);
static const Rect kDartB(30, 140, 30, 30);
static const Rect kDartC(30, 30, 24, 24);

static Mat plainBackground()
{
    return Mat::zeros(200, 200, CV_8UC3);
}

static Mat frameWith(const std::vector<Rect> &darts)
{
    Mat frame = plainBackground();
    for (const Rect &dart : darts)
    {
        rectangle(frame, dart, Scalar(255, 255, 255), FILLED);
    }
    return frame;
}

// Morphology dilates the blobs a few pixels, so a tip is asked to be near its dart
// rather than inside the exact rectangle.
static bool tipNear(const Point2f &tip, const Rect &dart, const std::string &what)
{
    const int margin = 14;
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

int main()
{
    DartParams params;
    params.stability_frames = 1; // one call is one window

    const std::vector<Mat> backgrounds = {plainBackground(), plainBackground(), plainBackground()};
    // #1345 gave processDartState the boards so a refused window can say how much of a
    // figure was on one. Unfitted here on purpose: this file is about the working
    // backgrounds, and an unknown board omits the clause rather than deciding anything.
    const std::vector<motion_processing::BoardExtent> no_boards(3);

    // ---- window 1: dart A on every camera ----------------------------------------------
    {
        const Mat seen = frameWith({kDartA});
        const std::vector<Mat> frames = {seen.clone(), seen.clone(), seen.clone()};
        DartStateResult r = processDartState(frames, backgrounds, no_boards, true, false, params);
        say(r.previous_state == DartBoardState::CLEAN && r.current_state == DartBoardState::DART_1,
            "window 1: three cameras seeing dart A take the board to DART_1");
        say(r.camera_results[1].tip_found && tipNear(r.camera_results[1].tip_position, kDartA, "window 1, camera 2"),
            "window 1: camera 2's tip is in dart A");
    }

    // ---- window 2: camera 0 flickers CLEAN, cameras 1 and 2 see A and B ----------------
    {
        const Mat both = frameWith({kDartA, kDartB});
        const std::vector<Mat> frames = {plainBackground(), both.clone(), both.clone()};
        DartStateResult r = processDartState(frames, backgrounds, no_boards, true, false, params);
        say(r.previous_state == DartBoardState::DART_1 && r.current_state == DartBoardState::DART_2,
            "window 2: two cameras moving up outvote one flicker, DART_1 -> DART_2");
        say(r.camera_results[0].detected_state == DartBoardState::CLEAN,
            "window 2: camera 1 really did read CLEAN -- the flicker this issue is about happened");

        // THE ISSUE. On the shipped code camera 1's flicker wiped every working
        // background before cameras 2 and 3 were processed, their threshold became the
        // union of A and B, and this tip landed in A.
        say(r.camera_results[1].tip_found &&
                tipNear(r.camera_results[1].tip_position, kDartB, "window 2, camera 2"),
            "window 2: camera 2's tip is in dart B, the dart that just landed -- its working "
            "background survived camera 1's flicker");
        say(r.camera_results[2].tip_found &&
                tipNear(r.camera_results[2].tip_position, kDartB, "window 2, camera 3"),
            "window 2: camera 3's tip is in dart B too");
    }

    // ---- window 3: the takeout ---------------------------------------------------------
    {
        const std::vector<Mat> frames = {plainBackground(), plainBackground(), plainBackground()};
        DartStateResult r = processDartState(frames, backgrounds, no_boards, true, false, params);
        say(r.previous_state == DartBoardState::DART_2 && r.current_state == DartBoardState::CLEAN,
            "window 3: an empty board is the takeout, DART_2 -> CLEAN");
    }

    // ---- window 4: a new round, dart C alone -------------------------------------------
    // The reconciled CLEAN above is what clears the working backgrounds now. If it did
    // not, this window's threshold for cameras 2 and 3 would be the diff against a
    // working background still holding A and B -- ghosts of removed darts beside C --
    // and the tip could land in one of them.
    {
        const Mat seen = frameWith({kDartC});
        const std::vector<Mat> frames = {seen.clone(), seen.clone(), seen.clone()};
        DartStateResult r = processDartState(frames, backgrounds, no_boards, true, false, params);
        say(r.previous_state == DartBoardState::CLEAN && r.current_state == DartBoardState::DART_1,
            "window 4: the next round begins at DART_1");
        say(r.camera_results[1].tip_found &&
                tipNear(r.camera_results[1].tip_position, kDartC, "window 4, camera 2"),
            "window 4: camera 2's tip is in dart C alone -- the takeout cleared every working "
            "background, so no ghost of A or B is in the picture");
        say(r.camera_results[2].tip_found &&
                tipNear(r.camera_results[2].tip_position, kDartC, "window 4, camera 3"),
            "window 4: camera 3's tip is in dart C too");
    }

    std::cout << (failures ? "FAILURES: " : "ALL OK: ") << failures << std::endl;
    return failures ? 1 : 0;
}
