// #1517: a camera that cannot tell every ring apart does not stand alone for the vote.
//
// #1485 zeroes a ring the band check refused, and a zeroed ellipse contains no point --
// so a camera whose treble ring was refused reads every treble as the single at the same
// radius, forever, with nothing downstream saying so. `chooseScore`'s no-consensus
// fallback then took `readings[0]` BY INDEX: a calibration failure on the lowest-index
// camera outranked a whole camera's T20 with its own structural S20, on every treble of
// every visit, and the published 0.7 read exactly like any other lone reading.
//
// `PointScore::rings_complete` is the fact (set in scorePoint off the ellipses
// themselves, because a refused ring IS a zeroed ellipse), and the preference is one
// loop: where no two cameras agree, the first camera that fitted every ring wins, index
// order among the complete, and the first of all only when none is complete.
// `ScoreChoice::preferred_complete` is the account -- true exactly where the preference
// DECIDED, so the vote's log line can say it did.
//
// What is deliberately NOT here: a consensus is left alone. Two cameras agreeing is the
// stronger evidence even when both are incomplete -- re-weighing agreement by ring
// completeness would be a new vote, #797's open territory -- and the asserted-wedge
// fallback is left alone too: a complete camera whose wedge was asserted is still a
// constant, and the preference never promotes a constant over a measurement.
//
// The design is from the rescued Codex work (wip-rescue-rings, 4bc3bd9); this file holds
// it the way i1346 holds the vote it extends.
//
//   g++ -std=c++17 -I src -I src/utils -o vote_check testers/i1517_vote_check.cpp \
//       $(pkg-config --cflags --libs opencv4)

#include <iostream>
#include <string>
#include <vector>

#include "detector/geometry/detection/score_processing.hpp"

using score_processing::chooseScore;
using score_processing::PointScore;
using score_processing::ScoreChoice;

static int failures = 0;

static void say(bool ok, const std::string &what)
{
    std::cout << (ok ? "OK   " : "FAIL ") << what << std::endl;
    if (!ok)
    {
        failures++;
    }
}

static PointScore measured(const std::string &score, bool complete)
{
    PointScore p;
    p.score = score;
    p.wedge_measured = true;
    p.wedge_asserted = false;
    p.rings_complete = complete;
    return p;
}

static PointScore asserted(const std::string &score, bool complete)
{
    PointScore p;
    p.score = score;
    p.wedge_measured = false;
    p.wedge_asserted = true;
    p.rings_complete = complete;
    return p;
}

static PointScore ringOnly(const std::string &score, bool complete)
{
    // A BULL or an OUTER: the ring ellipses scored it and no wedge entered it (#1489).
    PointScore p;
    p.score = score;
    p.wedge_measured = false;
    p.wedge_asserted = false;
    p.ring_only = true;
    p.rings_complete = complete;
    return p;
}

int main()
{
    const std::vector<bool> all(3, true);

    // ---- the defect's own scene: a refused treble ring, replayed as data ---------------
    // Camera 0's treble ring was zeroed, so the dart in the treble 20 read S20 there;
    // camera 1 fitted every ring and read T20. No two agree. By index alone the
    // structurally-wrong S20 publishes; the complete camera must win instead.
    {
        const ScoreChoice c = chooseScore(
            {measured("S20", false), measured("T20", true), asserted("S20", true)}, all);
        say(c.camera == 1, "a complete camera beats an incomplete one at no-consensus");
        say(c.confidence == 0.7f && c.agreeing == 1,
            "and still stands alone at 0.7 -- the preference moves the choice, never the count");
        say(c.preferred_complete,
            "and the choice SAYS the preference decided, so the vote's account can");
    }

    // ---- index order among the equally complete ----------------------------------------
    {
        const ScoreChoice c = chooseScore(
            {measured("S5", true), measured("S6", true), asserted("S20", true)}, all);
        say(c.camera == 0 && c.confidence == 0.7f,
            "all complete: the lowest index stands, #797's rule untouched");
        say(!c.preferred_complete,
            "and nothing claims a preference that never decided anything");
    }

    // ---- NO camera complete: back to index, said as such -------------------------------
    {
        const ScoreChoice c = chooseScore(
            {measured("S5", false), measured("S6", false), asserted("S20", true)}, all);
        say(c.camera == 0 && c.confidence == 0.7f,
            "none complete: the lowest index still stands -- the fallback fills, it never abstains");
        say(!c.preferred_complete,
            "and preferred_complete is false: readings[0] was not passed over for being incomplete");
    }

    // ---- a consensus is left alone, deliberately ---------------------------------------
    // Two incomplete cameras agreeing really did read the same thing twice; re-weighing
    // agreement by completeness would be a different vote (#797), not this issue's.
    {
        const ScoreChoice c = chooseScore(
            {measured("S20", false), measured("S20", false), measured("T20", true)}, all);
        say(c.camera == 0 && c.agreeing == 2 && c.confidence == 0.9f && !c.preferred_complete,
            "two agreeing incomplete cameras keep their consensus; the preference is no-consensus only");
    }

    // ---- the preference never promotes an assertion ------------------------------------
    // The complete camera's wedge was asserted: it is a constant, and constants only ever
    // fill the void (#1346). The incomplete measurement still wins at 0.7.
    {
        const ScoreChoice c = chooseScore(
            {measured("S12", false), asserted("S20", true), asserted("S20", true)}, all);
        say(c.camera == 0 && c.confidence == 0.7f && !c.by_default,
            "an incomplete measurement still beats a complete assertion -- readings only");
    }

    // ---- composition with ring_only (#1489): the two fields are two facts --------------
    // A bull from a complete camera against a single from an incomplete one: the complete
    // ring-only reading wins the fallback, and the choice reports the winner's own kind.
    {
        const ScoreChoice c = chooseScore(
            {measured("S3", false), ringOnly("BULL", true), asserted("S20", true)}, all);
        say(c.camera == 1 && c.preferred_complete,
            "a complete ring-only reading is preferred over an incomplete wedge reading");
        say(c.ring_only,
            "and ring_only reports the reading that WON, so the two fields compose");
    }

    // ---- a complete camera that may not vote is not in the vote ------------------------
    {
        const ScoreChoice c = chooseScore(
            {measured("S20", false), measured("T20", true), asserted("S20", true)},
            {true, false, true});
        say(c.camera == 0 && !c.preferred_complete,
            "a complete camera without a vote (no tip, or a MISS) cannot be preferred into one");
    }

    std::cout << (failures ? "FAILURES: " : "ALL OK: ") << failures << std::endl;
    return failures ? 1 : 0;
}
