// turnaus#1821: a dart in the board published as an unflagged MISS by the rim-only carry.
//
// Live on casual board 20, 2026-10-11 (build 0.1.13-1798, OD_LONE_CAMERA=on; the session's
// log is kept off GitHub and the lines this check needs are quoted here, verbatim but cut):
//
//   00:59:20.147  I1707 RIM CARRIED: 3 camera(s) moved up and 1 of them cleared the floor in
//                 the scoring area, under the quorum of 2 -- ...
//   00:59:20.148  No consensus, using single camera score: D20 from camera 1
//   00:59:20.148  SCORE: D20 | ... | Camera: 1                    (a lone reading published)
//
//   00:59:28.793  I1707 RIM CARRIED: 3 camera(s) moved up and 1 of them cleared the floor in
//                 the scoring area, under the quorum of 2 -- ...
//   00:59:28.794  PATH: DEGRADED -- no geometric entry (TOO-FEW-CONSTRAINTS: no entry: 1 usable
//                 constraint(s) of 3 cameras, and one line is anywhere along itself (cam 1: no
//                 usable axis: rim only: ...; cam 2: no usable axis: rim only: ...)), ...
//   00:59:28.794  SCORE: MISS | Position: (-1,-1) | Confidence: 0.500000 | Camera: -1
//                 thrown S13; the thrower typed it (truth row `None - S13 typed vote 0 1`)
//
//   01:02:06.363  I1707 RIM CARRIED: 3 camera(s) moved up and 1 of them cleared ...
//   01:02:06.364  PATH: ... 0 usable constraint(s) of 3 cameras ... (cam 1: ... rim only ...;
//                 cam 2: no usable axis: not straight: the kept centreline scatters 3.90 px RMS
//                 about the line against a 2.5 px gate ...; cam 3: ... rim only ...)
//   01:02:08.241  I1707 RIM CARRIED: 3 camera(s) moved up and 0 of them cleared ...
//   01:02:10.312  I1707 RIM CARRIED: 3 camera(s) moved up and 1 of them cleared ...
//   01:02:10.313  PATH: ... 0 usable ... (cam 1: rim only; cam 2: not straight: 10.06 px RMS
//                 ...; cam 3: rim only)
//                 #1802's three phantom misses, a body at the board between two takeouts
//   01:02:59.085  I1707 RIM CARRIED: 2 camera(s) moved up and 0 of them cleared ...
//
// What the log does NOT say is whether camera 3 found a tip at 00:59:28, and where: no
// camera's reading reached the vote, so it found none or found one that read MISS. Both
// are held below. The kept frames (OD_KEEP_FRAMES=on, reference 01M4KX818T...) settle it.
//
// Every assertion is labelled by what can turn it red:
//   live:     the 00:59:28 window, with each tip state the log allows
//   phantom:  #1802's three windows (and 01:02:59), whatever their tips were
//   lone:     a window whose vote had a reading
//   carry:    a dart the rim-only votes did not carry
//   switch:   OD_RIM_OFFER off
//   wedge:    a tip whose wedge was not read (an asserted 20, an unanchored camera)
//   count:    two cameras clearing the scoring floor (a board of four)
// testers/i1821_check.sh mutates each clause of rim_offer::decide and predicts which go red.
//
//   g++ -std=c++17 -O1 -I src -o i1821_offer_check testers/i1821_offer_check.cpp

#include <iostream>
#include <string>
#include <vector>

#include "detector/geometry/detection/rim_offer.hpp"

using rim_offer::Offer;
using rim_offer::Witness;

static int failures = 0;

static void say(bool ok, const std::string &what)
{
    std::cout << (ok ? "OK   " : "FAIL ") << what << std::endl;
    if (!ok)
    {
        failures++;
    }
}

static Witness rim(int camera)
{
    Witness w;
    w.camera = camera;
    return w;
}

// A camera that cleared the scoring floor. `line` is whether its constraint was usable;
// `segment` is the wedge its ruler reads at its tip, 0 for a tip with no read wedge and -1
// for no tip at all.
static Witness scoring(int camera, bool line, int segment)
{
    Witness w;
    w.camera = camera;
    w.cleared_scoring = true;
    w.usable_line = line;
    w.tip_found = segment >= 0;
    w.wedge_read = segment > 0;
    w.segment = segment > 0 ? segment : -1;
    return w;
}

