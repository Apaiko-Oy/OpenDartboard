// #1338: the arithmetic, on its own, and the proof that the answer moves with the number.
//
// `motion_processing::whyNoEventIsPossible` is what GeometryDetector::initialize refuses
// on. It is a pure function of two counts and one MotionParams, so it can be asked
// directly -- and, more to the point, it can be asked with `min_cameras_for_event` moved,
// which is the one thing a run of the whole detector cannot do without a second build. A
// gate nothing can fail is not evidence, so what is measured here is that the SAME board
// -- one camera answering of three -- is refused at the shipped threshold of 2 and
// admitted at 1, and that at the shipped threshold the answer flips between 1 answering
// and 2.
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
    say(shipped.min_cameras_for_event == 2,
        "the constant the detector is gated on is still 2 -- if this line is the failure, "
        "every number below it is about a different board");

    // ---- the threshold, crossed by the board -------------------------------------------
    const std::string one = motion_processing::whyNoEventIsPossible(3, 1, shipped);
    const std::string two = motion_processing::whyNoEventIsPossible(3, 2, shipped);
    const std::string three = motion_processing::whyNoEventIsPossible(3, 3, shipped);

    std::cout << "3 slots, 1 answering: " << (one.empty() ? "(possible)" : one) << std::endl;
    std::cout << "3 slots, 2 answering: " << (two.empty() ? "(possible)" : two) << std::endl;
    std::cout << "3 slots, 3 answering: " << (three.empty() ? "(possible)" : three) << std::endl;

    say(!one.empty(), "one camera answering of three cannot form a dart event");
    say(two.empty(), "two answering can, so the refusal is not a refusal of everything");
    say(three.empty(), "three answering can");

    // #1321's rule: the count is stated against the threshold it fell short of, so a
    // sentence carrying the wrong number can be seen to be wrong without reading the code.
    say(one.find("only 1 of 3") != std::string::npos &&
            one.find("at least 2") != std::string::npos,
        "the refusal names the count it has and the threshold it needs, in one sentence");

    // ---- the threshold, moved under the same board -------------------------------------
    // This is the half a whole-binary run cannot do. If the answer for one answering
    // camera does not change when min_cameras_for_event does, then the gate is not reading
    // the constant it claims to read and the sentence above is decoration.
    motion_processing::MotionParams relaxed = shipped;
    relaxed.min_cameras_for_event = 1;
    const std::string one_relaxed = motion_processing::whyNoEventIsPossible(3, 1, relaxed);
    std::cout << "min_cameras_for_event=1, 3 slots, 1 answering: "
              << (one_relaxed.empty() ? "(possible)" : one_relaxed) << std::endl;
    say(one_relaxed.empty(),
        "the same one-camera board is admitted when the threshold is moved to 1, so the "
        "refusal is driven by min_cameras_for_event and by nothing else");

    motion_processing::MotionParams strict = shipped;
    strict.min_cameras_for_event = 3;
    say(!motion_processing::whyNoEventIsPossible(3, 2, strict).empty(),
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
