// #1678: the lone-camera corroboration rule held to its own numbers, without a fixture.
//
// `subFloorCameraCorroborates` is pure and inline in dart_processing.hpp. The numbers are
// OD_LONE_CENSUS=1's on the capture clock: rig-20260929's window #21 (visit 6's D5, the
// dart the vote refused) and rig-20260918's window #15 (visit 4.3, a MISS with the same
// vote shape: one camera up, none CLEAN). Not wired into run_all.sh or unit_check.sh,
// whose tables this issue does not own; it compiles with unit_check.sh's own g++ line
// (-I src -I src/utils, the build's _deps, pkg-config opencv4) in od-amd64:bullseye, and
// printed 9 OK, CHECK_RC=0 on 2026-09-29.
#include <cstdio>
#include <string>

#include "detector/geometry/detection/dart_processing.hpp"

static int failed = 0;
static void say(bool ok, const std::string &what)
{
    std::printf("%s%s\n", ok ? "OK   " : "FAIL ", what.c_str());
    if (!ok) failed++;
}

int main()
{
    using dart_processing::subFloorCameraCorroborates;

    // r29 #21, camera 1: 179 px in the scoring area (floor 215), 1366 in the physical
    // board, valid axis, tip (377,264) inside the scoring area, not a re-report.
    say(subFloorCameraCorroborates(1366, 215, true, true, true, false),
        "r29 #21 camera 1 (D5 clipped at the double wire) corroborates");
    // r29 #21, camera 3: 671 px physical (floor 183), valid axis, tip (869,159) off the
    // scoring area.
    say(!subFloorCameraCorroborates(671, 183, true, true, false, false),
        "r29 #21 camera 3, tip outside the scoring area, does not");
    // r18 #15 (a MISS): camera 3 has 723 px physical against 194, a valid axis and a tip,
    // but 0 px in the scoring area, so its tip is not in it.
    say(!subFloorCameraCorroborates(723, 194, true, true, false, false),
        "r18 #15 camera 3 (the miss) does not");
    // r18 #15 camera 2: 274 px physical against 204, valid 28 px axis, tip off the area.
    say(!subFloorCameraCorroborates(274, 204, true, true, false, false),
        "r18 #15 camera 2 (the miss) does not");
    say(!subFloorCameraCorroborates(150, 215, true, true, true, false),
        "a physical figure under the floor does not");
    say(!subFloorCameraCorroborates(1366, 215, false, true, true, false),
        "a figure that fits no axis does not");
    say(!subFloorCameraCorroborates(1366, 215, true, false, true, false),
        "a figure with no tip does not");
    say(!subFloorCameraCorroborates(1366, 215, true, true, true, true),
        "a #1535 re-report of an earlier dart's tip does not");
    say(!subFloorCameraCorroborates(1366, 0, true, true, true, false),
        "no floor (no fitted board) is no verdict");
    std::printf("CHECK_RC=%d\n", failed);
    return failed;
}
