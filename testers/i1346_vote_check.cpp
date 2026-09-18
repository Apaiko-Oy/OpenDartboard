// #1346: the vote, on its own, and #796's scene replayed against it.
//
// `score_processing::chooseScore` is the consensus that used to live inline in
// processScore, where the defect #796 measured could not be tested without footage: two
// cameras whose wedge was ASSERTED (the default-to-20) agreeing with each other outvoted
// the one camera that measured, and published S20 S20 S20 over a hand-verified 36 at the
// 0.9 that is supposed to mean two measurements. The vote is pure and inline in the
// header now (#1338's shape), so the scene is replayed here as PointScores: the same
// three readings, and the assertion that the measurement wins.
//
// What is also held: a bull from an unoriented camera counts as a measurement (the wedge
// never entered it), asserted readings still fill the void when nothing measured -- at
// 0.5, the fallback that never outvotes -- the lowest-index rule among disagreeing
// measured cameras is unchanged (#797's open question, deliberately not this decision),
// and a camera that may not vote is not in the vote at all.
//
//   g++ -std=c++17 -I src -I src/utils -o vote_check testers/i1346_vote_check.cpp \
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

static PointScore measured(const std::string &score)
{
    PointScore p;
    p.score = score;
    p.wedge_measured = true;
    p.wedge_asserted = false;
    return p;
}

static PointScore asserted(const std::string &score)
{
    PointScore p;
    p.score = score;
    p.wedge_measured = false;
    p.wedge_asserted = true;
    return p;
}

static PointScore bull(const std::string &score)
{
    // A bull from an UNORIENTED camera: no wedge was measured and none was asserted --
    // the score came from the ring ellipses alone.
    PointScore p;
    p.score = score;
    p.wedge_measured = false;
    p.wedge_asserted = false;
    return p;
}

int main()
{
    const std::vector<bool> all(3, true);

    // ---- #796's scene: S20 S20 asserted, S12 measured ----------------------------------
    // The shipped vote published S20 from 2 cameras at 0.9. The hand-verified reading was
    // camera 1's. One measurement beats two constants.
    {
        const ScoreChoice c = chooseScore({asserted("S20"), measured("S12"), asserted("S20")}, all);
        say(c.camera == 1, "#796's scene: the camera that measured wins the vote");
        say(!c.by_default && c.confidence == 0.7f,
            "and stands alone at 0.7 -- not outvoted, and not credited with a consensus it does not have");
    }

    // ---- two measurements agreeing still earn the 0.9 ----------------------------------
    {
        const ScoreChoice c = chooseScore({measured("S5"), measured("S5"), asserted("S20")}, all);
        say(c.camera == 0 && c.agreeing == 2 && c.confidence == 0.9f,
            "two measured cameras agreeing are a consensus at 0.9, the assertion beside them counted for nothing");
    }

    // ---- two assertions agreeing earn nothing ------------------------------------------
    // This is the 0.9 the shipped vote handed out for two cameras agreeing on a constant.
    {
        const ScoreChoice c = chooseScore({asserted("S20"), asserted("S20"), asserted("D20")}, all);
        say(c.camera == 0 && c.by_default && c.confidence == 0.5f && c.agreeing == 0,
            "with nothing measured the assertion is published as the fallback it is: 0.5, by_default, no consensus claimed");
    }

    // ---- a bull from an unoriented camera is a measurement -----------------------------
    // The wedge never entered a bull, so two unoriented cameras agreeing on BULL really
    // did measure the same thing twice.
    {
        const ScoreChoice c = chooseScore({bull("BULL"), bull("BULL"), asserted("S20")}, all);
        say(c.camera == 0 && c.agreeing == 2 && c.confidence == 0.9f && !c.by_default,
            "two bulls from unoriented cameras are a real consensus -- an ellipse reading is not an asserted wedge");
    }

    // ---- disagreeing measurements: #797's rule, unchanged and on purpose ---------------
    {
        const ScoreChoice c = chooseScore({measured("S5"), measured("S6"), asserted("S20")}, all);
        say(c.camera == 0 && c.confidence == 0.7f && c.agreeing == 1,
            "measured cameras that disagree still fall to the lowest index at 0.7 -- #797's open question, not this issue's");
    }

    // ---- a camera that may not vote is not in the vote ---------------------------------
    // may_vote is what processScore always required: a found tip and not a MISS. Here the
    // only measured camera has no vote, so the assertion is what is left.
    {
        const ScoreChoice c = chooseScore({asserted("S20"), measured("S12"), asserted("S20")},
                                          {true, false, true});
        say(c.camera == 0 && c.by_default,
            "a measured camera without a vote (no tip, or a MISS) does not vote, and the fallback fills the void");
    }

    // ---- nobody may vote ---------------------------------------------------------------
    {
        const ScoreChoice c = chooseScore({asserted("S20"), measured("S12")}, {false, false});
        say(c.camera == -1, "no eligible camera is camera -1, which processScore publishes as the vote's MISS");
    }

    std::cout << (failures ? "FAILURES: " : "ALL OK: ") << failures << std::endl;
    return failures ? 1 : 0;
}
