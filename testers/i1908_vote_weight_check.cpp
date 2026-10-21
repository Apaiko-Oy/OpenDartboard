// turnaus#1908: OD_VOTE_WEIGHT=fit -- the string vote weighs each camera by its own evidence.
//
// chooseScore counts cameras. On 2026-10-20 a D12 read S12 at radius 0.85 and a D9 read S9
// "from 3 cameras agreeing" on the degraded path, and the vote had no way to say that one
// camera saw the dart better than two others. weighVote (score_processing.hpp) gives each
// reading a weight from its camera's axis verdict (the I1911 REFUSALS word and residual),
// its ring set (#1517's rings_complete) and its calibration's twenty-fold coherence, and the
// heaviest score string replaces the majority's only where it is a different string, beats
// it by the tie margin, reaches the floor, and was seen in the scoring area by one camera.
//
// The issue's acceptance: two rim-only cameras agreeing on S12 do not outvote one scoring-area
// camera reading D12 when its weight is higher, and with the weights ignored they do.
//
//   g++ -std=c++17 -I src -I src/utils -o vote_weight testers/i1908_vote_weight_check.cpp
//       $(pkg-config --cflags --libs opencv4)
//
// Every assertion prints `OK   <label>: ...` or `FAIL <label>: ...`; testers/i1908_check.sh
// counts the FAILs per label under each mutation against a prediction stated first.

#include <cmath>
#include <cstdlib>
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

static bool close_to(double a, double b) { return std::fabs(a - b) < 1e-6; }

static PointScore reading(const std::string &score, bool complete = true)
{
    PointScore p;
    p.score = score;
    p.wedge_measured = true;
    p.rings_complete = complete;
    return p;
}

static VoteEvidence seen(const std::string &reason, double rms = 0.5, double sectors = -1.0)
{
    VoteEvidence e;
    e.reason = reason;
    e.rms_px = rms;
    e.gate_px = 2.5;
    e.sectors_r = sectors;
    return e;
}

static std::string published(const std::vector<PointScore> &p, const ScoreChoice &c)
{
    return c.camera >= 0 ? p[c.camera].score : std::string("none");
}

