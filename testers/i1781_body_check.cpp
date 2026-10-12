// turnaus#1781: a takeout's arm published as a dart, either side of the END.
//
// Live on build 2b56b48 (OD_SPIKE_THRESHOLD=0.006, OD_LONE_CAMERA=on), 2026-10-10 16:24, the
// thrower walked up to take the darts out and the board published two darts nobody threw:
//
//   16:24:28.362  D11  cams 2 and 3 `not straight`, camera 1's lone tip. Camera 2's
//                      cumulative board change in that window was 69,193 px -- the figure
//                      the END's CLEAN BY REVERSION line fell FROM one window later.
//   16:24:29.105  END  CLEAN BY REVERSION on all three cameras.
//   16:24:30.081  S2   cams 1 and 3 `not straight`, camera 2's lone tip; its vote came
//                      ~976 ms after the END's (publication to publication).
//
// The rule (dart_processing.hpp, under OD_BODY_WINDOW) has two clauses, each a pure
// predicate held here at its boundaries and on the figures that bought it:
//
//   size   freshFigureIsBodySized: a voting camera whose fresh change is at least
//          bodySizedFreshSharePercent() of its board saw a body, not a dart
//   after  arrivalFollowsTakeoutTooSoon: a first dart voted within
//          arrivalAfterTakeoutHorizonMs() of a reversion END is the arm leaving
//
// Every assertion is labelled by what can turn it red:
//   pure:   the predicates' shape, with the figure passed explicitly -- no mutation of
//           the two constants moves one
//   size:   reads bodySizedFreshSharePercent()
//   after:  reads arrivalAfterTakeoutHorizonMs()
// and each `size:`/`after:` assertion names its side: `live` (must be refused) or
// `fixture` (must stand). testers/i1781_check.sh mutates each constant both ways and
// predicts which single assertion goes red.
//
//   g++ -std=c++17 -O1 -I src -I src/utils -o i1781_body_check testers/i1781_body_check.cpp \
//       $(pkg-config --cflags --libs opencv4)

#include <iostream>
#include <string>

#include "detector/geometry/detection/dart_processing.hpp"

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

// ---- the figures, and where each comes from -----------------------------------------
//
// The live board: the live log prints no board size, so the live arm is put on the
// fixture board of the same physical rig: rig-20260929's camera 2 board, from the
// I1781BODY census of the bakeoff (OD_BODY_CENSUS=1).
static const int kLiveCam2Board = 215989;
// The live arm: camera 2's cumulative figure in the D11 window (the END's "fell from").
// The fresh figure is that less the visit's darts already on the board, so it is put at
// the cumulative figure less two of the largest darts rig-20260929's camera 2 brought on
// the bakeoff (12,692 px), the conservative side: 43,809 px, 20.3%.
static const int kLiveCam2Cumulative = 69193;
static const int kRig29Cam2LargestDart = 12692;
static const int kLiveArmFresh = kLiveCam2Cumulative - 2 * kRig29Cam2LargestDart;
// The largest fresh figure any camera brought to a dart the vote called on any fixture
// window of the bakeoff (census over all seven runs): rig-20260922 opening, window 1,
// camera 1 -- v1.2's S16 read against the calibration picture holding the parked dart.
static const int kFixtureMaxDartFresh = 21508;
static const int kFixtureMaxDartBoard = 215925;
// The live S2's distance from the END, publication to publication.
static const long long kLiveS2AfterEndMs = 976;
// The soonest any first dart of a visit was voted after a reversion END on any fixture.
// rig-20260922 opening, window 3; no other fixture window reconciles a takeout by
// reversion.
static const long long kFixtureSoonestFirstDartMs = 4433;

int main()
{
    // ---- pure: the shape, with the figure given explicitly ----------------------------
    say(freshFigureIsBodySized(10, 100, 10.0), "pure: a share exactly at the figure is a body");
    say(!freshFigureIsBodySized(9, 100, 10.0), "pure: a share under the figure is not");
    say(!freshFigureIsBodySized(50, 0, 10.0), "pure: a camera with no board has no share and is never a body");
    say(!freshFigureIsBodySized(0, 100, 0.0), "pure: no fresh change is never a body, whatever the figure");
    say(!arrivalFollowsTakeoutTooSoon(-1, 1000), "pure: no END yet (-1) is never too soon");
    say(arrivalFollowsTakeoutTooSoon(0, 1000), "pure: a vote in the END's own millisecond is too soon");
    say(arrivalFollowsTakeoutTooSoon(999, 1000), "pure: one millisecond inside the horizon is too soon");
    say(!arrivalFollowsTakeoutTooSoon(1000, 1000), "pure: at the horizon it is not");

    // ---- size: the figure, against the live arm and the largest fixture dart -----------
    say(freshFigureIsBodySized(kLiveArmFresh, kLiveCam2Board),
        "size: live -- the D11 window's arm on camera 2 (" + std::to_string(kLiveArmFresh) + " of " +
            std::to_string(kLiveCam2Board) + " px) is body-sized, so the phantom D11 is held");
    say(!freshFigureIsBodySized(kFixtureMaxDartFresh, kFixtureMaxDartBoard),
        "size: fixture -- the largest fresh figure a called fixture dart brought (" +
            std::to_string(kFixtureMaxDartFresh) + " of " + std::to_string(kFixtureMaxDartBoard) +
            " px) is not, so no fixture dart is held");

    // ---- after: the horizon, against the live S2 and the soonest fixture first dart ----
    say(arrivalFollowsTakeoutTooSoon(kLiveS2AfterEndMs),
        "after: live -- the phantom S2, " + std::to_string(kLiveS2AfterEndMs) + " ms after the END, is held");
    say(!arrivalFollowsTakeoutTooSoon(kFixtureSoonestFirstDartMs),
        "after: fixture -- the soonest fixture first dart after a reversion END (" +
            std::to_string(kFixtureSoonestFirstDartMs) + " ms) stands");

    std::cout << "FAILURES: " << failures << std::endl;
    return failures == 0 ? 0 : 1;
}
