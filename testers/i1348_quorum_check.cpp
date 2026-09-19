// #1348: the two quorums, the populations they are measured against, and the one place
// the difference between the old rule and the new one can actually be seen.
//
// Three populations were conflated by the code this issue is about, and they are not the
// same set:
//
//   ANSWERING   a camera that produced a frame. `whyNoEventIsPossible` was asked against
//               this until #1348, and since #1339 an answering camera with no fitted
//               board abstains from the motion figure, so it can never spike.
//   VOTING      a camera with a frame AND a fitted board. This is the ceiling on
//               `cameras_that_spiked` and, since #1354, on `moves_up` and `goes_clean`
//               too -- one population, both quorums.
//   EVIDENCE    a camera with changed pixels on its own board. This is NOT a population
//               for a quorum and the last section here is why: a fitted camera with
//               nothing on its board is voting CLEAN, which is the vote that ends a
//               round. #1345's "the evidence needs a location, not a bigger majority" is
//               about what a camera SAYS, and #1354 answered it; a quorum cannot.
//
// The rule is measured rather than described, and the measurement that matters is the
// one a whole-binary run cannot make. On a three-slot board -- and `whyNoEventIsPossible`
// refuses any other number of slots -- a majority of 1, 2 or 3 voters floored at 2 IS 2,
// so the majority rule and the absolute count agree on every population the detector can
// reach. Their difference is at FOUR voters, which #1355 made the arrays safe for, and it
// is reached here by driving processDartState directly, the way #1355 reached its fourth
// camera.
//
//   ./quorum_check <case>     table | sentences | four-majority | four-absolute
//                             | shoes-majority | shoes-absolute
//
// One process is one case, because processDartState's state is static and its
// `initialized` flag is set once for the life of the program.
//
//   g++ -std=c++17 -O1 -I src -I src/utils -o quorum_check testers/i1348_quorum_check.cpp \
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

// ---- the synthetic board, #1355's shapes -----------------------------------------------
static const Rect kDartA(140, 20, 24, 24);
static const Rect kDartB(30, 140, 30, 30);
// Off every fitted board, at the top of the frame, and far bigger than either dart:
// #1345's camera 1, whose 12,109-13,372 changed pixels were the thrower's shoes.
static const Rect kShoes(0, 0, 200, 60);

static Mat plainBackground()
{
    return Mat::zeros(200, 200, CV_8UC3);
}

static Mat frameWith(const std::vector<Rect> &shapes)
{
    Mat f = plainBackground();
    for (const Rect &r : shapes)
    {
        rectangle(f, r, Scalar(255, 255, 255), FILLED);
    }
    return f;
}

// A board fitted over the middle of the frame: kDartA and kDartB are inside it, kShoes is
// entirely outside it.
static motion_processing::BoardExtent fittedBoard()
{
    motion_processing::BoardExtent e;
    e.known = true;
    e.edge = RotatedRect(Point2f(100, 100), Size2f(180, 180), 0.0f);
    return e;
}

// ---- the table ---------------------------------------------------------------------------
static void theTable()
{
    DartParams shipped;
    std::cout << "min_cameras_to_move_the_board as this build ships it: "
              << shipped.min_cameras_to_move_the_board << std::endl;
    say(shipped.min_cameras_to_move_the_board == 2,
        "the floor the vote is gated on is 2 -- a board never moves on one camera's word. "
        "If this line is the failure, every number below it is about a different rule");

    const int expected[7] = {2, 2, 2, 2, 3, 3, 4};
    for (int voters = 0; voters <= 6; voters++)
    {
        const int q = stateVoteQuorum(voters, shipped);
        std::cout << "  voters " << voters << " -> quorum " << q << std::endl;
        say(q == expected[voters],
            "a window with " + std::to_string(voters) + " voters takes " +
                std::to_string(expected[voters]) + " of them (it answered " + std::to_string(q) + ")");
    }
    say(stateVoteQuorum(3, shipped) == 2 && stateVoteQuorum(2, shipped) == 2,
        "at three voters and under the quorum IS the 2 that was typed there before #1348, "
        "so nothing either fixture measures moves");
    say(stateVoteQuorum(4, shipped) == 3,
        "and at four -- the count #1355 made the per-camera arrays safe for -- an absolute "
        "2 would be a MINORITY, so the majority is 3");

    // The falsification, as a constant moved under a fixed board: if the answer does not
    // change when the rule does, the vote is not reading the rule it claims to read.
    DartParams absolute = shipped;
    absolute.absolute_quorum = true;
    for (int voters = 0; voters <= 6; voters++)
    {
        say(stateVoteQuorum(voters, absolute) == 2,
            "under the restored absolute count, " + std::to_string(voters) +
                " voters still take 2");
    }
    say(stateVoteQuorum(5, shipped) != stateVoteQuorum(5, absolute),
        "the two rules really do disagree somewhere, so the switch is not decoration");
}

