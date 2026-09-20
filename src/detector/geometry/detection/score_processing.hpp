#pragma once

#include <opencv2/opencv.hpp>
#include <map>
#include <string>
#include <vector>
#include "dart_processing.hpp"
#include "../calibration/geometry_calibration.hpp"

using namespace cv;
using namespace std;

namespace score_processing
{
    // #1186: where a dart is on the board, in the board's own frame rather than in one
    // camera's pixels. radius is 0 at the bull centre and 1 at the outer edge of the
    // double ring; angle is degrees clockwise from the vertical through the middle of
    // the 20, so the 20 spans [-9, 9), the 1 spans [9, 27) and so on round the board.
    // Both come from the rulers the calibration already holds for the camera the vote
    // chose: the six ring ellipses as a radial ruler with known marks in millimetres, and
    // the twenty wires as an angular ruler with marks 18 degrees apart. Neither is a
    // metric pose; a wire-to-wire fraction in image angle is a rectification, not a
    // reconstruction.
    struct BoardPosition
    {
        bool has_radius = false; // the tip is on the board and the radial ruler answered
        bool has_angle = false;  // the wedge the dart is in is known
        float radius = -1.0f;    // 0 .. 1 on a scoring dart; >1 is off the board
        float angle = -1.0f;     // [0, 360)
    };

    // #1186: one camera's reading of one tip, as a decision rather than as a string. The
    // score string is composed from ring and segment here, and nothing downstream parses
    // it back. wedge_measured is false where the orientation stage had no answer for this
    // camera and the wedge is the one the scorer asserts by default (upstream's "no
    // orientation data, defaulting to 20"); the board angle then says where in *that*
    // wedge the tip is, which is the position the score implies rather than one measured
    // against the number ring.
    struct PointScore
    {
        string score = "MISS";       // S20, D5, T17, BULL, OUTER, MISS - unchanged vocabulary
        string ring;                 // single, double, triple, bull, outer; empty for a miss
        int segment = -1;            // 1..20; -1 where the ring has no segment or the dart missed
        bool wedge_measured = false; // orientation known for this camera
        // #1346: true exactly where the 20 was ASSERTED rather than measured -- the
        // upstream "no orientation data, defaulting to 20", set at the site of the
        // assertion. A bull is never asserted: its score comes from the ring ellipses and
        // the wedge never enters it, so a bull from an unoriented camera is a measurement.
        bool wedge_asserted = false;
        BoardPosition board;
    };

    // Score result for a single dart
    struct ScoreResult
    {
        string score = "MISS";                        // Dart score (S20, D5, T17, BULL, etc.)
        Point2f dartboard_position = Point2f(-1, -1); // Position on dartboard coordinate system
        Point2f pixel_position = Point2f(-1, -1);     // Original pixel position
        Point2f center_position = Point2f(-1, -1);    // Dart center position
        float confidence = 0.0f;                      // Scoring confidence
        int camera_index = -1;                        // Which camera detected this
        bool valid = false;                           // Is this a valid score result
        // #1186: the chosen camera's reading, in the board's frame. Absent on END and on
        // the MISS the vote publishes when no camera scored.
        string ring;
        int segment = -1;
        BoardPosition board;
    };

    /**
     * #1346: what the vote chose, and what the choice is worth.
     *
     * `camera` indexes the reading the board publishes, -1 when no camera may vote.
     * `agreeing` is how many MEASURED cameras agreed on it; `by_default` is true when the
     * published wedge was asserted rather than measured, which is also the only way
     * `confidence` can be 0.5 on a dart. The three confidences now mean something
     * (#797's complaint): 0.9 is two or more measured cameras agreeing, 0.7 is one
     * camera's measurement standing alone, 0.5 is a wedge nobody measured.
     */
    struct ScoreChoice
    {
        int camera = -1;
        float confidence = 0.5f;
        int agreeing = 0;
        bool by_default = false;
    };

