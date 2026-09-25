// #1628: a lone reading within its sigma of a wedge wire, and the reselection that was
// measured and refused (OD_LONE_WIRE=clear keeps it reachable).
//
// The rule is `score_processing::checkLoneReadingAgainstWires`, pure, applied after
// `chooseScore` so the vote and #1346's and #1517's checks of it are untouched. This file
// holds it on the readings rig-20260922 dev v7.2 really produced (the I1628LONE line of
// the run with OD_LOOK_BUDGET=1605 + OD_SEEK_ALIGN=1618, BOARD lines for the angles) and
// on the cases the rule must leave alone.
//
//   g++ -std=c++17 -I src -I src/utils -o wire_check testers/i1628_wire_check.cpp \
//       $(pkg-config --cflags --libs opencv4)
//   wire_check tree      -- the default: the check says how close, #1517's fallback publishes
//   wire_check clear     -- run under OD_LONE_WIRE=clear: the refused reselection, reachable

#include <cmath>
#include <iostream>
#include <string>
#include <vector>

#include "detector/geometry/detection/score_processing.hpp"

using namespace score_processing;

static int failures = 0;

static void say(bool ok, const std::string &what)
{
    std::cout << (ok ? "OK   " : "FAIL ") << what << std::endl;
    if (!ok)
    {
        failures++;
    }
}

static PointScore reading(const std::string &score, int segment, float radius, float angle,
                          bool complete = true)
{
    PointScore p;
    p.score = score;
    p.ring = "single";
    p.segment = segment;
    p.wedge_measured = true;
    p.rings_complete = complete;
    p.board.has_radius = true;
    p.board.radius = radius;
    p.board.has_angle = true;
    p.board.angle = angle;
    return p;
}

