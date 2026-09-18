// #1350: the refused window's account, on its own, and the proof it says what the vote did.
//
// `dart_processing::refusedWindowAccount` is the sentence a completed window leaves at
// INFO when its vote changed nothing -- the window that used to leave one empty line,
// which is how the only failure the rig shows tonight (#1345) was invisible at normal
// level. It is a pure function of the camera results and the two counts, inline in the
// header for #1338's reason, so it can be held here without building the detector: that
// it is EMPTY exactly when the state moved (the scored path's INFO output is promised
// byte for byte), that every camera's candidate is named beside its own figure, that an
// abstaining camera is named as abstaining rather than given a number, and that the
// threshold in the sentence is read from DartParams rather than retyped -- proved by
// moving it and watching the sentence move.
//
//   g++ -std=c++17 -I src -I src/utils -o vote_line_check testers/i1350_vote_line_check.cpp \
//       $(pkg-config --cflags --libs opencv4)

#include <iostream>
#include <string>
#include <vector>

#include "detector/geometry/detection/dart_processing.hpp"

using dart_processing::CameraDetectionResult;
using dart_processing::DartBoardState;
using dart_processing::DartParams;
using dart_processing::refusedWindowAccount;

static int failures = 0;

static void say(bool ok, const std::string &what)
{
    std::cout << (ok ? "OK   " : "FAIL ") << what << std::endl;
    if (!ok)
    {
        failures++;
    }
}

static CameraDetectionResult answered(DartBoardState state, double change_ratio)
{
    CameraDetectionResult r;
    r.detected_state = state;
    r.change_ratio = change_ratio;
    r.frame_available = true;
    return r;
}

int main()
{
    const DartParams shipped;

    std::cout << "change_percent_threshold as this build ships it: "
              << shipped.change_percent_threshold << std::endl;
    say(shipped.change_percent_threshold == 0.22,
        "the threshold #1350 hoisted is still 0.22 -- the hoist moved a literal into "
        "DartParams and nothing else; if this line is the failure, the state stage's "
        "sensitivity has been changed and every number below is about a different board");

    // ---- tonight's window, the one the blank line hid (#1345) --------------------------
    std::vector<CameraDetectionResult> cameras = {
        answered(DartBoardState::DART_1, 0.310),
        answered(DartBoardState::CLEAN, 0.048),
        answered(DartBoardState::CLEAN, 0.019),
    };
    const std::string refused = refusedWindowAccount(cameras, DartBoardState::CLEAN,
                                                     DartBoardState::CLEAN, 1, 2, shipped);
    std::cout << refused << std::endl;

    say(!refused.empty(), "a window whose vote changed nothing gets a sentence");
    say(refused.find("camera 1 said DART_1 (0.310)") != std::string::npos,
        "the camera that saw a dart is named beside the figure it answered with");
    say(refused.find("camera 2 said CLEAN (0.048)") != std::string::npos &&
            refused.find("camera 3 said CLEAN (0.019)") != std::string::npos,
        "and so are the two that refused it, each with its own figure");
    say(refused.find("1 moved up and 2 read CLEAN") != std::string::npos &&
            refused.find("takes 2") != std::string::npos,
        "the two counts are stated against the 2 either of them needed (#1321's shape)");
    say(refused.find("stays CLEAN") != std::string::npos,
        "the sentence says what the board stayed as");
    say(refused.find("0.220") != std::string::npos,
        "the threshold the figures are read against is in the sentence");

    // ---- the two windows that must stay silent here ------------------------------------
    // A dart: the scored path already speaks at INFO, and #1350 promises that output
    // byte for byte, so the account must be empty -- the caller then logs the blank
    // line exactly as before.
    say(refusedWindowAccount(cameras, DartBoardState::CLEAN, DartBoardState::DART_1, 2, 1, shipped).empty(),
        "a window that moved the state up gets no sentence -- the scored path speaks");
    // A takeout: CLEAN is a change from DART_2, and the takeout is its own message.
    say(refusedWindowAccount(cameras, DartBoardState::DART_2, DartBoardState::CLEAN, 0, 2, shipped).empty(),
        "a takeout gets no sentence either");

    // ---- an abstaining camera is a name, not a number ----------------------------------
    std::vector<CameraDetectionResult> with_abstainer = {
        answered(DartBoardState::DART_1, 0.310),
        answered(DartBoardState::CLEAN, 0.048),
        CameraDetectionResult(), // frame_available true by default...
    };
    with_abstainer[2].frame_available = false;
    with_abstainer[2].change_ratio = 0.777; // a figure that must NOT be printed
    const std::string abstained = refusedWindowAccount(with_abstainer, DartBoardState::CLEAN,
                                                       DartBoardState::CLEAN, 1, 1, shipped);
    std::cout << abstained << std::endl;
    say(abstained.find("camera 3 abstained (no frames this window)") != std::string::npos,
        "a camera that contributed no frames is named as abstaining");
    say(abstained.find("0.777") == std::string::npos,
        "and no figure is printed for it -- an abstention is not a measurement");

    // ---- the threshold is read, not retyped --------------------------------------------
    // This is the half a run of the whole detector cannot do without a second build: move
    // the constant and watch the sentence move with it. If it does not, the sentence
    // carries a literal and would go on naming 0.220 after somebody resolves #1345 by
    // moving the threshold -- a line that reports the wrong number and cannot be seen to.
    DartParams moved = shipped;
    moved.change_percent_threshold = 0.5;
    const std::string against_moved = refusedWindowAccount(cameras, DartBoardState::CLEAN,
                                                           DartBoardState::CLEAN, 1, 2, moved);
    say(against_moved.find("0.500") != std::string::npos &&
            against_moved.find("0.220") == std::string::npos,
        "the sentence names the threshold DartParams holds, so a moved constant moves it");

    std::cout << (failures ? "FAILURES: " : "ALL OK: ") << failures << std::endl;
    return failures ? 1 : 0;
}
