// turnaus#1802: a body at the board, carried as three darts by the rim-only votes.
//
// Live on casual board 20 (2026-10-11, build 0.1.13-1798, OD_SPIKE_THRESHOLD=0.006
// OD_LONE_CAMERA=on OD_KEEP_FRAMES=on), between the takeout END at 01:01:58.525 and the next
// at 01:02:14.754, the board published three MISSes nobody threw. The log's own figures:
//
//   01:02:06.363  I1707 RIM CARRIED, 3 up, 1 clearing the scoring floor -- camera 2,
//                 `not straight` (3.90 px RMS against a 2.5 px gate); cams 1 and 3 `rim only`
//   01:02:08.241  I1707 RIM CARRIED, 3 up, 0 clearing; all three `rim only`
//   01:02:10.312  I1707 RIM CARRIED, 3 up, 1 clearing -- camera 2, `not straight` (10.06 px)
//   01:02:14.738  END: CLEAN BY REVERSION, camera 2 falling FROM 25,835 px (cams 1 and 3
//                 from 2,833 and 3,063)
//
// The fresh figures of the three windows were never printed (OD_BODY_CENSUS was off). The
// one figure printed is camera 2's cumulative 25,835 px in the third window; from a CLEAN
// board, and on every window a hold keeps at CLEAN, the working background is empty and the
// fresh figure IS the cumulative one, so that is the figure the rule reads there. The
// second window's assertion carries the same figure as a stand-in, and says so.
//
// The rule (dart_processing.hpp, under OD_RIM_BODY): rimCarryIsABody holds an advance the
// rim-only votes carried when no voting camera cleared the scoring floor on a usable line and
// a voting camera's fresh figure is body-sized by #1781's measure (freshFigureIsBodySized) at
// rimBodySharePercent().
//
// Every assertion is labelled by what can turn it red:
//   pure:    the predicate's shape, with the figure passed explicitly
//   share:   reads rimBodySharePercent()
//   usable:  reads the usable-line clause
//   rim:     reads the rim-carried clause
// and names its side: `live` (must be held) or `fixture`/`surround`/`scoring` (must stand).
// testers/i1802_check.sh mutates each and predicts which assertions go red.
//
// No session data is in this file: only the log's pixel figures and its clock times.
//
//   g++ -std=c++17 -O1 -I src -I src/utils -o i1802_rimbody_check
//       testers/i1802_rimbody_check.cpp $(pkg-config --cflags --libs opencv4)

#include <iostream>
#include <string>
#include <vector>

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
// The board: the live log prints no board size, so camera 2's is #1781's stand-in, the same
// rig's camera-2 board on rig-20260929 (I1781BODY census of the bakeoff). Cameras 1 and 3
// are given the same size; their figures are an order of magnitude under any threshold.
static const int kBoard = 215989;
// Live, the third phantom window: camera 2's cumulative figure (the END's "fell from"),
// 11.96% of the board; cameras 1 and 3's, 2,833 and 3,063.
static const int kLiveCam2 = 25835;
static const int kLiveCam1 = 2833;
static const int kLiveCam3 = 3063;
// The largest fresh figure any camera brought to any called dart on the seven bakeoff
// replays (#1781's census): rig-20260922 opening, window 1, camera 1, 9.96% of its board.
static const int kFixtureMaxDart = 21508;
static const int kFixtureMaxDartBoard = 215925;
// A dart in the surround: live 2b56b48 16:51, three surround misses, camera 2 at 32,153 px
// in the window after the third (docs/rig.md, #1783); a third of it per dart.
static const int kSurroundDart = 32153 / 3;

static RimVoter rimOnly(int fresh, int board = kBoard)
{
    RimVoter v;
    v.voted_up = true;
    v.cleared_scoring_floor = false;
    v.axis_usable = false;
    v.fresh_pixels = fresh;
    v.board_pixels = board;
    return v;
}

static RimVoter clearsOnALine(int fresh, int board = kBoard)
{
    RimVoter v = rimOnly(fresh, board);
    v.cleared_scoring_floor = true;
    v.axis_usable = true;
    return v;
}

static RimVoter clearsNotStraight(int fresh, int board = kBoard)
{
    RimVoter v = rimOnly(fresh, board);
    v.cleared_scoring_floor = true;
    v.axis_usable = false;
    return v;
}