int main(int argc, char **argv)
{
    const std::string mode = argc > 1 ? argv[1] : "tree";
    const bool reselectOn = loneWireReselectIsOn();
    std::cout << "MODE " << mode << " OD_LONE_WIRE=clear " << (reselectOn ? 1 : 0)
              << " sigma=" << kLoneReadingSigmaMm << std::endl;
    const std::vector<bool> all3 = {true, true, false};

    // v7.2, rig-20260922 dev, both switches on: camera 1 S19 at 189.69 deg, radius
    // 0.126; camera 2 S3 at 180.14 deg, radius 0.352 (the same tip, tipR 54.6 mm, that
    // published S3 alone in the default dev window); camera 3's tip 285 mm out, a MISS
    // that casts no vote.
    const PointScore cam1 = reading("S19", 19, 0.126045f, 189.691330f);
    const PointScore cam2 = reading("S3", 3, 0.352392f, 180.144089f);
    PointScore cam3;
    const std::vector<PointScore> v72 = {cam1, cam2, cam3};

    say(std::fabs(wedgeWireMarginMm(cam1) - 0.26f) < 0.02f,
        "pure: camera 1's S19 is 0.26 mm of arc past the 3/19 wire (0.69 deg at 21.4 mm)");
    say(wedgeWireMarginMm(cam2) > 9.0f && wedgeWireMarginMm(cam2) < 9.5f,
        "pure: camera 2's S3 is ~9.3 mm from its nearest wedge wire");
    say(wedgeWireMarginMm(cam3) < 0.0f, "pure: a MISS has no wedge margin");

    const ScoreChoice vote = chooseScore(v72, all3);
    say(vote.camera == 0 && vote.agreeing == 1,
        "pure: the vote has no consensus and #1517's fallback takes camera 1 (index 0)");

    const LoneWireCheck c = checkLoneReadingAgainstWires(v72, all3, vote, !reselectOn);
    say(c.checked && c.near_wire, "pure: camera 1's reading is inside the sigma");
    say(c.choice.confidence == 0.7f && c.choice.agreeing == 1,
        "pure: whichever camera publishes, it is a lone reading at 0.7 -- nothing agreed");
    if (mode == "tree")
    {
        // The default: the reselection was measured 1:1 over both fixtures and both
        // windows (rig-22 dev v7.2 gained, rig-22 opening v7.2 lost) and is refused.
        say(!c.reselected && c.choice.camera == 0 && v72[c.choice.camera].score == "S19",
            "tree: v7.2 still publishes camera 1's S19 -- #1517's fallback is the default");
        say(c.account.find("the reselection is off") != std::string::npos,
            "tree: the account says the lone reading was inside the sigma and why it stands");
    }
    else
    {
        say(reselectOn, "clear: OD_LONE_WIRE=clear is read");
        say(c.reselected && c.choice.camera == 1 && v72[c.choice.camera].score == "S3",
            "clear: v7.2 publishes camera 2's S3");
        say(c.account.find("publishes instead") != std::string::npos,
            "clear: the account says the lone reading was replaced");
    }

    // What the rule must leave alone, in every mode.
    {
        // The alternative is near a wire too: nothing is clear, the fallback stands.
        const std::vector<PointScore> pts = {cam1, reading("S3", 3, 0.35f, 171.5f), cam3};
        const LoneWireCheck c =
            checkLoneReadingAgainstWires(pts, all3, chooseScore(pts, all3), false);
        say(c.near_wire && !c.reselected && c.choice.camera == 0,
            "pure: no clear alternative -- the near reading stands");
    }
    {
        // The fallback's reading is clear: nothing to check against.
        const std::vector<PointScore> pts = {reading("S19", 19, 0.3f, 198.0f), cam2, cam3};
        const LoneWireCheck c =
            checkLoneReadingAgainstWires(pts, all3, chooseScore(pts, all3), false);
        say(c.checked && !c.near_wire && !c.reselected && c.choice.camera == 0,
            "pure: a fallback reading clear of its wires stands");
    }
    {
        // A consensus is never touched, however close to a wire.
        const std::vector<PointScore> pts = {cam1, reading("S19", 19, 0.13f, 189.5f), cam2};
        const std::vector<bool> v = {true, true, true};
        const ScoreChoice ch = chooseScore(pts, v);
        const LoneWireCheck c = checkLoneReadingAgainstWires(pts, v, ch, false);
        say(ch.agreeing == 2 && !c.checked && !c.reselected && c.choice.camera == ch.camera,
            "pure: a consensus is not checked");
    }
    {
        // #1517 is not undone: an incomplete camera does not replace a complete one.
        const std::vector<PointScore> pts = {cam1, reading("S3", 3, 0.352f, 180.1f, false), cam3};
        const LoneWireCheck c =
            checkLoneReadingAgainstWires(pts, all3, chooseScore(pts, all3), false);
        say(c.near_wire && !c.reselected, "pure: an incomplete camera never replaces a complete one");
    }
    {
        // A camera that did not vote is not a candidate.
        const std::vector<bool> v = {true, false, false};
        const LoneWireCheck c =
            checkLoneReadingAgainstWires(v72, v, chooseScore(v72, v), false);
        say(!c.reselected && c.choice.camera == 0, "pure: a camera that did not vote is not a candidate");
    }
    {
        // A ring-only fallback (a BULL) measured no wedge and is not checked.
        PointScore bull;
        bull.score = "BULL";
        bull.ring = "bull";
        bull.ring_only = true;
        bull.board.has_radius = true;
        bull.board.radius = 0.02f;
        bull.board.has_angle = true;
        bull.board.angle = 189.9f;
        const std::vector<PointScore> pts = {bull, cam2, cam3};
        const LoneWireCheck c =
            checkLoneReadingAgainstWires(pts, all3, chooseScore(pts, all3), false);
        say(!c.checked && !c.reselected, "pure: a ring-only reading is never checked against a wedge wire");
    }

    std::cout << "FAILURES: " << failures << std::endl;
    return failures == 0 ? 0 : 1;
}
