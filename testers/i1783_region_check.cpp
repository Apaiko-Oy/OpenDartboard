// turnaus#1783: a takeout of three surround misses opens no event.
//
// Live on build 2b56b48 (OD_SPIKE_THRESHOLD=0.006, OD_LONE_CAMERA=on), 2026-10-10 16:51, a
// visit of three misses on the surround published three MISSes, each `I1707 RIM CARRIED`
// with the cameras `rim only`: #1689's dart counts reach the rim. The thrower pulled the
// three darts and nothing followed in the log for 16 s -- no event, no window, no vote --
// until a second takeout's window reverted all three cameras and published the END. The
// motion figure that opens an event is counted inside the double's outer ellipse, so a
// hand gripping darts between the double and the rim changes nothing it counts.
//
// The rule (motion_processing.hpp, under OD_MOTION_REGION=rim): the motion figure is
// counted out to the rim (the double's ellipse x 225.5/170, dart_processing's
// Region::tip_mask) and stays a share of the DOUBLE's area, so every ratio in MotionParams
// keeps its units. Held here on the rig's own camera-2 ellipse, with a changed patch put
// where each case puts it:
//
//   double:  the default count -- a hand in the surround is not counted (the live fault)
//   rim:     the switch's count -- the same hand is, and the thrower beyond the rim is not
//   units:   a dart inside the double is the same share of the board counted either way
//
// testers/i1783_check.sh mutates each part and predicts which single assertion goes red.
//
//   g++ -std=c++17 -O1 -I src -I src/utils -o i1783_region_check testers/i1783_region_check.cpp
//       $(pkg-config --cflags --libs opencv4)      (one line)

#include <iostream>
#include <string>

#include "detector/geometry/detection/motion_processing.hpp"

using namespace motion_processing;

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
// rig-20260929's camera 2 -- the rig the live session ran on -- as its own MOTION REGION line
// prints it (the #1793 bakeoff's r29-dev log): the 418x656 px ellipse at (649,290) turned 91
// degrees, 215,989 px of a 1280x720 frame. Its long axis is horizontal: 328 px each side of
// the centre to the double, 435 px to the rim.
static const cv::Size kFrame(1280, 720);
static const cv::RotatedRect kCam2Double(cv::Point2f(649.f, 290.f), cv::Size2f(418.f, 656.f), 91.f);
static const int kCam2Board = 215989;
// The live entry: OD_SPIKE_THRESHOLD=0.006 (build 2b56b48's run).
static const double kLiveEntry = 0.006;
// A changed patch of 50 x 50 px: a hand and three barrels' worth on a camera 2.4 m from the
// thrower is larger, a dart's splash is of this order (#1353: 0.0135-0.089 of a board).
static const int kPatch = 50;

static cv::Mat patchAt(int cx, int cy)
{
    cv::Mat t = cv::Mat::zeros(kFrame, CV_8UC1);
    cv::rectangle(t, cv::Rect(cx - kPatch / 2, cy - kPatch / 2, kPatch, kPatch), cv::Scalar(255), cv::FILLED);
    return t;
}

static std::string share(double s)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "%.4f", s);
    return buf;
}

int main()
{
    const MotionMasks dbl = motionMasks(kFrame, kCam2Double, false);
    const MotionMasks rim = motionMasks(kFrame, kCam2Double, true);

    // The ellipse drawn is the board the live line printed, or nothing below is about it.
    say(dbl.denominator > kCam2Board - 2000 && dbl.denominator < kCam2Board + 2000,
        "pure: the double's ellipse is camera 2's board (" + std::to_string(dbl.denominator) + " px, the log's " +
            std::to_string(kCam2Board) + ")");

    // In the surround: 380 px right of the centre, between the double (328) and the rim (435).
    const cv::Mat surround = patchAt(649 + 380, 290);
    // Beyond the rim: 470 px right of the centre -- the thrower, which #1364 keeps out.
    const cv::Mat beyond = patchAt(649 + 470, 290);
    // On the board: 100 px right of the centre, a dart.
    const cv::Mat inside = patchAt(649 + 100, 290);

    const double surround_double = boardShare(surround, dbl);
    const double surround_rim = boardShare(surround, rim);
    say(surround_double <= kLiveEntry,
        "double: the hand in the surround is " + share(surround_double) +
            " of the board counted to the double, under the live entry " + share(kLiveEntry) +
            ", so the takeout opens no event (the live fault)");
    say(surround_rim > kLiveEntry,
        "rim: surround -- the same hand is " + share(surround_rim) + " counted to the rim, over the live entry " +
            share(kLiveEntry) + ", so the takeout opens its event");
    say(boardShare(beyond, rim) == 0.0,
        "rim: beyond -- a patch past the rim is " + share(boardShare(beyond, rim)) +
            " counted to the rim: the thrower beyond the board is still not motion on it");
    say(boardShare(inside, dbl) > 0.0 && boardShare(inside, dbl) == boardShare(inside, rim),
        "units: a dart inside the double is " + share(boardShare(inside, dbl)) + " of the board counted to the double and " +
            share(boardShare(inside, rim)) + " counted to the rim -- one share, one denominator");

    std::cout << "FAILURES: " << failures << std::endl;
    return failures == 0 ? 0 : 1;
}
