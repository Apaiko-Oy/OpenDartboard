// #1389: the camera quorum's arithmetic and its sentence, asked directly.
//
// Everything in camera_quorum.hpp is pure and inline for the reason whyNoEventIsPossible
// is (#1338) and whyNoStateChangeIsPossible is (#1348): a tester holds the rule without
// building the detector, and the number can be MOVED under a fixed board, which a whole
// binary cannot do without a second build.
//
// What is asked here is the half ADR-0081 §3 is about and a whole-binary run can only
// show one instance of: that the refusal names EVERY camera and its own reason, that it
// says something about a camera that can vote as well as about one that cannot, and that
// it is not a count. "A board reporting 'scoring on two of three' and nothing else has
// told nobody anything."
//
//   g++ -std=c++17 -I src -o quorum_check testers/i1389_quorum_check.cpp

#include <iostream>
#include <string>
#include <vector>

#include "detector/geometry/camera_quorum.hpp"

static int failures = 0;

static void say(bool ok, const std::string &what)
{
    std::cout << (ok ? "OK   " : "FAIL ") << what << std::endl;
    if (!ok)
    {
        failures++;
    }
}

int main(int argc, char **argv)
{
    const std::string which = argc > 1 ? argv[1] : "all";

    if (which == "all" || which == "floor")
    {
        std::cout << "camera_quorum::kCameras = " << camera_quorum::kCameras
                  << "; camera_quorum::cameras() = " << camera_quorum::cameras() << std::endl;
        say(camera_quorum::kCameras == 2,
            "the floor is two, and the reason is the state vote: stateVoteQuorum never "
            "returns fewer, so below it a board can never advance its own state (ADR-0081 §1)");

        // The gate, crossed. A gate nothing can fail is not evidence that anything was gated.
        std::cout << "0 seeing of 3: " << camera_quorum::whyTooFewCamerasSee(3, 0, 2) << std::endl;
        std::cout << "1 seeing of 3: " << camera_quorum::whyTooFewCamerasSee(3, 1, 2) << std::endl;
        std::cout << "2 seeing of 3: "
                  << (camera_quorum::whyTooFewCamerasSee(3, 2, 2).empty()
                          ? "(may score)"
                          : camera_quorum::whyTooFewCamerasSee(3, 2, 2))
                  << std::endl;
        say(!camera_quorum::whyTooFewCamerasSee(3, 0, 2).empty() &&
                !camera_quorum::whyTooFewCamerasSee(3, 1, 2).empty(),
            "nought and one are refused");
        say(camera_quorum::whyTooFewCamerasSee(3, 2, 2).empty() &&
                camera_quorum::whyTooFewCamerasSee(3, 3, 2).empty(),
            "two is the floor and three is the rig, so both may score");
        say(!camera_quorum::whyTooFewCamerasSee(3, 2, 3).empty(),
            "and the two-camera board is refused when the floor is moved to three, which is "
            "the one thing ADR-0081 §4 leaves open -- so the gate really reads the number");

        // #1321's rule: the count is stated against the threshold it fell short of.
        const std::string one = camera_quorum::whyTooFewCamerasSee(3, 1, 2);
        say(one.find("only 1 of 3") != std::string::npos && one.find("needs 2") != std::string::npos,
            "the refusal names the count it has and the threshold it needs, in one sentence");
        say(one.find("1 of 3 cameras is looking") != std::string::npos &&
                camera_quorum::whyTooFewCamerasSee(3, 2, 3).find("2 of 3 cameras are looking") !=
                    std::string::npos,
            "and it is English at both counts");
    }

    if (which == "all" || which == "named")
    {
        // ADR-0081 §3. The vocabulary is board_look's and is quoted here as board_look
        // writes it -- `refusal()` for NoFrame is "no frame to look at", and the
        // ring-not-traced sentence is its own. camera_quorum has no opinion about either
        // and must not acquire one, so what is asked is that it carries them through.
        std::vector<std::string> reasons;
        reasons.push_back("");
        reasons.push_back("no frame to look at");
        reasons.push_back("is not looking at the dartboard: no doubles ring could be traced around the bull");
        const std::string named = camera_quorum::namingEachCamera(reasons);
        std::cout << named << std::endl;

        say(named.find("camera 1:") != std::string::npos &&
                named.find("camera 2:") != std::string::npos &&
                named.find("camera 3:") != std::string::npos,
            "every camera is named, by its one-based number, including the one that is fine");
        say(named.find("camera 2: no frame to look at") != std::string::npos,
            "the camera that delivered nothing carries board_look's own NoFrame words, which "
            "is what sends somebody to the USB bus and to #1319");
        say(named.find("camera 3: is not looking at the dartboard: no doubles ring") != std::string::npos,
            "and a camera that answered and was refused carries its own, which is the aim and "
            "the lighting -- the opposite end of the room");
        say(named.find("camera 1: sees the dartboard and can vote") != std::string::npos,
            "a camera that CAN vote is named too, because 'two of three' without saying which "
            "two has told nobody anything (ADR-0081 §3)");

        // The mutation this is really guarding: a refusal that degrades to a count.
        say(named.find("2 of 3") == std::string::npos && named.find("2 of the 3") == std::string::npos,
            "and the sentence is not a count at all");

        // One camera, and no camera, are both legal boards to be asked about.
        std::vector<std::string> alone(1, std::string("no frame to look at"));
        say(camera_quorum::namingEachCamera(alone) == "camera 1: no frame to look at",
            "a one-slot board names its one camera");
        say(camera_quorum::namingEachCamera(std::vector<std::string>()).empty(),
            "and a board with no slots says nothing rather than half a sentence");
    }

    std::cout << (failures ? "QUORUM_CHECK_FAILED=" : "QUORUM_CHECK_OK=") << failures << std::endl;
    return failures ? 1 : 0;
}
