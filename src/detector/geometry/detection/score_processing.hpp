#pragma once

#include <opencv2/opencv.hpp>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>
#include "dart_processing.hpp"
#include "../calibration/geometry_calibration.hpp"
#include "../calibration/board_model.hpp"

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
        // #1489: the WEDGE of this reading was measured -- this camera is anchored AND
        // the wedge is part of what was read. It is not `anchored` on its own: a bull
        // read by an anchored camera measured a ring and no wedge at all.
        bool wedge_measured = false;
        // #1346: true exactly where the 20 was ASSERTED rather than measured -- the
        // upstream "no orientation data, defaulting to 20", set at the site of the
        // assertion. A bull is never asserted: its score comes from the ring ellipses and
        // the wedge never enters it.
        bool wedge_asserted = false;
        // #1489: the wedge is NO PART of this reading -- a BULL or an OUTER, scored by
        // the ring ellipses alone with the angular ruler never asked. Such a reading is
        // neither measured nor asserted, and the three are exclusive: at most one is true
        // of any one reading.
        //
        // #1346 said the first half of this and left the second implicit -- a bull "is a
        // measurement", meaning it is not the asserted 20 and the vote must not discard
        // it. The vote read that as `!wedge_asserted` and so counted a ring-only reading
        // among the cameras that MEASURED A WEDGE, which is what 0.7 and 0.9 say. On
        // mocks/rig-20260918 under OD_RINGS=asfitted that is eight darts of nineteen
        // published at 0.7 or 0.9 by cameras that read no wedge at all, so a geometry
        // change pushing MORE darts into the 25 ring reads as the anchor improving. The
        // agreement is real and still wins a consensus; it is just not agreement about a
        // wedge, and `ScoreChoice::ring_only` is where the published reading says so.
        bool ring_only = false;
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
     * `agreeing` is how many cameras that READ something agreed on it; `by_default` is
     * true when the published wedge was asserted rather than measured, which is also the
     * only way `confidence` can be 0.5 on a dart. The three confidences now mean
     * something (#797's complaint): 0.9 is two or more readings agreeing, 0.7 is one
     * reading standing alone, 0.5 is a wedge nobody measured.
     *
     * #1489: and `ring_only` is the SECOND axis those three needed, because they count
     * cameras and say nothing about what the cameras read. A BULL or an OUTER is scored
     * by the ring ellipses with no wedge in it at all, so two cameras agreeing on one is
     * a real consensus -- 0.9, unchanged -- about something that is not a wedge. Read
     * as a number alone, 0.9 and 0.7 then move with the geometry rather than with the
     * anchor: eight of the rig's nineteen darts published at 0.7 or 0.9 with not one
     * wedge measured anywhere in the run.
     *
     * So this is not a fourth confidence. A fourth number would have to mean "agreed,
     * but about a ring", which is the same count of cameras as 0.9 with a different
     * subject -- it would leave `agreeing` ambiguous, change what a published float
     * means to every client of the WebSocket API, and still not tell a reader which of
     * the two a 0.9 was. The count and the subject are two questions, so they are two
     * fields: the census reports 0.9 and 0.7 each split by `ring_only`, and the sum of
     * the split is the number that was there before.
     */
    struct ScoreChoice
    {
        int camera = -1;
        float confidence = 0.5f;
        int agreeing = 0;
        bool by_default = false;
        bool ring_only = false;
    };

    /**
     * #1489: a ring-only reading counts as a camera that measured a wedge, the way every
     * build before this issue did -- the falsifier, on the same binary. A distinction
     * that can only ever be drawn cannot be shown to be doing anything.
     */
    inline bool ringOnlyReadingsCountAsMeasured()
    {
        static const bool v = []
        {
            const char *e = std::getenv("OD_RING_ONLY");
            return e != nullptr && std::string(e) == "counted";
        }();
        return v;
    }

    /**
     * #1489: how a published reading came by its wedge, in the words the BOARD line has
     * always used. The first two spellings are byte-for-byte what they were; the third
     * is the state that had no name and was printed as the second.
     */
    inline string howTheWedgeWasRead(const PointScore &point)
    {
        if (point.ring_only)
        {
            return "wedge not in this reading";
        }
        return point.wedge_measured ? "wedge measured" : "wedge by default";
    }

    /**
     * #1346: the vote, pure, and the decision #796 measured finally in the code that
     * runs. A camera whose wedge was ASSERTED -- `wedge_asserted`, the default-to-20 --
     * contributes nothing to the consensus: two constants agreeing outvoted the one
     * camera that measured, which published S20 S20 S20 over a hand-verified 36, and two
     * cameras agreeing on a constant earned the 0.9 that is supposed to mean two
     * measurements. Asserted readings are kept aside and published ONLY when no camera
     * measured a wedge at all, at 0.5 -- the fallback fills a void, it never outvotes.
     *
     * Among readings the rule is upstream's, unchanged: two or more agreeing on one score
     * string win at 0.9; otherwise the lowest-index reading stands alone at 0.7, which is
     * #797's open question and deliberately not this decision.
     *
     * #1489: a READING is what this bucket always really held, and calling it `measured`
     * is what went wrong. A bull and an outer bull are scored by the ring ellipses with
     * the wedge never asked, so they are neither asserted nor a wedge measurement -- and
     * they belong in this bucket, because the fallback fills a void and a ring reading is
     * not a void. They vote exactly as they did. What changes is that the choice now says
     * which kind of reading won, instead of leaving a reader to infer a measured wedge
     * from a number that only ever counted cameras.
     *
     * `may_vote[i]` is what processScore has always required of a voter: the camera
     * participated in the window, is calibrated, found a tip, and did not read MISS.
     * Inline for #1338's reason: a tester holds the vote without building the detector.
     */
    inline ScoreChoice chooseScore(const vector<PointScore> &points, const vector<bool> &may_vote)
    {
        ScoreChoice out;
        vector<int> readings;
        vector<int> defaulted;
        for (size_t i = 0; i < points.size(); i++)
        {
            if (i >= may_vote.size() || !may_vote[i])
            {
                continue;
            }
            (points[i].wedge_asserted ? defaulted : readings).push_back((int)i);
        }

        if (!readings.empty())
        {
            // Upstream's consensus, restricted to cameras that read something: count each
            // score string's cameras, and the first largest group of two or more wins.
            map<string, vector<int>> score_cameras;
            for (int index : readings)
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
                out.camera = readings[0];
                out.agreeing = 1;
                out.confidence = 0.7f;
            }
            // #1489: what the winning cameras agreed ABOUT, read off the reading that is
            // published rather than off the score string, so nothing downstream parses a
            // score back into a decision (#1186's rule). A group is homogeneous by
            // construction -- `ring_only` is a fact about the ring the string names -- and
            // the tester asks that rather than assuming it.
            out.ring_only = points[out.camera].ring_only;
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
        // #1467: and the fit, where there was one. `readable()` is `wholeRing()` on a
        // calibration made before that issue or on the counting path, so this door is no
        // wider than it was and is narrower where a plane was fitted and not trusted.
        return calib.ellipses.hasValidDoubles && calib.wires.readable();
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
     * `camera_quorum::namingEachCamera` nor `orientation_processing::namingEachCamera`:
     * those answer "can it vote on what is on the board" and "can its wedge be read",
     * which are different questions about the same camera, and one sentence answering all
     * three would be wrong about two of them.
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
    //
    // #1486: `derived` is an anchor this camera did not measure itself and the scorer may
    // nonetheless read a wedge from -- the rotation between this camera's wire ring and an
    // anchored camera's, measured off darts both of them placed. An untrusted one (the
    // default) leaves every line below exactly as it was.
    PointScore scorePoint(Point2f pixel, const DartboardCalibration &calib,
                          const orientation_processing::DerivedAnchor &derived =
                              orientation_processing::DerivedAnchor(),
                          const board_model::Model *physical = nullptr);

    // Process dart scoring from tip detection results
    ScoreResult processScore(
        const vector<Mat> &background_frames,
        const dart_processing::DartStateResult &dart_result,
        const vector<DartboardCalibration> &calib,
        bool debug_mode = false,
        const vector<board_model::Model> *physical = nullptr);

} // namespace score_processing
