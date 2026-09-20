// #1489: a reading of the ring alone is not a camera that measured a wedge.
//
// `OUTER` is the OUTER BULL -- the 25 ring, posted to Turnaus as "25" -- and `BULL` is
// the 50. Both are scored by the ring ellipses with the angular ruler never asked, so
// neither is a wedge measurement and neither is the asserted 20. Before this issue the
// vote had only those two words for it: it split the cameras on `wedge_asserted` alone,
// so a bull or an outer bull landed in the bucket called `measured` and published at 0.7,
// or at 0.9 with a second camera agreeing -- the two confidences whose whole meaning is
// that a camera MEASURED A WEDGE. On mocks/rig-20260918 under OD_RINGS=asfitted that is
// eight darts of eighteen, in a run where not one wedge was measured anywhere.
//
// What is asked here, in the two halves the defect has:
//
//   1. THE VOTE (`chooseScore`), pure: a ring-only reading still votes and still wins a
//      consensus -- the agreement is real -- and the choice now says what it was
//      agreement ABOUT. #1346's rules are re-asked unchanged beside it, because a
//      distinction that quietly moved the vote would be a different bug in the same line.
//
//   2. THE READING (`scorePoint`), on a synthetic board: the flags a bull and an outer
//      bull carry, from an ANCHORED camera and from an unanchored one. The anchored half
//      is the face of this defect no footage in this repository can show -- the rig has
//      no anchored camera -- and it is the one where `wedge_measured` was true outright.
//
// THE FALSIFIER, on this same binary: `OD_RING_ONLY=counted` makes a ring-only reading
// count as a measured wedge again, exactly as every build before #1489 did. This check
// reads the switch itself and asserts the OTHER answers under it, so neither half can be
// green by refusing everything.
//
//   i1489_wedge_check                      # this tree's decision
//   OD_RING_ONLY=counted i1489_wedge_check # the same binary, before #1489
#include <opencv2/opencv.hpp>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "geometry_calibration.hpp"
#include "score_processing.hpp"

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

// ---- the readings, as fields ------------------------------------------------------------

static PointScore measured(const std::string &score)
{
    PointScore p;
    p.score = score;
    p.wedge_measured = true;
    return p;
}

static PointScore asserted(const std::string &score)
{
    PointScore p;
    p.score = score;
    p.wedge_asserted = true;
    return p;
}

/** A BULL or an OUTER as `scorePoint` now states one: the ring answered, the wedge never. */
static PointScore ringOnly(const std::string &score)
{
    PointScore p;
    p.score = score;
    p.ring_only = true;
    return p;
}

/** The same reading as every build before #1489 left it: nothing set at all. */
static PointScore ringOnlyAsItWas(const std::string &score)
{
    PointScore p;
    p.score = score;
    return p;
}

// ---- a board to score against (the fixture i1317_guards.cpp uses, with its anchor a
//      parameter, because that is the axis this issue turns on) -----------------------------

static DartboardCalibration boardAnchored(bool anchored)
{
    DartboardCalibration calib;
    calib.camera_index = 0;
    calib.capture_width = 1280;
    calib.capture_height = 720;
    calib.bullCenter = cv::Point(640, 360);
    calib.frameCenter = cv::Point(640, 360);
    calib.sees_board = true;

    calib.ellipses.hasValidDoubles = true;
    calib.ellipses.validOuterPoints = 108;
    calib.ellipses.validInnerPoints = 96;
    calib.ellipses.outerDoubleEllipse = cv::RotatedRect(cv::Point2f(640, 360), cv::Size2f(400, 400), 0.0f);
    calib.ellipses.innerDoubleEllipse = cv::RotatedRect(cv::Point2f(640, 360), cv::Size2f(376, 376), 0.0f);
    calib.ellipses.outerTripleEllipse = cv::RotatedRect(cv::Point2f(640, 360), cv::Size2f(248, 248), 0.0f);
    calib.ellipses.innerTripleEllipse = cv::RotatedRect(cv::Point2f(640, 360), cv::Size2f(224, 224), 0.0f);
    calib.ellipses.outerBullEllipse = cv::RotatedRect(cv::Point2f(640, 360), cv::Size2f(64, 64), 0.0f);
    calib.ellipses.innerBullEllipse = cv::RotatedRect(cv::Point2f(640, 360), cv::Size2f(26, 26), 0.0f);
    calib.ellipses.hasValidTriples = true;
    calib.ellipses.hasValidBulls = true;
    calib.ellipses.hasDetectedEllipses = true;

    for (int i = 0; i < wire_processing::kWiresRequired; i++)
    {
        const double angle = (-CV_PI / 2.0) + (i * 2.0 * CV_PI / wire_processing::kWiresRequired);
        calib.wires.wireEndpoints.add(cv::Point2f(
            640.0f + 200.0f * (float)cos(angle),
            360.0f + 200.0f * (float)sin(angle)));
    }
    calib.wires.wiresDetected = wire_processing::kWiresRequired;
    calib.wires.camera_index = 0;
    calib.wires.isValid = true;

    calib.orientation.camera_index = 0;
    calib.orientation.wedge20WireIndex = 0;
    calib.orientation.southWireIndex = 10;
    calib.orientation.isStarCamera = anchored;
    calib.orientation.anchored = anchored;
    return calib;
}

