// #1338: the arithmetic, on its own, and the proof that the answer moves with the number.
//
// `motion_processing::whyNoEventIsPossible` is what GeometryDetector::initialize refuses
// on. It is a pure function of two counts and a threshold, so it can be asked directly --
// and, more to the point, it can be asked with the threshold MOVED, which is the one
// thing a run of the whole detector cannot do without a second build. A gate nothing can
// fail is not evidence.
//
// #1353 moved the shipped quorum from 2 to 1 -- a dart splash is a one-camera motion
// fact -- and #1389 put it back, but not by moving that constant back. It split the two
// questions that constant was answering:
//
//   the TRIGGER   how many cameras must spike TOGETHER inside one window. That is
//                 `MotionParams::min_cameras_for_event`, it is still #1353's measured 1,
//                 and it is what processMotion gates an event on.
//
//   the CENSUS    how many cameras a board must HAVE before it may score at all. That is
//                 `camera_quorum::cameras()` -- two -- read here, by calibration
//                 admission and by the state vote's floor, so the three cannot disagree
//                 (ADR-0081 §2).
//
// So what is measured here moved with the split: the SAME board, one camera answering of
// three, is now refused at the shipped census of 2 and admitted when the census is moved
// to 1, and moving the spike trigger does not change that answer at all -- which is the
// half #1389 bought and the half that was not askable before it.
//
//   g++ -std=c++17 -I src -I src/utils -o event_check testers/i1338_event_check.cpp \
//       $(pkg-config --cflags --libs opencv4)

#include <iostream>
#include <string>

#include "detector/geometry/camera_quorum.hpp"
#include "detector/geometry/detection/dart_processing.hpp"
#include "detector/geometry/detection/motion_processing.hpp"

static int failures = 0;

static void say(bool ok, const std::string &what)
{
    std::cout << (ok ? "OK   " : "FAIL ") << what << std::endl;
    if (!ok)
    {
        failures++;
    }
}