int main()
{
    std::cout << "rimBodySharePercent() = " << rimBodySharePercent() << "%; live camera 2 "
              << 100.0 * kLiveCam2 / kBoard << "%, largest fixture dart " << 100.0 * kFixtureMaxDart / kFixtureMaxDartBoard
              << "%, a surround dart " << 100.0 * kSurroundDart / kBoard << "%" << std::endl;

    // ---- pure: the shape, with the figure given explicitly ----------------------------
    {
        const std::vector<RimVoter> body = {rimOnly(20, 100), rimOnly(1, 100)};
        say(rimCarryIsABody(true, body, 10.0), "pure: a rim-carried window with a body-sized rim-only voter is held");
        say(!rimCarryIsABody(false, body, 10.0), "pure: the same figures in a window the rim did not carry are not");
        say(!rimCarryIsABody(true, {rimOnly(9, 100), rimOnly(1, 100)}, 10.0), "pure: a figure under the share is not a body");
        say(!rimCarryIsABody(true, {clearsOnALine(1, 100), rimOnly(20, 100)}, 10.0),
            "pure: one camera on a usable line in the scoring area keeps the dart, whatever the rim saw");
        say(rimCarryIsABody(true, {clearsNotStraight(20, 100), rimOnly(1, 100)}, 10.0),
            "pure: a camera that cleared the floor with no usable line does not keep it");
        RimVoter clean = rimOnly(20, 100);
        clean.voted_up = false;
        say(!rimCarryIsABody(true, {clean, rimOnly(1, 100)}, 10.0), "pure: a figure on a camera that did not vote up is not read");
        say(!rimCarryIsABody(true, {rimOnly(-1, 100), rimOnly(1, 100)}, 10.0), "pure: an uncomputed figure (-1) is not a body");
        say(!rimCarryIsABody(true, {}, 10.0), "pure: no voters, no body");
    }

    // ---- live: the phantom windows, which must be held ---------------------------------
    // 01:02:10.312, figures as printed: camera 2 cleared the floor `not straight`.
    say(rimCarryIsABody(true, {rimOnly(kLiveCam1), clearsNotStraight(kLiveCam2), rimOnly(kLiveCam3)}),
        "usable: live 01:02:10 -- camera 2 cleared the scoring floor on no usable line, 25,835 px: held");
    // 01:02:08.241: no camera cleared it. Its own figures were not printed; the third window's
    // stand in.
    say(rimCarryIsABody(true, {rimOnly(kLiveCam1), rimOnly(kLiveCam2), rimOnly(kLiveCam3)}),
        "share: live 01:02:08 -- every camera rim only, camera 2 at the third window's 25,835 px: held");

    // ---- what must stand -----------------------------------------------------------------
    // The largest dart figure on record, put in the phantom's own shape (no camera on a line).
    say(!rimCarryIsABody(true, {rimOnly(kLiveCam1), clearsNotStraight(kFixtureMaxDart, kFixtureMaxDartBoard),
                                rimOnly(kLiveCam3)}),
        "share: fixture -- the largest fixture dart (9.96%) with no camera on a line stands");
    // A surround miss: three rim-only cameras, a surround dart's figure on camera 2.
    say(!rimCarryIsABody(true, {rimOnly(kLiveCam1), rimOnly(kSurroundDart), rimOnly(kLiveCam3)}),
        "share: surround -- a dart in the surround (about 5%) on three rim-only cameras stands");
    // #1821's S13 (00:59:28): camera 3 cleared on a usable line, cameras 1 and 2 rim only; a
    // body-sized figure put on camera 2 cannot hold it.
    say(!rimCarryIsABody(true, {rimOnly(kLiveCam1), rimOnly(kLiveCam2), clearsOnALine(kLiveCam3)}),
        "usable: fixture -- one camera on a usable line in the scoring area (#1821's S13) stands");
    // A window the scoring area carried is #1781's to judge (OD_BODY_WINDOW), never this rule's.
    say(!rimCarryIsABody(false, {clearsNotStraight(kLiveCam2), clearsNotStraight(kLiveCam2), rimOnly(kLiveCam3)}),
        "rim: scoring -- a window the scoring area carried is not this rule's, however large");

    std::cout << "FAILURES: " << failures << std::endl;
    return failures == 0 ? 0 : 1;
}