/** Exactly one of the three, or none of them on a MISS -- the vocabulary's own rule. */
static bool atMostOneFlag(const PointScore &p)
{
    return (p.wedge_measured ? 1 : 0) + (p.wedge_asserted ? 1 : 0) + (p.ring_only ? 1 : 0) <= 1;
}

static std::string flagsOf(const PointScore &p)
{
    return p.score + " [measured=" + (p.wedge_measured ? "y" : "n") +
           " asserted=" + (p.wedge_asserted ? "y" : "n") +
           " ring_only=" + (p.ring_only ? "y" : "n") + "]";
}

int main()
{
    const std::vector<bool> all(3, true);
    const bool before = score_processing::ringOnlyReadingsCountAsMeasured();

    std::cout << "=== this binary is reading "
              << (before ? "OD_RING_ONLY=counted -- the state BEFORE #1489"
                         : "the tree's own decision (no OD_RING_ONLY)")
              << " ===" << std::endl;

    // ---- 1. the vote ---------------------------------------------------------------------
    std::cout << std::endl
              << "=== 1. the vote: what a ring reading is worth, and what it is agreement ABOUT ===" << std::endl;
    {
        // The issue's second criterion, said exactly: this is NOT about refusing OUTER.
        const ScoreChoice c = chooseScore({ringOnly("OUTER"), ringOnly("OUTER"), asserted("S20")}, all);
        say(c.camera == 0 && c.agreeing == 2 && c.confidence == 0.9f && !c.by_default,
            "two cameras agreeing on OUTER still reach consensus at 0.9 -- the agreement is real");
        say(c.ring_only,
            "and the choice says it was agreement about a RING, with no wedge in it");
    }
    {
        const ScoreChoice c = chooseScore({ringOnly("BULL"), ringOnly("BULL"), asserted("S20")}, all);
        say(c.camera == 0 && c.agreeing == 2 && c.confidence == 0.9f && c.ring_only,
            "a BULL behaves identically to an OUTER -- the same ellipse reading, the same answer");
    }
    {
        // The control that gives the two above their meaning: a consensus that really IS
        // about a wedge must still say so, or `ring_only` would be a constant.
        const ScoreChoice c = chooseScore({measured("T20"), measured("T20"), asserted("S20")}, all);
        say(c.camera == 0 && c.agreeing == 2 && c.confidence == 0.9f && !c.ring_only,
            "two MEASURED cameras agreeing are a consensus about a wedge, and are not marked ring-only");
    }
    {
        // A reading is not a void, so the fallback does not fill one here: #1346's rule,
        // reached by a ring reading rather than by a wedge measurement.
        const ScoreChoice c = chooseScore({asserted("S20"), ringOnly("OUTER"), asserted("S20")}, all);
        say(c.camera == 1 && c.confidence == 0.7f && !c.by_default && c.ring_only,
            "one ring reading outranks two assertions and stands alone at 0.7 -- the fallback never outvotes");
    }
    {
        const ScoreChoice c = chooseScore({ringOnly("OUTER"), measured("T20")}, {true, true});
        say(c.camera == 0 && c.confidence == 0.7f && c.agreeing == 1 && c.ring_only,
            "a ring reading and a wedge measurement that disagree fall to the lowest index (#797), unchanged");
    }
    {
        // #1346's own scene, re-asked: nothing in this issue may move it.
        const ScoreChoice c = chooseScore({asserted("S20"), measured("S12"), asserted("S20")}, all);
        say(c.camera == 1 && c.confidence == 0.7f && !c.by_default && !c.ring_only,
            "#796's scene is untouched: the camera that measured still wins, at 0.7, not ring-only");
        const ScoreChoice d = chooseScore({asserted("S20"), asserted("S20"), asserted("D20")}, all);
        say(d.camera == 0 && d.by_default && d.confidence == 0.5f && d.agreeing == 0 && !d.ring_only,
            "and two assertions agreeing still earn nothing but the 0.5 fallback");
    }
    {
        // What the defect looked like from the vote's side, and the half that must NOT
        // change: a reading the scorer left blank -- which is what a bull was before
        // #1489 -- is still counted a reading, so an old caller loses no vote.
        const ScoreChoice c = chooseScore({ringOnlyAsItWas("OUTER"), ringOnlyAsItWas("OUTER")}, {true, true});
        say(c.camera == 0 && c.agreeing == 2 && c.confidence == 0.9f,
            "a reading carrying neither flag still votes -- this issue moved no camera out of the vote");
        say(!c.ring_only,
            "but nothing SAYS it was a ring, which is the silence #1489 is about: the flag is the sentence");
    }

    // ---- 2. the reading ------------------------------------------------------------------
    std::cout << std::endl
              << "=== 2. scorePoint: a bull and an outer bull, anchored and not ===" << std::endl;
    const cv::Point2f in_inner_bull(645.0f, 360.0f); // 5 px from the centre: inside the 26 px bull
    const cv::Point2f in_outer_bull(660.0f, 360.0f); // 20 px: outside the bull, inside the 64 px ring
    const cv::Point2f on_a_wedge(760.0f, 360.0f);    // 120 px: an ordinary single

    for (int anchored = 1; anchored >= 0; anchored--)
    {
        const DartboardCalibration calib = boardAnchored(anchored == 1);
        const std::string who = anchored ? "an ANCHORED camera" : "an unanchored camera";

        const PointScore outer = score_processing::scorePoint(in_outer_bull, calib);
        const PointScore bull = score_processing::scorePoint(in_inner_bull, calib);
        const PointScore wedge = score_processing::scorePoint(on_a_wedge, calib);
        std::cout << "    " << who << ": " << flagsOf(outer) << " " << flagsOf(bull)
                  << " " << flagsOf(wedge) << std::endl;

        say(outer.score == "OUTER" && outer.ring == "outer" && bull.score == "BULL",
            std::string("the fixture really reaches the two bull rings from ") + who);

        if (before)
        {
            // The defect, put back on this same binary. This is the state the issue
            // measured, and on an anchored camera it is `wedge_measured` outright.
            say(outer.ring_only == false && bull.ring_only == false,
                std::string("OD_RING_ONLY=counted: neither ring reading is marked ring-only, from ") + who);
            say(outer.wedge_measured == (anchored == 1) && bull.wedge_measured == (anchored == 1),
                std::string("and its wedge reads as MEASURED exactly where the camera is anchored -- from ") + who +
                    ", which is what the vote counted");
        }
        else
        {
            say(outer.ring_only && bull.ring_only,
                std::string("a bull and an outer bull are stated ring-only from ") + who);
            say(!outer.wedge_measured && !bull.wedge_measured,
                std::string("neither is counted as having MEASURED a wedge, from ") + who +
                    " -- the wedge is no part of the reading");
            say(!outer.wedge_asserted && !bull.wedge_asserted,
                std::string("and neither is counted as having ASSERTED one, from ") + who);
        }

        // The controls. An ordinary dart on the same board, from the same camera, must
        // still be read the way it always was -- otherwise the repair above is just a
        // scorer that stopped reading wedges.
        say(!wedge.ring_only && wedge.wedge_measured == (anchored == 1) && wedge.wedge_asserted == (anchored != 1),
            std::string("control: an ordinary dart from ") + who + " is " +
                (anchored ? "a measured wedge" : "the asserted 20") + ", and is not ring-only");
        say(wedge.segment > 0 && wedge.score != "MISS",
            std::string("control: and it is still given a number (") + wedge.score + ") from " + who);
        say(atMostOneFlag(outer) && atMostOneFlag(bull) && atMostOneFlag(wedge),
            std::string("no reading from ") + who + " carries two of the three at once");

        // #1489 rewrote the wedge stage's gate from `wedge_measured` to `anchored`, which
        // is the one place this repair could have silently dropped something. The board
        // ANGLE of a bull is that something: it is stated where the camera is anchored
        // and absent where it is not, and it was so before this issue.
        say(bull.board.has_angle == (anchored == 1) && outer.board.has_angle == (anchored == 1),
            std::string("the board angle of a bull is still stated exactly where there is an anchor to state it -- ") + who);
    }
    {
        // The property `chooseScore` leans on when it reads `ring_only` off the published
        // camera alone: within one run the flag is a fact about the ring the score string
        // names, so a group of cameras agreeing on one string cannot be mixed. Asked
        // rather than assumed.
        const DartboardCalibration anchored = boardAnchored(true);
        const DartboardCalibration loose = boardAnchored(false);
        bool homogeneous = true;
        const cv::Point2f tips[3] = {in_inner_bull, in_outer_bull, on_a_wedge};
        for (const cv::Point2f &tip : tips)
        {
            for (const DartboardCalibration &camera : {anchored, loose})
            {
                const PointScore p = score_processing::scorePoint(tip, camera);
                const bool names_a_bull = (p.score == "BULL" || p.score == "OUTER");
                homogeneous = homogeneous && p.ring_only == (names_a_bull && !before);
            }
        }
        say(homogeneous,
            "ring_only is a function of the score string and of nothing else -- not of the anchor -- so two "
            "cameras agreeing on one string agree about it, and reading it off the published camera is enough");
    }

    std::cout << std::endl
              << (failures ? "FAILURES: " : "ALL OK: ") << failures << std::endl;
    return failures ? 1 : 0;
}