// ---- the sentences -----------------------------------------------------------------------
static void theSentences()
{
    const DartParams shipped;
    const std::string none = whyNoStateChangeIsPossible(3, 0, shipped);
    const std::string one = whyNoStateChangeIsPossible(3, 1, shipped);
    const std::string two = whyNoStateChangeIsPossible(3, 2, shipped);
    const std::string three = whyNoStateChangeIsPossible(3, 3, shipped);

    std::cout << "3 slots, 0 voting: " << (none.empty() ? "(possible)" : none) << std::endl;
    std::cout << "3 slots, 1 voting: " << (one.empty() ? "(possible)" : one) << std::endl;
    std::cout << "3 slots, 2 voting: " << (two.empty() ? "(possible)" : two) << std::endl;
    std::cout << "3 slots, 3 voting: " << (three.empty() ? "(possible)" : three) << std::endl;

    say(!none.empty(), "a board with no camera able to vote can never change state");
    say(!one.empty(),
        "nor can one with a single voting camera -- #1348's title: it forms dart events "
        "happily since #1353 and cannot move its own state, so it holds CLEAN for ever");
    say(two.empty() && three.empty(), "two and three voting cameras can");

    // #1321's rule: the count is stated against the threshold it fell short of.
    say(one.find("only 1 of 3") != std::string::npos && one.find("takes 2") != std::string::npos,
        "the refusal names the count it has and the threshold it needs, in one sentence");
    say(one.find("taken out") != std::string::npos,
        "and it names the takeout, because a board that cannot move cannot see one either");

    // The other quorum, against the other name for the same population. #1338's own
    // tester moves min_cameras_for_event under a fixed board; what is asked here is that
    // the sentence stopped describing the ANSWERING count.
    const std::string no_event = motion_processing::whyNoEventIsPossible(3, 0);
    std::cout << "3 slots, 0 able to spike: " << no_event << std::endl;
    say(no_event.find("can report motion") != std::string::npos &&
            no_event.find("fitted board") != std::string::npos,
        "the event quorum's sentence names the cameras that can SPIKE -- a frame and a "
        "fitted board -- and not the ones that merely answered");
}

// ---- four voters, both rules ---------------------------------------------------------------
//
// Two of four cameras see dart A; the other two see the plain background. Under the
// majority rule that is 2 of 4 against a quorum of 3 and the board stays CLEAN; under the
// restored absolute count it is 2 against 2 and the board moves to DART_1. This is the
// only place in the repository where the two rules answer differently, because
// whyNoEventIsPossible will not let a whole binary run four cameras at all.
static void fourVoters(bool absolute)
{
    DartParams params;
    params.stability_frames = 1; // one call is one window
    params.absolute_quorum = absolute;

    const int cameras = 4;
    const std::vector<Mat> backgrounds(cameras, plainBackground());
    const std::vector<motion_processing::BoardExtent> boards(cameras, fittedBoard());

    const Mat seen = frameWith({kDartA});
    std::vector<Mat> frames(cameras);
    for (int i = 0; i < cameras; i++)
    {
        frames[i] = i < 2 ? seen.clone() : plainBackground();
    }

    DartStateResult r = processDartState(frames, backgrounds, boards, true, false, params);
    int moved_up = 0;
    for (const CameraDetectionResult &c : r.camera_results)
    {
        if (c.frame_available && !c.abstained_no_board && c.detected_state != DartBoardState::CLEAN)
        {
            moved_up++;
        }
    }
    std::cout << "four voters, " << (absolute ? "absolute" : "majority") << " rule: "
              << moved_up << " of 4 cameras called a dart, quorum "
              << stateVoteQuorum(4, params) << ", board "
              << getDartBoardStateName(r.previous_state) << " -> "
              << getDartBoardStateName(r.current_state) << std::endl;

    say(moved_up == 2,
        "the fixture really is two of four calling a dart (it was " + std::to_string(moved_up) + ")");
    if (absolute)
    {
        say(r.current_state == DartBoardState::DART_1,
            "under the absolute count two of four move a four-camera board -- a MINORITY "
            "of its cameras, which is the rule #1348 replaced");
    }
    else
    {
        say(r.current_state == DartBoardState::CLEAN,
            "under the majority rule two of four do not move the board: the quorum is 3");
    }
}

