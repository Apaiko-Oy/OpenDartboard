// #1773: a vote reading within its sigma of a RING wire -- lone or consensus -- publishes
// flagged with the ring across the wire, at #1556's 0.7, and the score string does not
// move.
//
// The rule is `score_processing::checkVoteReadingAgainstRingWires`, pure, over the vote's
// chosen PointScore and ScoreChoice. This file holds it on the three readings the live
// session of 2026-10-10 (build 2b56b48) really produced -- the BOARD lines of the log,
// rebuilt as PointScores -- and on the cases the rule must leave alone.
//
//   g++ -std=c++17 -I src -I src/utils -o ringwire_check testers/i1773_ringwire_check.cpp
//       $(pkg-config --cflags --libs opencv4)
//
// THREE PREFIXES, and the mutation harness (i1773_check.sh) counts them:
//   pure:  structure that no ring arithmetic touches -- a MISS has no margin, a reading
//          without a radius has none, a wedge-by-default reading is not asked, the
//          naming of the score across a wire;
//   ring:  an assertion whose figure IS the ring arithmetic (a margin, a wire, a flag
//          that depends on one). Every one of these carries a millimetre figure, so a
//          mutation of the radius arithmetic turns every one of them red -- the check
//          prints RING-SENSITIVE n= so the prediction is a count and not a guess;
//   both:  the consensus case, which the lone-only mutation turns red and nothing else.
//
// The three live darts, from the log (BOARD lines; `camera N` in the log is 0-based):
//   15:51:49  T19 lone camera 1, ring=triple segment=19 radius=0.600382 angle=198.876022,
//             thrown S19. 102.1 mm from the bull: 3.1 mm inside the treble's inner wire
//             (99), 4.9 mm inside its outer (107); LONE-WIRE said 14.4 mm from a wedge wire.
//   16:06:25  OUTER lone camera 0, ring=outer radius=0.050671 angle=195.488434, thrown
//             S1. 8.6 mm: 2.3 mm outside the bull's wire (6.35), 7.3 inside the 25's (15.9).
//   16:47:22  OUTER, 2 cameras agreeing, ring=outer radius=0.040479, thrown BULL. 6.9 mm:
//             0.5 mm outside the bull's wire.

#include <cmath>
#include <iostream>
#include <string>
#include <vector>

#include "detector/geometry/detection/score_processing.hpp"

using namespace score_processing;

static int failures = 0;
static int ringSensitive = 0;
static int consensusSensitive = 0;

static void say(bool ok, const std::string &what)
{
    std::cout << (ok ? "OK   " : "FAIL ") << what << std::endl;
    if (!ok)
    {
        failures++;
    }
    if (what.rfind("ring:", 0) == 0)
    {
        ringSensitive++;
    }
    if (what.rfind("both:", 0) == 0)
    {
        consensusSensitive++;
    }
}

static bool near(float a, float b, float tol)
{
    return std::fabs(a - b) <= tol;
}

static PointScore reading(const std::string &score, const std::string &ring, int segment, float radius,
                          float angle = -1.0f)
{
    PointScore p;
    p.score = score;
    p.ring = ring;
    p.segment = segment;
    p.wedge_measured = segment > 0;
    p.ring_only = segment < 1 && (ring == "bull" || ring == "outer");
    p.board.has_radius = radius >= 0.0f;
    p.board.radius = radius;
    p.board.has_angle = angle >= 0.0f;
    p.board.angle = angle;
    return p;
}

static ScoreChoice choice(int camera, int agreeing, float confidence, bool by_default = false)
{
    ScoreChoice c;
    c.camera = camera;
    c.agreeing = agreeing;
    c.confidence = confidence;
    c.by_default = by_default;
    return c;
}

