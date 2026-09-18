// #1363: the configured anchor's arithmetic, on its own, held to the three constants
// upstream proved.
//
// `wedge20WireFromSouthWedge` is the star camera's own math generalised: upstream
// hard-coded three cases -- MIDDLE looks at the 6 and takes south-5, TOP at the 12 and
// south-18, BOTTOM at the 7 and south-12 -- and each is this formula at one wedge. The
// mocks' star camera has scored hand-verified darts through the MIDDLE case for the life
// of the fork, so reproducing all three pins the convention a configured wedge rides on.
// `configuredSouthWedge` is the operator's statement parsed: one entry per camera, the
// wedge number at the bottom of that camera's image, 0 for no statement, and a number no
// dartboard carries refused as -1 so the caller can refuse it by name.
//
//   g++ -std=c++17 -I src -I src/utils -o anchor_check testers/i1363_anchor_check.cpp \
//       $(pkg-config --cflags --libs opencv4)

#include <iostream>
#include <string>

#include "detector/geometry/calibration/orientation_processing.hpp"

using orientation_processing::configuredSouthWedge;
using orientation_processing::seqIndexOfWedge;
using orientation_processing::wedge20WireFromSouthWedge;

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
    // ---- the three cases upstream hard-coded, reproduced ------------------------------
    // Star/MIDDLE at south wire S looking at the 6: upstream wrote S-5 (mod 20).
    say(wedge20WireFromSouthWedge(15, 6, 20) == 10, "MIDDLE at the 6: south 15 -> wire 10 (south-5)");
    say(wedge20WireFromSouthWedge(3, 6, 20) == 18, "and it wraps: south 3 -> wire 18");
    // TOP at the 12: upstream wrote S-18.
    say(wedge20WireFromSouthWedge(19, 12, 20) == 1, "TOP at the 12: south 19 -> wire 1 (south-18)");
    // BOTTOM at the 7: upstream wrote S-12.
    say(wedge20WireFromSouthWedge(15, 7, 20) == 3, "BOTTOM at the 7: south 15 -> wire 3 (south-12)");

    // ---- the identity and the refusals ------------------------------------------------
    say(wedge20WireFromSouthWedge(5, 20, 20) == 5,
        "a camera looking straight at the 20 anchors on its own south wire");
    say(wedge20WireFromSouthWedge(5, 21, 20) == -1, "a wedge no board carries anchors nothing");
    say(wedge20WireFromSouthWedge(-1, 6, 20) == -1, "no south wire, no anchor");
    say(wedge20WireFromSouthWedge(5, 6, 0) == -1, "no wires, no anchor");
    say(seqIndexOfWedge(20) == 0 && seqIndexOfWedge(5) == 19 && seqIndexOfWedge(0) == -1,
        "the sequence starts at the 20, ends at the 5, and carries no 0");

    // ---- the operator's statement, parsed ---------------------------------------------
    say(configuredSouthWedge("9,0,3", 0) == 9 && configuredSouthWedge("9,0,3", 2) == 3,
        "\"9,0,3\" anchors cameras 1 and 3 with their own wedges");
    say(configuredSouthWedge("9,0,3", 1) == 0, "and states nothing for camera 2");
    say(configuredSouthWedge("9,0,3", 3) == 0, "a camera past the list is unanchored, not an error");
    say(configuredSouthWedge("", 0) == 0, "no statement anchors nothing");
    say(configuredSouthWedge("9,,3", 1) == 0, "a blank entry is no statement");
    say(configuredSouthWedge("9,21,3", 1) == -1,
        "21 is not a number a dartboard carries, and is refused rather than guessed at");
    say(configuredSouthWedge("9,x,3", 1) == -1, "and neither is x");

    std::cout << (failures ? "FAILURES: " : "ALL OK: ") << failures << std::endl;
    return failures ? 1 : 0;
}
