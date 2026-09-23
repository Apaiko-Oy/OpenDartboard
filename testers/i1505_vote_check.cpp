// #1505: a MISS on the surround is measured, and the repair that would let it vote is
// refused -- with the pin that re-measures the refusal on the shipping binary.
//
//   i1505_vote_check fresh     the tree's rule: a MISS never votes, surround or not
//   i1505_vote_check votes     OD_SURROUND=votes, the refused repair, pinned
//
// The mode is set before anything reads the environment, because the switches cache
// their first read (the od_fix shape). Each claim prints OK or FAIL with the issue's
// own words; the process exits with the number of failures.
//
// WHAT IS HELD, and why each half exists:
//
//   scorePoint's geometry -- a synthetic circular board, so the numbers are exact:
//     inside the doubles                        -> a score, and a vote
//     past the doubles, within 225.5/170 of it  -> MISS, on_surround, radius > 1
//     past the physical rim                     -> MISS, NOT on_surround
//   the default rule -- neither MISS votes. Visit 6's off-board dart therefore still
//     publishes a phantom S7 (the one on-board error of the census), because the
//     repair was MEASURED AND REFUSED: on the real binary the same run that turned
//     that phantom into MISS flipped visit 3's CORRECT S7 to MISS off a flight
//     artifact at ruler radius 1.177 -- three percent from the honest witness's
//     1.147, with the honest one NEARER the board. aVoteIsCast in
//     score_processing.hpp carries the record.
//   the BOARD-line spelling -- a published MISS (reachable under the pin) must not
//     read "wedge by default", which is i1484's census needle for #1346's asserted 20.
//   the pin -- under OD_SURROUND=votes the surround MISS votes and chooseScore needs
//     no new branch: the lone-reading fallback publishes readings[0] (MISS at 0.7),
//     two surround MISSes agree at 0.9, and the beyond-rim artifact still abstains.
//     These hold the pin honest, so the refusal can be re-measured rather than
//     re-argued.
//
// MUTATION PROOF: see testers/i1505_inside.sh, which records the plants, the
// predictions made before each run, and what each run measured.
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "geometry_calibration.hpp"
#include "detector/geometry/detection/score_processing.hpp"

namespace
{
    int failures = 0;
    void say(bool ok, const std::string &what)
    {
        std::cout << (ok ? "OK   " : "FAIL ") << what << std::endl;
        if (!ok)
        {
            failures++;
        }
    }

    /** A synthetic camera looking straight at a board: circles, so radii are exact. */
    DartboardCalibration syntheticBoard(float boardPx)
    {
        DartboardCalibration calib;
        calib.camera_index = 0;
        calib.bullCenter = cv::Point(640, 360);
        calib.sees_board = true;
        const cv::Point2f centre(640.0f, 360.0f);
        const perspective_processing::DartboardSpec spec;
        auto ring = [&](double mm)
        {
            const float r = boardPx * (float)(mm / spec.outerDoubleRadius);
            return cv::RotatedRect(centre, cv::Size2f(2.0f * r, 2.0f * r), 0.0f);
        };
        calib.ellipses.innerBullEllipse = ring(spec.bullRadius);
        calib.ellipses.outerBullEllipse = ring(spec.bull25Radius);
        calib.ellipses.innerTripleEllipse = ring(spec.innerTripleRadius);
        calib.ellipses.outerTripleEllipse = ring(spec.outerTripleRadius);
        calib.ellipses.innerDoubleEllipse = ring(spec.innerDoubleRadius);
        calib.ellipses.outerDoubleEllipse = ring(spec.outerDoubleRadius);
        calib.ellipses.hasValidDoubles = true;
        for (int i = 0; i < wire_processing::kWiresRequired; i++)
        {
            const float a = (float)(2.0 * CV_PI * i / wire_processing::kWiresRequired);
            calib.wires.wireEndpoints.add(centre + cv::Point2f(boardPx * std::cos(a), boardPx * std::sin(a)));
        }
        calib.wires.wiresDetected = wire_processing::kWiresRequired;
        return calib;
    }

    score_processing::PointScore reading(const std::string &score, bool on_surround)
    {
        score_processing::PointScore p;
        p.score = score;
        p.ring = score == "MISS" ? "" : "single";
        p.on_surround = on_surround;
        return p;
    }
}