int main()
{
    std::cout << "sigma=" << kLoneReadingSigmaMm << " scoring radius=" << kScoringRadiusMm << std::endl;

    // ---- the three live darts ---------------------------------------------------------
    const PointScore liveT19 = reading("T19", "triple", 19, 0.600382f, 198.876022f);
    const PointScore liveOuterLone = reading("OUTER", "outer", -1, 0.050671f, 195.488434f);
    const PointScore liveOuterPair = reading("OUTER", "outer", -1, 0.040479f);

    {
        const RingWireMargin m = ringWireMarginMm(liveT19);
        say(near(m.radiusMm, 102.06f, 0.05f) && near(m.marginMm, 3.06f, 0.05f) && near(m.wireMm, 99.0f, 0.01f) &&
                m.across == "single",
            "ring: live T19 at radius 0.600382 is 102.1 mm from the bull, 3.1 mm inside the treble's inner wire (99), single across");
        say(near(wedgeWireMarginMm(liveT19), 14.4f, 0.1f),
            "pure: and 14.4 mm from its nearest wedge wire -- #1628's figure, the one LONE-WIRE printed live");
    }
    {
        const RingWireMargin m = ringWireMarginMm(liveOuterLone);
        say(near(m.radiusMm, 8.61f, 0.05f) && near(m.marginMm, 2.26f, 0.05f) && near(m.wireMm, 6.35f, 0.01f) &&
                m.across == "bull",
            "ring: live lone OUTER at 0.050671 is 8.6 mm out, 2.3 mm from the bull's wire (6.35), bull across; the 25's wire is 7.3 away");
    }
    {
        const RingWireMargin m = ringWireMarginMm(liveOuterPair);
        say(near(m.radiusMm, 6.88f, 0.05f) && near(m.marginMm, 0.53f, 0.05f) && m.across == "bull",
            "ring: live consensus OUTER at 0.040479 is 6.9 mm out, 0.5 mm from the bull's wire");
    }

    // (1) the lone T19: flagged T19 or S19, at 0.7, and the sentence says so.
    {
        const RingWireCall c = checkVoteReadingAgainstRingWires(liveT19, choice(1, 1, 0.7f));
        say(c.checked && c.near_wire && c.flagged && c.alternative == "S19" && near(c.confidence, 0.7f, 1e-6f),
            "ring: live (1) the lone T19 3.1 mm inside the treble's inner wire publishes flagged with S19 at 0.7");
        say(c.account.find("RING-WIRE: camera 1's T19 (alone) sits 3.1 mm from the treble's inner wire") == 0 &&
                c.account.find("publishes flagged with S19 across that wire at 0.7") != std::string::npos &&
                c.account.find("wedge wire") == std::string::npos,
            "ring: live (1) the RING-WIRE sentence names the wire, the margin and the alternative, and not the wedge wire 14.4 mm away");
        // The LONE-WIRE sentence, from the same reading: names both wires and the nearer.
        const std::vector<PointScore> pts = {PointScore(), liveT19, PointScore()};
        const std::vector<bool> mv = {false, true, false};
        const LoneWireCheck l = checkLoneReadingAgainstWires(pts, mv, choice(1, 1, 0.7f), true);
        say(l.checked && !l.near_wire &&
                l.account == "LONE-WIRE: camera 1's T19 is 14.4 mm from a wedge wire and 3.1 mm from the treble's inner wire (the ring wire is nearer), clear of the 5 mm sigma",
            "ring: live (1) LONE-WIRE now reads '14.4 mm from a wedge wire and 3.1 mm from the treble's inner wire (the ring wire is nearer)'");
        say(l.choice.camera == 1 && !l.reselected,
            "pure: live (1) the lone reading still publishes -- the ring check moves no choice");
    }
    // (2) the lone OUTER 2.3 mm from the bull's wire: flagged OUTER or BULL.
    {
        const RingWireCall c = checkVoteReadingAgainstRingWires(liveOuterLone, choice(0, 1, 0.7f));
        say(c.checked && c.near_wire && c.flagged && c.alternative == "BULL" && near(c.confidence, 0.7f, 1e-6f),
            "ring: live (2) the lone OUTER 2.3 mm from the bull's wire publishes flagged with BULL at 0.7");
        say(c.account.find("camera 0's OUTER (alone) sits 2.3 mm from the bull's wire") != std::string::npos,
            "ring: live (2) the sentence names the bull's wire at 2.3 mm");
        // And #1628's check, which returned before it said anything about this reading.
        const std::vector<PointScore> pts = {liveOuterLone, PointScore(), PointScore()};
        const std::vector<bool> mv = {true, false, false};
        const LoneWireCheck l = checkLoneReadingAgainstWires(pts, mv, choice(0, 1, 0.7f), true);
        say(!l.checked && l.account.empty(),
            "pure: live (2) LONE-WIRE still says nothing about a ring-only reading -- the ring check is what speaks");
    }
    // (3) the consensus OUTER 0.5 mm from the bull's wire: flagged, demoted 0.9 -> 0.7.
    {
        const RingWireCall c = checkVoteReadingAgainstRingWires(liveOuterPair, choice(0, 2, 0.9f));
        say(c.checked && c.near_wire && c.flagged && c.alternative == "BULL" && near(c.confidence, 0.7f, 1e-6f),
            "both: live (3) the two-camera OUTER 0.5 mm from the bull's wire publishes flagged with BULL, demoted 0.9 -> 0.7");
        say(c.account.find("camera 0's OUTER (2 cameras agreeing) sits 0.5 mm from the bull's wire") != std::string::npos,
            "both: live (3) the sentence says two cameras agreed and the wire is 0.5 mm away");
    }

    // ---- the margins, ring by ring ----------------------------------------------------
    {
        const RingWireMargin m = ringWireMarginMm(reading("S19", "single", 19, 95.2f / 170.0f));
        say(near(m.marginMm, 3.8f, 0.05f) && near(m.wireMm, 99.0f, 0.01f) && m.across == "triple",
            "ring: a single at 95.2 mm is 3.8 mm from the treble's inner wire, triple across");
    }
    {
        const RingWireMargin m = ringWireMarginMm(reading("S19", "single", 19, 18.7f / 170.0f));
        say(near(m.marginMm, 2.8f, 0.05f) && near(m.wireMm, 15.9f, 0.01f) && m.across == "outer",
            "ring: a single at 18.7 mm is 2.8 mm from the 25 ring's wire, outer across");
    }
    {
        const RingWireMargin m = ringWireMarginMm(reading("S19", "single", 19, 136.0f / 170.0f));
        say(near(m.marginMm, 26.0f, 0.05f) && near(m.wireMm, 162.0f, 0.01f) && m.across == "double",
            "ring: a single at 136 mm is 26 mm from the double's inner wire (nearer than the treble's outer at 29)");
    }
    {
        const RingWireMargin m = ringWireMarginMm(reading("D20", "double", 20, 169.0f / 170.0f));
        say(near(m.marginMm, 1.0f, 0.05f) && near(m.wireMm, 170.0f, 0.01f) && m.across == "miss",
            "ring: a double at 169 mm is 1.0 mm from the double's outer wire, MISS across");
    }
    {
        const RingWireMargin m = ringWireMarginMm(reading("D20", "double", 20, 163.2f / 170.0f));
        say(near(m.marginMm, 1.2f, 0.05f) && near(m.wireMm, 162.0f, 0.01f) && m.across == "single",
            "ring: a double at 163.2 mm is 1.2 mm from the double's inner wire, single across");
    }
    {
        const RingWireMargin m = ringWireMarginMm(reading("BULL", "bull", -1, 5.1f / 170.0f));
        say(near(m.marginMm, 1.25f, 0.05f) && near(m.wireMm, 6.35f, 0.01f) && m.across == "outer",
            "ring: a bull at 5.1 mm is 1.25 mm from the bull's wire, outer across");
    }
    {
        // The ellipse says treble, the ruler says 95 mm: the margin is to the edge the
        // ruler has crossed, and the alternative is the ring on its far side.
        const RingWireMargin m = ringWireMarginMm(reading("T19", "triple", 19, 95.0f / 170.0f));
        say(near(m.marginMm, 4.0f, 0.05f) && near(m.wireMm, 99.0f, 0.01f) && m.across == "single",
            "ring: a treble by the ellipses at a ruler radius of 95 mm is 4.0 mm across the treble's inner wire, single across");
    }
    say(ringWireMarginMm(PointScore()).marginMm < 0.0f, "pure: a MISS has no ring margin");
    say(ringWireMarginMm(reading("S20", "single", 20, -1.0f)).marginMm < 0.0f,
        "pure: a reading without a radius has no ring margin");

    // ---- naming the score across the wire ---------------------------------------------
    say(scoreAcrossRingWire(liveT19, "single") == "S19" &&
            scoreAcrossRingWire(reading("D5", "double", 5, 0.99f), "miss") == "MISS" &&
            scoreAcrossRingWire(liveOuterLone, "bull") == "BULL" &&
            scoreAcrossRingWire(reading("BULL", "bull", -1, 0.03f), "outer") == "OUTER",
        "pure: the score across a wire is S19 for a T19, MISS past a double, BULL and OUTER across the bull's wire");
    say(scoreAcrossRingWire(liveOuterLone, "single").empty(),
        "pure: a ring-only OUTER cannot name the single across the 25 ring's wire -- it never asked the angular ruler");

    // ---- what the rule must leave alone -----------------------------------------------
    {
        // 12 mm clear of every ring wire: said, not flagged, confidence untouched.
        const PointScore p = reading("S20", "single", 20, 87.0f / 170.0f, 3.0f);
        const RingWireCall c = checkVoteReadingAgainstRingWires(p, choice(0, 1, 0.7f));
        say(c.checked && !c.near_wire && !c.flagged && c.alternative.empty() && near(c.confidence, 0.7f, 1e-6f) &&
                near(c.ring.marginMm, 12.0f, 0.05f) && c.account.find("12.0 mm from the treble's inner wire") != std::string::npos &&
                c.account.find("clear of the 5 mm sigma") != std::string::npos,
            "ring: a single 12 mm clear of every ring wire is said clear and not flagged, at its own 0.7");
    }
    {
        // The wedge wire is nearer than the ring wire and both are inside the sigma: the
        // ring across is offered, the sentence says the wedge wire is there and nearer,
        // and LONE-WIRE names the wedge wire as the nearer.
        const PointScore p = reading("T19", "triple", 19, 0.600382f, 189.5f); // 0.5 deg past the 3/19 wire
        const float wedge = wedgeWireMarginMm(p);
        say(wedge > 0.5f && wedge < 1.5f, "pure: a T19 0.5 deg past the 3/19 wire at 102 mm is ~0.9 mm from that wedge wire (#1628's arithmetic)");
        const RingWireCall c = checkVoteReadingAgainstRingWires(p, choice(1, 1, 0.7f));
        say(c.flagged && c.alternative == "S19" &&
                c.account.find("its nearest wedge wire is inside the sigma too, at 0.9 mm and nearer, said by LONE-WIRE and not offered (#1628)") != std::string::npos,
            "ring: both wires inside the sigma -- S19 across the ring wire is offered and the sentence says the wedge wire is nearer");
        const std::vector<PointScore> pts = {PointScore(), p, PointScore()};
        const std::vector<bool> mv = {false, true, false};
        const LoneWireCheck l = checkLoneReadingAgainstWires(pts, mv, choice(1, 1, 0.7f), true);
        say(l.near_wire && l.account.find("0.9 mm from a wedge wire and 3.1 mm from the treble's inner wire (the wedge wire is nearer)") != std::string::npos,
            "ring: and LONE-WIRE names the wedge wire as the nearer, with both margins");
    }
    {
        // The wedge wire inside the sigma and the ring wire clear: #1628's sentence, with
        // the ring margin beside it; the ring check says clear and flags nothing.
        const PointScore p = reading("S19", "single", 19, 85.0f / 170.0f, 189.5f);
        const RingWireCall c = checkVoteReadingAgainstRingWires(p, choice(1, 1, 0.7f));
        say(c.checked && !c.near_wire && !c.flagged && near(c.ring.marginMm, 14.0f, 0.05f),
            "ring: a wedge-wire margin inside the sigma with the ring wire 14 mm clear flags nothing on the ring");
        const std::vector<PointScore> pts = {PointScore(), p, PointScore()};
        const std::vector<bool> mv = {false, true, false};
        const LoneWireCheck l = checkLoneReadingAgainstWires(pts, mv, choice(1, 1, 0.7f), true);
        say(l.near_wire && l.account.find("14.0 mm from the treble's inner wire (the wedge wire is nearer)") != std::string::npos,
            "ring: LONE-WIRE still names the wedge wire when it is the nearer, and gives the ring margin");
    }
    {
        // A ring-only OUTER near the 25 ring's wire: inside the sigma, unnameable, unflagged.
        const PointScore p = reading("OUTER", "outer", -1, 14.96f / 170.0f);
        const RingWireCall c = checkVoteReadingAgainstRingWires(p, choice(0, 1, 0.7f));
        say(c.checked && c.near_wire && !c.flagged && c.alternative.empty() && near(c.confidence, 0.7f, 1e-6f) &&
                c.account.find("0.9 mm from the 25 ring's wire") != std::string::npos &&
                c.account.find("the single across it cannot be named") != std::string::npos,
            "ring: an OUTER 0.9 mm inside the 25 ring's wire is inside the sigma, cannot name its single, and publishes unflagged saying so");
    }
    {
        // #1707's shape, now by default: a lone D20 near the outer wire offers MISS, near
        // the inner wire offers S20.
        const RingWireCall outer = checkVoteReadingAgainstRingWires(reading("D20", "double", 20, 0.979f, 3.0f), choice(0, 1, 0.7f));
        const RingWireCall inner = checkVoteReadingAgainstRingWires(reading("D20", "double", 20, 0.955f, 3.0f), choice(0, 1, 0.7f));
        say(outer.flagged && outer.alternative == "MISS" && inner.flagged && inner.alternative == "S20",
            "ring: a D20 at 0.979 offers MISS across the outer wire; at 0.955 it offers S20 across the inner");
    }
    {
        // A wedge-by-default reading is not asked: it already publishes at 0.5 as
        // "nobody measured the wedge", and S20-by-default is no alternative to T20-by-default.
        PointScore p = reading("T20", "triple", 20, 0.600382f, 3.0f);
        p.wedge_measured = false;
        p.wedge_asserted = true;
        const RingWireCall c = checkVoteReadingAgainstRingWires(p, choice(0, 0, 0.5f, true));
        say(!c.checked && !c.flagged && c.account.empty() && near(c.confidence, 0.5f, 1e-6f),
            "pure: a wedge-by-default reading is not asked and keeps its 0.5");
    }
    {
        const RingWireCall c = checkVoteReadingAgainstRingWires(PointScore(), choice(-1, 0, 0.5f));
        say(!c.checked && !c.flagged && c.account.empty(), "pure: a vote with no camera is not asked");
    }
    {
        // The census line, in the shape i1773_census.py parses.
        const ScoreChoice ch = choice(1, 1, 0.7f);
        const RingWireCall c = checkVoteReadingAgainstRingWires(liveT19, ch);
        const std::string line = ringWireCensusLine(7, ch, liveT19, c);
        say(line.find("I1773RING window=7 agreeing=1 cam=2 score=T19 ring=triple radius_mm=102.06 wire_mm=99.00 margin=3.06 wedge_margin=14.4") == 0 &&
                line.find(" checked=1 near=1 flagged=1 alt=S19 conf=0.70") != std::string::npos,
            "ring: the I1773RING census line carries the radius, the wire, both margins, the flag and the alternative");
    }

    std::cout << "RING-SENSITIVE n=" << ringSensitive << " CONSENSUS n=" << consensusSensitive << std::endl;
    std::cout << "FAILURES: " << failures << std::endl;
    return failures == 0 ? 0 : 1;
}