int main()
{
    // ---- parse: off is the default, `fit` the only word ---------------------------------
    unsetenv("OD_VOTE_WEIGHT");
    say(voteWeightMode() == VoteWeightMode::Off, "parse: unset OD_VOTE_WEIGHT is Off");
    say(voteWeightModeFrom(nullptr) == VoteWeightMode::Off && voteWeightModeFrom("fit") == VoteWeightMode::Fit &&
            voteWeightModeFrom("on") == VoteWeightMode::Off && voteWeightModeFrom("FIT") == VoteWeightMode::Off &&
            voteWeightModeFrom("") == VoteWeightMode::Off,
        "parse: exactly `fit` is Fit; on, FIT and empty are Off");

    // ---- issue: two rim-only S12 against one scoring-area D12 ----------------------------
    {
        std::vector<PointScore> p = {reading("S12"), reading("S12"), reading("D12")};
        std::vector<bool> v = {true, true, true};
        std::vector<VoteEvidence> e = {seen("rim-only", 0.0), seen("rim-only", 0.0), seen("ok", 0.6)};
        const ScoreChoice majority = chooseScore(p, v);
        say(published(p, majority) == "S12" && majority.agreeing == 2,
            "issue: control -- the majority alone publishes S12 from 2 cameras (got " + published(p, majority) + ")");
        const WeightedVote w = weighVote(p, v, e, majority);
        say(published(p, w.choice) == "D12" && w.overturned && w.choice.camera == 2 && w.choice.agreeing == 1 &&
                close_to(w.choice.confidence, 0.7f),
            "issue: weighted, camera 3's D12 (1.00) outweighs two rim-only S12 (0.25 each) and publishes alone at 0.7"
            " (got " + published(p, w.choice) + ", why=" + w.why + ")");
        say(w.weights == "S12=0.50[cam1 0.25,cam2 0.25] D12=1.00[cam3 1.00]",
            "account: the weights are said per string and per camera (got `" + w.weights + "`)");
        say(weightedVoteCensusLine(7, "vote", "S12", w) ==
                "I1908VOTE window=7 path=vote majority=S12 weighted=D12 overturned=1 why=overturned "
                "weights=S12=0.50[cam1 0.25,cam2 0.25] D12=1.00[cam3 1.00]",
            "account: the I1908VOTE census line (got `" + weightedVoteCensusLine(7, "vote", "S12", w) + "`)");
    }

    // ---- weight: the three factors -------------------------------------------------------
    say(close_to(voteWeightOf(reading("S1"), seen("ok", 1.2)), 1.0), "weight: an admitted line, every ring, unmeasured R = 1.00");
    say(close_to(voteWeightOf(reading("S1"), seen("not-straight", 5.0)), 0.5),
        "weight: not straight at 5.00 px against the 2.50 px gate = 0.50");
    say(close_to(voteWeightOf(reading("S1"), seen("rim-only", 0.0)), 0.25), "weight: rim only = 0.25");
    say(close_to(voteWeightOf(reading("S1", false), seen("ok", 1.2, 0.8)), 0.4),
        "weight: a zeroed ring halves it and R=0.80 scales it: 1.00 x 0.50 x 0.80 = 0.40");

    // ---- keep: where the weighting publishes exactly what the majority would ------------
    {
        std::vector<PointScore> p = {reading("S20"), reading("S20"), reading("T20")};
        std::vector<bool> v = {true, true, true};
        std::vector<VoteEvidence> e = {seen("ok"), seen("ok"), seen("ok")};
        const ScoreChoice m = chooseScore(p, v);
        const WeightedVote w = weighVote(p, v, e, m);
        say(!w.overturned && w.why == "same" && w.choice.camera == m.camera && w.choice.agreeing == m.agreeing &&
                close_to(w.choice.confidence, m.confidence),
            "keep: the weighted winner is the majority's string -- the choice is the majority's, field for field");
    }
    {
        // Two tips with no line (0.50 each) against one admitted line (1.00): a tie.
        std::vector<PointScore> p = {reading("S12"), reading("S12"), reading("D12")};
        std::vector<bool> v = {true, true, true};
        std::vector<VoteEvidence> e = {seen("not-a-shaft"), seen("not-a-shaft"), seen("ok")};
        const WeightedVote w = weighVote(p, v, e, chooseScore(p, v));
        say(published(p, w.choice) == "S12" && !w.overturned && w.why == "tie",
            "keep: 1.00 against 1.00 is a tie and the majority's S12 stands (got " + published(p, w.choice) +
                ", why=" + w.why + ")");
    }
    {
        // No two agree: camera 1's S5 stands as the lone fallback (rim only, 0.25); camera
        // 2's S1 weighs 0.40 -- heavier by 0.15, under the 0.50 floor.
        std::vector<PointScore> p = {reading("S5"), reading("S1")};
        std::vector<bool> v = {true, true};
        std::vector<VoteEvidence> e = {seen("rim-only", 0.0), seen("not-a-shaft", 0.5, 0.8)};
        const WeightedVote w = weighVote(p, v, e, chooseScore(p, v));
        say(published(p, w.choice) == "S5" && !w.overturned && w.why == "floor",
            "keep: a weighted winner under the 0.50 floor does not publish (got " + published(p, w.choice) +
                ", why=" + w.why + ")");
    }

    // ---- guard: #1505 / #1346 / the scoring area ----------------------------------------
    {
        // Camera 1's T5 is the lone fallback at 0.125 (bent past 10 px, R 0.5); camera 2's
        // S12 is rim only at 0.25. With the floor lowered to reach the guard, a winner no
        // scoring-area camera saw still does not publish.
        std::vector<PointScore> p = {reading("T5"), reading("S12")};
        std::vector<bool> v = {true, true};
        std::vector<VoteEvidence> e = {seen("not-straight", 10.0, 0.5), seen("rim-only", 0.0)};
        const WeightedVote w = weighVote(p, v, e, chooseScore(p, v), 0.10, 0.10);
        say(published(p, w.choice) == "T5" && !w.overturned && w.why == "rim",
            "guard: a winner only rim-only cameras read never replaces the majority (got " + published(p, w.choice) +
                ", why=" + w.why + ")");
    }
    {
        // A MISS does not vote (aVoteIsCast): two cameras' MISS cannot be weighed into a hit
        // and nothing is manufactured where no camera may vote.
        std::vector<PointScore> p = {reading("MISS"), reading("MISS"), reading("S5")};
        p[0].ring.clear();
        p[1].ring.clear();
        std::vector<bool> v = {aVoteIsCast(p[0]), aVoteIsCast(p[1]), true};
        std::vector<VoteEvidence> e = {seen("ok"), seen("ok"), seen("not-a-shaft")};
        const WeightedVote w = weighVote(p, v, e, chooseScore(p, v));
        std::vector<bool> none = {false, false, false};
        const WeightedVote nobody = weighVote(p, none, e, chooseScore(p, none));
        say(published(p, w.choice) == "S5" && !w.overturned && nobody.choice.camera == -1 && nobody.why == "none",
            "guard: a MISS is not weighed, and with no voter nothing is published (got " + published(p, w.choice) +
                ", nobody=" + std::to_string(nobody.choice.camera) + ")");
    }
    {
        // #1346: an asserted 20 is not weighed however well its camera saw.
        std::vector<PointScore> p = {reading("S20"), reading("S20"), reading("S1")};
        p[0].wedge_measured = p[1].wedge_measured = false;
        p[0].wedge_asserted = p[1].wedge_asserted = true;
        std::vector<bool> v = {true, true, true};
        std::vector<VoteEvidence> e = {seen("ok"), seen("ok"), seen("not-a-shaft")};
        const WeightedVote w = weighVote(p, v, e, chooseScore(p, v));
        say(published(p, w.choice) == "S1" && !w.overturned && w.weights == "S1=0.50[cam3 0.50]",
            "guard: two asserted S20 are not weighed against camera 3's measured S1 (got " + published(p, w.choice) +
                ", weights `" + w.weights + "`)");
    }

    std::cout << (failures == 0 ? "ALL HELD" : "FAILURES: " + std::to_string(failures)) << std::endl;
    return failures == 0 ? 0 : 1;
}