int main()
{
    // ---- live: 00:59:28, the S13 ---------------------------------------------------------
    {
        const Offer o = rim_offer::decide(true, true, false, {rim(0), rim(1), scoring(2, true, 13)});
        say(o.flag && o.alternative == "S13" && o.camera == 2,
            "live: 00:59:28 S13 offered S13 -- camera 3's tip in the 13 (got flag=" +
                std::to_string(o.flag) + " alt=" + o.alternative + " why=" + o.reason + ")");
    }
    {
        const Offer o = rim_offer::decide(true, true, false, {rim(0), rim(1), scoring(2, true, -1)});
        say(!o.flag && o.reason == "no-tip",
            "live: 00:59:28 with no tip on camera 3 is not offered, and says no-tip (got " + o.reason + ")");
    }

    // ---- phantom: #1802, a body at the board ---------------------------------------------
    // Whatever the scoring camera's tip was -- here the most dangerous case, a tip in a read
    // wedge -- a camera whose figure was not straight offers nothing.
    for (const char *when : {"01:02:06", "01:02:10"})
    {
        const Offer o = rim_offer::decide(true, true, false, {rim(0), scoring(1, false, 20), rim(2)});
        say(!o.flag && o.reason == "no-usable-line",
            std::string("phantom: ") + when + " (camera 2 not straight) is not offered (got " + o.reason + ")");
    }
    {
        const Offer o = rim_offer::decide(true, true, false, {rim(0), rim(1), rim(2)});
        say(!o.flag, "phantom: 01:02:08 (no camera cleared) is not offered (got " + o.reason + ")");
    }
    {
        const Offer o = rim_offer::decide(true, true, false, {rim(0), rim(1)});
        say(!o.flag, "phantom: 01:02:59 (2 up, none cleared) is not offered (got " + o.reason + ")");
    }

    // ---- lone: 00:59:20, a reading published --------------------------------------------
    {
        const Offer o = rim_offer::decide(true, true, true, {rim(0), scoring(1, true, 20), rim(2)});
        say(!o.flag && o.reason == "a-camera-reading-published",
            "lone: 00:59:20 D20 published from a camera's reading is left to #1707/#1773 (got " + o.reason + ")");
    }

    // ---- carry: not rim-carried -----------------------------------------------------------
    {
        const Offer o = rim_offer::decide(true, false, false, {rim(0), scoring(1, true, 6), rim(2)});
        say(!o.flag && o.reason == "not-rim-carried",
            "carry: a dart the rim-only votes did not carry is not offered (got " + o.reason + ")");
    }

    // ---- switch: off ---------------------------------------------------------------------
    {
        const Offer o = rim_offer::decide(false, true, false, {rim(0), rim(1), scoring(2, true, 13)});
        say(!o.flag && o.alternative == "S13" && o.reason == "would-offer-switch-off",
            "switch: off publishes no flag and the census still names S13 (got flag=" + std::to_string(o.flag) +
                " alt=" + o.alternative + " why=" + o.reason + ")");
    }

    // ---- wedge: a tip whose wedge was not read ---------------------------------------------
    {
        const Offer o = rim_offer::decide(true, true, false, {rim(0), rim(1), scoring(2, true, 0)});
        say(!o.flag && o.reason == "wedge-not-read",
            "wedge: a tip with no read wedge is not offered (got flag=" + std::to_string(o.flag) + " alt=" +
                o.alternative + " why=" + o.reason + ")");
    }

    // ---- count: two cameras cleared --------------------------------------------------------
    {
        const Offer o =
            rim_offer::decide(true, true, false, {rim(0), scoring(1, true, 13), scoring(2, true, 6), rim(3)});
        say(!o.flag && o.reason == "more-than-one-camera-cleared",
            "count: two cameras clearing the scoring floor are not one camera's offer (got " + o.reason + ")");
    }

    std::cout << "FAILURES: " << failures << std::endl;
    return failures == 0 ? 0 : 1;
}