int main(int argc, char **argv)
{
    const std::string mode = argc > 1 ? argv[1] : "fresh";
    if (mode == "votes")
    {
        setenv("OD_SURROUND", "votes", 1);
    }
    else
    {
        unsetenv("OD_SURROUND");
    }

    const float boardPx = 300.0f;
    const DartboardCalibration calib = syntheticBoard(boardPx);
    const cv::Point2f centre(640.0f, 360.0f);

    // ---- scorePoint's geometry, mode-independent ----------------------------------------
    const score_processing::PointScore inside =
        score_processing::scorePoint(centre + cv::Point2f(0.90f * boardPx, 0.0f), calib);
    say(inside.score != "MISS" && !inside.on_surround && score_processing::aVoteIsCast(inside),
        "a tip inside the doubles scores and votes (got " + inside.score + ")");

    const score_processing::PointScore surround =
        score_processing::scorePoint(centre + cv::Point2f(1.08f * boardPx, 0.0f), calib);
    say(surround.score == "MISS" && surround.on_surround,
        "a tip 8% past the doubles is a MISS measured ON THE SURROUND");
    say(surround.board.has_radius && surround.board.radius > 1.0f && surround.board.radius < 1.2f,
        "... and the ruler still answers for it, past 1 (got " +
            std::to_string(surround.board.radius) + "), so a published MISS's BOARD line"
            " can say how far off the board the tip was");

    const score_processing::PointScore beyond =
        score_processing::scorePoint(centre + cv::Point2f(1.45f * boardPx, 0.0f), calib);
    say(beyond.score == "MISS" && !beyond.on_surround,
        "a \"tip\" past the physical rim (225.5/170) is not a place a dart can be:"
        " MISS, not on the surround");
    say(!score_processing::aVoteIsCast(beyond),
        "... and it abstains in every mode");

    // ---- the BOARD-line spelling --------------------------------------------------------
    const std::string said = score_processing::howTheWedgeWasRead(surround);
    say(said != "wedge by default" && said == "no wedge, the tip is off the board",
        "a published MISS does not read \"wedge by default\" (i1484's needle for #1346's"
        " asserted 20); it says: " + said);

    // ---- the rule under this mode -------------------------------------------------------
    if (mode == "votes")
    {
        say(score_processing::aVoteIsCast(surround),
            "OD_SURROUND=votes: the surround MISS votes -- the refused repair, pinned so"
            " the refusal can be re-measured");

        using score_processing::chooseScore;
        std::vector<score_processing::PointScore> pts;
        std::vector<bool> votes;

        pts = {reading("MISS", true), reading("S7", false)};
        votes = {score_processing::aVoteIsCast(pts[0]), score_processing::aVoteIsCast(pts[1])};
        score_processing::ScoreChoice c = chooseScore(pts, votes);
        say(c.camera == 0 && pts[c.camera].score == "MISS" && c.confidence == 0.7f && c.agreeing == 1,
            "{surround MISS, S7}: chooseScore needs no new branch -- the lone-reading"
            " fallback publishes readings[0], MISS at 0.7. This is what repairs visit 6's"
            " phantom AND what flipped visit 3's correct S7, which is the refusal");

        pts = {reading("S7", false), reading("MISS", true)};
        votes = {true, true};
        c = chooseScore(pts, votes);
        say(c.camera == 0 && pts[c.camera].score == "S7" && c.confidence == 0.7f,
            "{S7, surround MISS}: the same fallback the other way round -- S7 at 0.7,"
            " by INDEX (#797's open question, deliberately not answered here)");

        pts = {reading("MISS", true), reading("MISS", true)};
        votes = {true, true};
        c = chooseScore(pts, votes);
        say(c.camera == 0 && pts[c.camera].score == "MISS" && c.confidence == 0.9f && c.agreeing == 2,
            "{surround MISS, surround MISS}: a real consensus that the dart is off the"
            " board -- MISS at 0.9");

        pts = {reading("MISS", false), reading("S7", false)};
        votes = {score_processing::aVoteIsCast(pts[0]), score_processing::aVoteIsCast(pts[1])};
        c = chooseScore(pts, votes);
        say(c.camera == 1 && pts[c.camera].score == "S7" && c.confidence == 0.7f,
            "{beyond-rim MISS, S7}: the artifact abstains even under the pin and S7"
            " stands alone at 0.7");
    }
    else
    {
        say(!score_processing::aVoteIsCast(surround),
            "the tree's rule: the surround MISS is measured (on_surround true) and votes"
            " nothing -- the repair is refused, and aVoteIsCast's comment carries the"
            " 1:1 trade that refused it");

        // The consequence the refusal keeps, stated so the census's one on-board error
        // cannot read as an oversight: with the honest witness silenced, the parallax
        // reading stands alone and the vote publishes it.
        std::vector<score_processing::PointScore> pts = {reading("MISS", true), reading("S7", false)};
        std::vector<bool> votes = {score_processing::aVoteIsCast(pts[0]), score_processing::aVoteIsCast(pts[1])};
        score_processing::ScoreChoice c = score_processing::chooseScore(pts, votes);
        say(c.camera == 1 && pts[c.camera].score == "S7" && c.confidence == 0.7f,
            "{surround MISS, S7} under the tree's rule: the phantom S7 publishes at 0.7"
            " -- visit 6's on-board error, kept knowingly, queued behind the tip"
            " machinery (#1494/#1495's family)");
    }

    std::cout << "CHECKS_FAILED=" << failures << " mode=" << mode << std::endl;
    return failures;
}