// ---- #1345's window, both rules --------------------------------------------------------
//
// The shape #1345 measured on mocks/rig-20260918: one camera with NO fitted board and a
// large change entirely off every board -- the thrower's shoes -- beside two cameras that
// did fit a board and have nothing on it. The board must not move, and the point of
// running it under BOTH rules is that the refusal must come from #1354's abstention and
// not from anything #1348 wrote. A duplicated rule is worse than none.
static void theShoes(bool absolute)
{
    DartParams params;
    params.stability_frames = 1;
    params.absolute_quorum = absolute;

    const int cameras = 3;
    const std::vector<Mat> backgrounds(cameras, plainBackground());
    std::vector<motion_processing::BoardExtent> boards(cameras, fittedBoard());
    boards[0].known = false; // the rig's camera 1: answering, no board fitted

    std::vector<Mat> frames(cameras);
    frames[0] = frameWith({kShoes});    // a big change, none of it on any board
    frames[1] = plainBackground();      // a fitted board with nothing on it
    frames[2] = plainBackground();

    DartStateResult r = processDartState(frames, backgrounds, boards, true, false, params);

    int voters = 0;
    for (const CameraDetectionResult &c : r.camera_results)
    {
        if (c.frame_available && !c.abstained_no_board)
        {
            voters++;
        }
    }
    std::cout << "#1345's window, " << (absolute ? "absolute" : "majority") << " rule: "
              << voters << " voters, camera 1 "
              << (r.camera_results[0].abstained_no_board ? "abstained" : "VOTED")
              << " on " << r.camera_results[0].total_changed_pixels << " changed pixels of which "
              << r.camera_results[0].board_changed_pixels << " were on a board, board "
              << getDartBoardStateName(r.previous_state) << " -> "
              << getDartBoardStateName(r.current_state) << std::endl;

    say(r.camera_results[0].total_changed_pixels > 0,
        "the needle is there: camera 1 really did see a large change (" +
            std::to_string(r.camera_results[0].total_changed_pixels) +
            " px), so an absence below is an absence and not an empty fixture");
    say(r.camera_results[0].abstained_no_board,
        "#1354: the camera with no fitted board abstains, so its change is not a vote");
    say(voters == 2, "which leaves two voters, both looking at an empty board");
    say(r.current_state == DartBoardState::CLEAN,
        std::string("the board does not move") +
            (absolute
                 ? " -- and it does not move under the ABSOLUTE count either, so the refusal "
                   "is #1354's location and #1348 did not add a second guard for it"
                 : " under the majority rule"));
}

int main(int argc, char **argv)
{
    const std::string which = argc > 1 ? argv[1] : "table";
    std::cout << "--- " << which << " ---" << std::endl;

    if (which == "table")
    {
        theTable();
    }
    else if (which == "sentences")
    {
        theSentences();
    }
    else if (which == "four-majority")
    {
        fourVoters(false);
    }
    else if (which == "four-absolute")
    {
        fourVoters(true);
    }
    else if (which == "shoes-majority")
    {
        theShoes(false);
    }
    else if (which == "shoes-absolute")
    {
        theShoes(true);
    }
    else
    {
        std::cerr << "usage: quorum_check table|sentences|four-majority|four-absolute|"
                     "shoes-majority|shoes-absolute" << std::endl;
        return 2;
    }

    std::cout << (failures ? "QUORUM_CHECK_FAILED=" : "QUORUM_CHECK_OK=") << failures << std::endl;
    return failures ? 1 : 0;
}