int main()
{
    const motion_processing::MotionParams shipped;

    std::cout << "min_cameras_for_event (the per-window spike trigger) as this build ships it: "
              << shipped.min_cameras_for_event << std::endl;
    std::cout << "camera_quorum::cameras() (the board census) as this build ships it:           "
              << camera_quorum::cameras() << std::endl;

    say(shipped.min_cameras_for_event == 1,
        "the spike trigger is 1 since #1353 -- the rig census measured a dart splash as a "
        "one-camera motion fact (the weak-side camera of one throw reads 0.0002-0.008, "
        "under any threshold that clears noise). If this line is the failure, a healthy "
        "three-camera board has stopped scoring and every number below it is about a "
        "different board");
    say(camera_quorum::cameras() == 2,
        "the board census is 2 since #1389 -- that is what the state vote requires, so "
        "below it a board is arithmetically unable to advance state (ADR-0081 §1)");

    // ---- the threshold, crossed by the board -------------------------------------------
    const std::string none = motion_processing::whyNoEventIsPossible(3, 0);
    const std::string one = motion_processing::whyNoEventIsPossible(3, 1);
    const std::string two = motion_processing::whyNoEventIsPossible(3, 2);
    const std::string three = motion_processing::whyNoEventIsPossible(3, 3);

    std::cout << "3 slots, 0 answering: " << (none.empty() ? "(possible)" : none) << std::endl;
    std::cout << "3 slots, 1 answering: " << (one.empty() ? "(possible)" : one) << std::endl;
    std::cout << "3 slots, 2 answering: " << (two.empty() ? "(possible)" : two) << std::endl;
    std::cout << "3 slots, 3 answering: " << (three.empty() ? "(possible)" : three) << std::endl;

    say(!none.empty(), "a board with no camera answering is refused, whatever the quorum");
    say(!one.empty(),
        "one camera answering of three is refused since #1389 -- it was admitted between "
        "#1353 and #1389, and it could not move its own state for a second of it");
    say(two.empty() && three.empty(),
        "two is the floor and three is the rig, so both may score (ADR-0081 §1)");

    // #1321's rule: the count is stated against the threshold it fell short of, so a
    // sentence carrying the wrong number can be seen to be wrong without reading the code.
    say(none.find("only 0 of 3") != std::string::npos &&
            none.find("needs 2") != std::string::npos,
        "the refusal names the count it has and the threshold it needs, in one sentence");

    // ---- the threshold, moved under the same board -------------------------------------
    // This is the half a whole-binary run cannot do. If the answer for one answering
    // camera does not change when the census does, then the gate is not reading the
    // number it claims to read and the sentence above is decoration.
    const std::string one_relaxed = motion_processing::whyNoEventIsPossible(3, 1, 1);
    std::cout << "census=1, 3 slots, 1 answering: "
              << (one_relaxed.empty() ? "(possible)" : one_relaxed) << std::endl;
    say(one_relaxed.empty(),
        "the same one-camera board is admitted when the census is moved back to #1353's 1, "
        "so the admission is driven by the camera quorum and by nothing else");
    say(!motion_processing::whyNoEventIsPossible(3, 2, 3).empty(),
        "and the two-camera board is refused when the census is moved to 3 -- which is "
        "what ADR-0081 §4 says happens if two-camera accuracy measures badly");

    // ---- #1389's own half: the three quorums cannot disagree ----------------------------
    // Two of the three are in this process and can be held to each other directly. The
    // third, calibration admission, is in geometry_detector.cpp and is held to them by
    // `testers/i1389_quorum_census.py`, which reads the source rather than running it --
    // because the thing to refuse is a FOURTH site, and a fourth site has no runtime to
    // be asked in until somebody writes one.
    const int floor_of_the_vote = dart_processing::DartParams().min_cameras_to_move_the_board;
    std::cout << "the state vote's floor: " << floor_of_the_vote
              << "; the board census: " << camera_quorum::cameras() << std::endl;
    say(floor_of_the_vote == camera_quorum::cameras(),
        "the state vote's floor and the dart event's board census are the same number, "
        "read from one place (#1389)");
    say(!motion_processing::whyNoEventIsPossible(3, floor_of_the_vote - 1).empty() &&
            motion_processing::whyNoEventIsPossible(3, floor_of_the_vote).empty(),
        "and the event census turns over exactly where the vote's floor is, so a board "
        "the vote could never move is never admitted by the motion stage either");

    // ---- a reduced board uses the same quorum -------------------------------------------
    // Motion keeps its state per slot, so two cameras that both have a board may form
    // events and advance the two-camera state vote.  The old exact-three restriction
    // made an auto-discovered pair calibrate and then fail before scoring could start.
    const std::string two_slots = motion_processing::whyNoEventIsPossible(2, 2);
    std::cout << "2 slots, 2 answering: " << (two_slots.empty() ? "(possible)" : two_slots) << std::endl;
    say(two_slots.empty(),
        "two cameras that can report motion meet the same two-camera quorum as the state vote");
    say(!motion_processing::whyNoEventIsPossible(1, 1).empty(),
        "one camera remains refused because the two-camera state-vote quorum is unchanged");

    // The admission sentence above must describe the detector, not merely be willing
    // prose.  A first calm frame seeds the two slots; a full-board change on the next
    // frame must enter the motion state machine.  Before the reduced-board repair,
    // detectMotion returned two zeroed MotionData objects on both calls and this stayed
    // IDLE forever.
    std::vector<cv::Mat> backgrounds(2, cv::Mat::zeros(96, 96, CV_8UC3));
    std::vector<cv::Mat> calm(2, cv::Mat::zeros(96, 96, CV_8UC3));
    std::vector<cv::Mat> changed(2, cv::Mat(96, 96, CV_8UC3, cv::Scalar(255, 255, 255)));
    motion_processing::BoardExtent board;
    board.known = true;
    board.edge = cv::RotatedRect(cv::Point2f(48, 48), cv::Size2f(80, 80), 0.0f);
    std::vector<motion_processing::BoardExtent> boards(2, board);
    motion_processing::MotionParams motion;
    motion.spike_threshold = 0.01;
    motion_processing::processMotion(calm, backgrounds, boards, false, motion);
    const motion_processing::MotionResult spike =
        motion_processing::processMotion(changed, backgrounds, boards, false, motion);
    say(spike.current_state == motion_processing::DartEventState::SPIKE_DETECTED,
        "two calibrated cameras seed motion and a board-sized change starts an event");

    std::cout << (failures ? "EVENT_CHECK_FAILED=" : "EVENT_CHECK_OK=") << failures << std::endl;
    return failures ? 1 : 0;
}
