// #1355: a board with a camera count other than three must not read or write past the
// end of any per-camera array, and every camera's state must be reconciled to the vote.
//
// `previous_states` was a static `vector<DartBoardState>` brace-initialised with exactly
// three CLEANs. The `!initialized` block sized `accumulated_frames`,
// `frames_accumulated` and `working_backgrounds` from `current_frames.size()` and did
// not touch it. It was then indexed `previous_states[i]` per REAL camera at four sites
// in the processing loop, and the post-vote loop iterated `previous_states.size()` --
// three -- rather than the camera count. So on a four-camera board the fourth camera was
// a read and a write past the end, and its state was never reconciled to the vote.
//
// #1341 has `1317-asan` hanging, so this is the direct call the issue names instead, and
// it is written so that the fourth camera's reconciliation is a COORDINATE rather than a
// hope: out-of-bounds reads are undefined and a test that only watches for a crash
// watches for nothing on a release build.
//
//   ./bounds_check <camera-count>
//
// One process is one camera count, because processDartState's state is static and its
// `initialized` flag is set once for the life of the program.
//
//   window 1  every camera but the LAST sees dart A; the last sees the plain background
//             -> the majority moves the board up, the last camera's own candidate is
//                CLEAN, and the vote reconciles it to DART_1 like everybody else
//   window 2  EVERY camera sees A and B
//             -> the last camera's candidate must be DART_2. On the shipped code, with
//                four cameras, camera 4's `previous_states[3]` was past the end and its
//                write-back went nowhere, so it answered DART_1: the ladder started it
//                from CLEAN again, one rung behind the board it is looking at.
//
//   g++ -std=c++17 -O1 -I src -I src/utils -o bounds_check testers/i1355_bounds_check.cpp \
//       src/detector/geometry/detection/dart_processing.cpp $(pkg-config --cflags --libs opencv4)

#include <cstdlib>
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

static const Rect kDartA(140, 20, 24, 24);
static const Rect kDartB(30, 140, 30, 30);

static Mat plainBackground()
{
    return Mat::zeros(200, 200, CV_8UC3);
}

static Mat frameWith(const std::vector<Rect> &darts)
{
    Mat f = plainBackground();
    for (const Rect &d : darts)
    {
        rectangle(f, d, Scalar(255, 255, 255), FILLED);
    }
    return f;
}

int main(int argc, char **argv)
{
    const int cameras = argc > 1 ? std::atoi(argv[1]) : 3;
    if (cameras < 2)
    {
        std::cerr << "usage: bounds_check <camera-count>, at least 2" << std::endl;
        return 2;
    }
    std::cout << "--- " << cameras << " cameras ---" << std::endl;

    DartParams params;
    params.stability_frames = 1; // one call is one window

    const std::vector<Mat> backgrounds(cameras, plainBackground());
    // Unfitted on purpose: this file is about the per-camera arrays, and an unknown
    // board leaves #1354's frame fallback deciding exactly as it always did.
    const std::vector<motion_processing::BoardExtent> no_boards(cameras);
    const size_t last = (size_t)cameras - 1;

    // ---- window 1: every camera but the last sees dart A -------------------------------
    {
        // The last camera dissents only where the board can still move without it. The
        // vote takes two to move whatever the camera count is, so on a TWO-camera board
        // a dissenter leaves the board where it was -- which is #1348's subject, not
        // this one. Two cameras is here to be measured for an out-of-bounds access, and
        // the dissent is what makes the FOURTH camera's reconciliation visible.
        const bool last_dissents = cameras >= 3;
        const Mat seen = frameWith({kDartA});
        std::vector<Mat> frames(cameras);
        for (int i = 0; i < cameras; i++)
        {
            frames[i] = (last_dissents && i == cameras - 1) ? plainBackground() : seen.clone();
        }
        DartStateResult r = processDartState(frames, backgrounds, no_boards, true, false, params);
        say(r.camera_results.size() == (size_t)cameras,
            "window 1: every camera slot produced a result");
        say(r.previous_state == DartBoardState::CLEAN && r.current_state == DartBoardState::DART_1,
            "window 1: the cameras that saw dart A take the board to DART_1");
        say(r.camera_results[last].detected_state ==
                (last_dissents ? DartBoardState::CLEAN : DartBoardState::DART_1),
            last_dissents
                ? "window 1: the last camera really did read CLEAN -- it has a state to reconcile"
                : "window 1: with two cameras nobody dissents, because the vote takes two to move");
    }

    // ---- window 2: every camera sees A and B -------------------------------------------
    // THE ISSUE. The last camera's previous state must be the RECONCILED DART_1, so its
    // candidate here is DART_2. With four cameras on the shipped code that camera's slot
    // was past the end of a three-element vector: the vote never wrote it, the ladder
    // read it from somewhere else, and DART_1 is what a ladder starting at CLEAN says.
    {
        const Mat both = frameWith({kDartA, kDartB});
        const std::vector<Mat> frames(cameras, both);
        DartStateResult r = processDartState(frames, backgrounds, no_boards, true, false, params);
        say(r.previous_state == DartBoardState::DART_1 && r.current_state == DartBoardState::DART_2,
            "window 2: the board moves DART_1 -> DART_2");
        for (size_t i = 0; i < r.camera_results.size(); i++)
        {
            say(r.camera_results[i].detected_state == DartBoardState::DART_2,
                "window 2: camera " + std::to_string(i + 1) +
                    " answers DART_2, so window 1's vote reached it (it answered " +
                    getDartBoardStateName(r.camera_results[i].detected_state) + ")");
        }
    }

    std::cout << (failures ? "FAILURES: " : "ALL OK: ") << failures << std::endl;
    return failures ? 1 : 0;
}