    /**
     * #1346: the vote, pure, and the decision #796 measured finally in the code that
     * runs. A camera whose wedge was ASSERTED -- `wedge_asserted`, the default-to-20 --
     * contributes nothing to the consensus: two constants agreeing outvoted the one
     * camera that measured, which published S20 S20 S20 over a hand-verified 36, and two
     * cameras agreeing on a constant earned the 0.9 that is supposed to mean two
     * measurements. Asserted readings are kept aside and published ONLY when no camera
     * measured a wedge at all, at 0.5 -- the fallback fills a void, it never outvotes.
     *
     * Among measured readings the rule is upstream's, unchanged: two or more agreeing on
     * one score string win at 0.9; otherwise the lowest-index measured camera stands
     * alone at 0.7, which is #797's open question and deliberately not this decision.
     *
     * `may_vote[i]` is what processScore has always required of a voter: the camera
     * participated in the window, is calibrated, found a tip, and did not read MISS.
     * Inline for #1338's reason: a tester holds the vote without building the detector.
     */
    inline ScoreChoice chooseScore(const vector<PointScore> &points, const vector<bool> &may_vote)
    {
        ScoreChoice out;
        vector<int> measured;
        vector<int> defaulted;
        for (size_t i = 0; i < points.size(); i++)
        {
            if (i >= may_vote.size() || !may_vote[i])
            {
                continue;
            }
            (points[i].wedge_asserted ? defaulted : measured).push_back((int)i);
        }

        if (!measured.empty())
        {
            // Upstream's consensus, restricted to cameras that measured: count each
            // score string's cameras, and the first largest group of two or more wins.
            map<string, vector<int>> score_cameras;
            for (int index : measured)
            {
                score_cameras[points[index].score].push_back(index);
            }
            string consensus_score;
            int max_consensus = 0;
            for (const auto &[score, cameras] : score_cameras)
            {
                if (cameras.size() >= 2 && (int)cameras.size() > max_consensus)
                {
                    consensus_score = score;
                    max_consensus = (int)cameras.size();
                }
            }
            if (!consensus_score.empty())
            {
                out.camera = score_cameras[consensus_score][0];
                out.agreeing = max_consensus;
                out.confidence = 0.9f;
            }
            else
            {
                out.camera = measured[0];
                out.agreeing = 1;
                out.confidence = 0.7f;
            }
            return out;
        }

        if (!defaulted.empty())
        {
            out.camera = defaulted[0];
            out.confidence = 0.5f;
            out.by_default = true;
        }
        return out;
    }

    // ---- #1451: whether a point can be SCORED from this camera, and how each one reads ----
    //
    // #1449 one field over, and worse in one specific way. That issue's unreadable camera
    // still scored -- wrongly, as #1346's asserted 20. A camera refused HERE contributes
    // NOTHING: `scorePoint` returns the default PointScore, whose `score` is "MISS", so
    // `may_vote` is false and the camera abstains from `chooseScore` entirely. A board on
    // which no camera can be scored from publishes every dart as a MISS.
    //
    // And it did it in silence. The refusal in `scorePoint` is `log_debug`, below the
    // default level; the one abstain line that IS at warning level covers `!sees_board`
    // and is never reached by this camera, because this camera does see the board. So the
    // startup census counted it a full voter, the board reported itself whole, and the
    // first thing that said otherwise was a dart published as a MISS.
    //
    // WHERE THE ASYMMETRY REALLY COMES FROM, because it is not where the issue guessed.
    // `calibrateSingleCamera` refuses a camera whose ring is not whole and clears
    // `sees_board`, so a FRESHLY calibrated camera cannot reach this state. The cache can:
    // #1442 moved the decision from `wireEndpoints.size()` to `wiresDetected` without
    // moving `sizeof(DartboardCalibration)`, so a calibration fwritten by an older binary
    // loads cleanly carrying `sees_board` true, `isValid` true and twenty-two detected
    // wires -- and the guards downstream ask `wholeRing()`. That is the door #1442's own
    // comment says it was closing, and closing it is what created this silence: the
    // scorer refuses the camera and the census never hears about it.

    /**
     * #1451: whether the SCORER will read a point from this camera.
     *
     * This is `scorePoint`'s own guard and there is now one of it. The census that reports
     * what a board can score with and the scorer that acts on it must not be able to drift
     * apart -- a camera counted at start and refused at every dart is exactly the silence
     * this issue is about. `sees_board` is NOT the question: a cached calibration can
     * carry it over a ring this expression refuses.
     *
     * NOT a behaviour change: this is the expression `scorePoint` had written inline,
     * character for character.
     */
    inline bool canScoreAPoint(const DartboardCalibration &calib)
    {
        return calib.ellipses.hasValidDoubles && calib.wires.wholeRing();
    }

    /**
     * #1451: whether a dart really IS scored from this camera, which is both of
     * `processScore`'s conditions and is what a census must ask.
     *
     * `canScoreAPoint` above is the guard INSIDE `scorePoint`, and it is right not to ask
     * `sees_board`: its caller asks that first and abstains the camera by name before ever
     * calling it. A census asking the inner guard alone repeats this very issue one field
     * further on -- and not hypothetically. #1372 clears `sees_board` on a cached camera
     * that produced no frame THIS start, while its cached ring and doubles stay exactly as
     * they were; that camera passes `canScoreAPoint` and is abstained by `processScore`
     * anyway. An earlier draft of this census counted it scorable, and
     * testers/i1451_scoring_check.cpp is what caught it.
     */
    inline bool aDartIsScoredFrom(const DartboardCalibration &calib)
    {
        return calib.sees_board && canScoreAPoint(calib);
    }

