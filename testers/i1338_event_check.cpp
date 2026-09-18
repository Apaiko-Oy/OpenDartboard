// #1338: the arithmetic, on its own, and the proof that the answer moves with the number.
//
// `motion_processing::whyNoEventIsPossible` is what GeometryDetector::initialize refuses
// on. It is a pure function of two counts and one MotionParams, so it can be asked
// directly -- and, more to the point, it can be asked with `min_cameras_for_event` moved,
// which is the one thing a run of the whole detector cannot do without a second build. A
// gate nothing can fail is not evidence.
//
// #1353 moved the shipped quorum from 2 to 1 -- a dart splash is a one-camera motion
// fact, and the two-camera question lives in the dart-state vote now -- so what is
// measured here moved with it: the SAME board, one camera answering of three, is
// admitted at the shipped threshold of 1 and refused when the threshold is moved back to
// #1338's 2, and a board with nothing answering is refused whatever the quorum.
//
//   g++ -std=c++17 -I src -I src/utils -o event_check testers/i1338_event_check.cpp \
//       $(pkg-config --cflags --libs opencv4)

#include <iostream>
#include <string>

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

    std::cout << "min_cameras_for_event as this build ships it: "
              << shipped.min_cameras_for_event << std::endl;
    say(shipped.min_cameras_for_event == 1,
        "the constant the detector is gated on is 1 since #1353 -- the rig census "
        "measured a dart splash as a one-camera motion fact (the weak-side camera of one "
        "throw reads 0.0002-0.008, under any threshold that clears noise), and the "
        "two-camera quorum lives in the dart-state vote. If this line is the failure, "
        "every number below it is about a different board");

    // ---- the threshold, crossed by the board -------------------------------------------
    const std::string none = motion_processing::whyNoEventIsPossible(3, 0, shipped);
    const std::string one = motion_processing::whyNoEventIsPossible(3, 1, shipped);
    const std::string two = motion_processing::whyNoEventIsPossible(3, 2, shipped);
    const std::string three = motion_processing::whyNoEventIsPossible(3, 3, shipped);

    std::cout << "3 slots, 0 answering: " << (none.empty() ? "(possible)" : none) << std::endl;
    std::cout << "3 slots, 1 answering: " << (one.empty() ? "(possible)" : one) << std::endl;
    std::cout << "3 slots, 2 answering: " << (two.empty() ? "(possible)" : two) << std::endl;
    std::cout << "3 slots, 3 answering: " << (three.empty() ? "(possible)" : three) << std::endl;

    say(!none.empty(), "a board with no camera answering is refused, whatever the quorum");
    say(one.empty(), "one camera answering of three can form a dart event since #1353");
    say(two.empty() && three.empty(), "and so can two and three");

    // #1321's rule: the count is stated against the threshold it fell short of, so a
    // sentence carrying the wrong number can be seen to be wrong without reading the code.
    say(none.find("only 0 of 3") != std::string::npos &&
            none.find("at least 1") != std::string::npos,
        "the refusal names the count it has and the threshold it needs, in one sentence");

    // ---- the threshold, moved under the same board -------------------------------------
    // This is the half a whole-binary run cannot do. If the answer for one answering
    // camera does not change when min_cameras_for_event does, then the gate is not reading
    // the constant it claims to read and the sentence above is decoration.
    motion_processing::MotionParams strict = shipped;
    strict.min_cameras_for_event = 2;
    const std::string one_strict = motion_processing::whyNoEventIsPossible(3, 1, strict);
    std::cout << "min_cameras_for_event=2, 3 slots, 1 answering: "
              << (one_strict.empty() ? "(possible)" : one_strict) << std::endl;
    say(!one_strict.empty(),
        "the same one-camera board is refused when the threshold is moved back to "
        "#1338's 2, so the admission is driven by min_cameras_for_event and by nothing else");

    motion_processing::MotionParams stricter = shipped;
    stricter.min_cameras_for_event = 3;
    say(!motion_processing::whyNoEventIsPossible(3, 2, stricter).empty(),
        "and the two-camera board is refused when the threshold is moved to 3");

    // ---- the second fact the same function carries --------------------------------------
    // detectMotion refuses to initialise on any number of slots but 3, returns zeroed
    // MotionData and never sets `initialized`, so a two-camera board reports no motion on
    // any camera on any cycle however many of them are answering.
    const std::string two_slots = motion_processing::whyNoEventIsPossible(2, 2, shipped);
    std::cout << "2 slots, 2 answering: " << (two_slots.empty() ? "(possible)" : two_slots) << std::endl;
    say(!two_slots.empty() && two_slots.find("only initialises on 3") != std::string::npos,
        "a board running two cameras is refused, and told that motion detection needs 3");

    std::cout << (failures ? "EVENT_CHECK_FAILED=" : "EVENT_CHECK_OK=") << failures << std::endl;
    return failures ? 1 : 0;
}