    /** How many of these cameras the scorer will read a point from. */
    inline int camerasThatCanScoreAPoint(const vector<DartboardCalibration> &calibrations)
    {
        int scorable = 0;
        for (const DartboardCalibration &calibration : calibrations)
        {
            if (aDartIsScoredFrom(calibration))
            {
                scorable++;
            }
        }
        return scorable;
    }

    /**
     * #1389 / ADR-0081 §3: this camera's own reason, never just a count. "A message saying
     * only 'two of three' has told nobody anything."
     *
     * Each branch sends the reader somewhere different: a camera that is not looking at
     * the board is the USB bus and the aim (#1318, #1319); an unfitted doubles ring is the
     * lighting; and a ring that is not whole on a camera that nonetheless calibrated is a
     * cache written by an older binary, where the remedy is to delete cache/ rather than
     * to touch the rig.
     */
    inline string howItScores(const DartboardCalibration &calib)
    {
        // #1321's rule, and the positive branch obeys it too: the count is stated against
        // the threshold even when it passed. An earlier draft of this line said "all 20"
        // as a constant, and the tester caught it measuring a board running under
        // OD_WIRE_COUNT=atleast, where a camera really holding twenty-one was reported as
        // holding all twenty. A census whose healthy sentence cannot report the number
        // that decided it hides exactly the reading this issue is about.
        if (aDartIsScoredFrom(calib))
        {
            return "has a fitted doubles ring and " + to_string(calib.wires.wiresDetected) +
                   " of the " + to_string(wire_processing::kWiresRequired) +
                   " wire boundaries a board has, so a dart is scored from it";
        }
        // Asked in `processScore`'s own order, and the order is load-bearing. A cached
        // camera that produced no frame this start keeps its cached ring and doubles and
        // loses only `sees_board` (#1372), so asking the ring first would report a whole
        // ring on a camera the scorer abstains before it looks at one.
        if (!calib.sees_board)
        {
            return "is not looking at the dartboard this start, so it abstains and no dart "
                   "is scored from it";
        }
        if (!calib.ellipses.hasValidDoubles)
        {
            // The same camera camera_quorum already abstains from both dart quorums for
            // (#1339, #1354), said here in the scorer's own words: no fitted ring is no
            // radial ruler, so there is no ring to put the dart in either.
            return "has no fitted doubles ring, so it has no radial ruler and no dart is "
                   "scored from it";
        }
        // A ring that is not whole. #1442's two shapes, and the count tells them apart,
        // because they send the reader to different places: short is a wire stage that
        // found too little, long is one that found too much and had the surplus dropped.
        const string count = to_string(calib.wires.wiresDetected) + " of the " +
                             to_string(wire_processing::kWiresRequired) + " wire boundaries a board has";
        // Seeing the board with a ring that is not whole is the state `calibrateSingleCamera`
        // refuses, so this camera did not calibrate on this start: it came off the cache,
        // written by a binary whose wire guard asked the other field (#1442). The remedy is
        // the cache and not the rig, and saying so is the whole point of naming it here.
        return "calibrated with " + count +
               ", which the wire guard refuses, so no dart is scored from it -- it came off "
               "the calibration cache, written by a binary that measured a whole ring "
               "differently; delete cache/ to measure this camera again";
    }

    /**
     * Every camera in its own slot, with its own reason, scorable or not. Deliberately not
     * `camera_quorum::namingEachCamera`: that one answers "can it vote on what is ON the
     * board", which is a different question about the same camera, and one sentence
     * answering both would be wrong about one of them.
     */
    inline string namingEachCamera(const vector<DartboardCalibration> &calibrations)
    {
        string out;
        for (size_t i = 0; i < calibrations.size(); i++)
        {
            out += out.empty() ? "" : "; ";
            out += "camera " + to_string(i + 1) + ": " + howItScores(calibrations[i]);
        }
        return out;
    }


    // #1186: score one tip against one camera's calibration. The string the vote counts
    // is PointScore::score; the rest is the same decision stated as fields.
    PointScore scorePoint(Point2f pixel, const DartboardCalibration &calib);

    // Process dart scoring from tip detection results
    ScoreResult processScore(
        const vector<Mat> &background_frames,
        const dart_processing::DartStateResult &dart_result,
        const vector<DartboardCalibration> &calib,
        bool debug_mode = false);

} // namespace score_processing
