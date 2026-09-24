#pragma once

#include <opencv2/opencv.hpp>
#include <cstdio>
#include <cstdlib>
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
        // #1517 (rescued Codex design): whether this camera fitted every ring a single
        // can be told from a treble or a double by. #1485 zeroes a ring the band check
        // refused and a zeroed ellipse contains no point, so a camera missing its treble
        // ring calls every treble a single -- structurally, on every dart, with nothing
        // in the reading saying so. Where no two cameras agree it is therefore not the
        // one the vote falls back to (chooseScore).
        bool rings_complete = true;
        // #1505: the tip was MEASURED past the outer double but within the board's
        // physical rim -- on the surround, the one place a missed dart really lands and
        // the region the tip search is deliberately masked out to (#1364). False for a
        // MISS from an invalid calibration, from an anchored camera whose wedge walk
        // failed, and for a "tip" beyond the physical rim, which is not a place a dart
        // can be. It is a measurement and it decides nothing on an ordinary run: the
        // repair it makes possible -- letting this eyewitness vote -- was measured and
        // REFUSED, and `aVoteIsCast` below carries the numbers.
        //
        // Measured on mocks/rig-20260918 (i1505_edge_probe, 19 dart events): 8 readings
        // came back MISS with a tip found. TWO were on the surround -- visit 6's
        // off-board dart at 215 mm (camera 1, the honest witness the vote silences)
        // and visit 1's leaning dart at 175 mm on one camera of three. SIX were beyond
        // the rim in that replay, at 238..256 mm -- flight and shaft artifacts on five
        // darts that were scored CORRECTLY -- and the real binary's windows put one of
        // those same flights ON the surround at 1.177, which is the measured reason
        // the franchise stays shut.
        bool on_surround = false;
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
        // #1555: WHICH path named this dart, and what the geometry said, as two fields
        // for #1489's reason -- the count (`confidence`) and the subject are different
        // questions and one number cannot answer both. `geometry_outcome` is
        // entry_intersection::outcomeWord's own vocabulary (SOLVED, WIRE-UNCERTAIN,
        // TOO-FEW-CONSTRAINTS, NEAR-PARALLEL, INCONSISTENT), empty where the solver was
        // never consulted; `degraded` is true exactly where the geometric path is the
        // published one, was asked, refused by name, and the vote published instead.
        // A `degraded` reading is a lone camera's, never a triangulated position.
        bool from_geometry = false;
        bool degraded = false;
        string geometry_outcome;
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
        // #1517: the no-consensus fallback passed over a lower-index camera whose ring
        // set was incomplete to publish this reading. False on a consensus, false when
        // readings[0] was itself complete, and false when no camera was -- true exactly
        // where the preference DECIDED, so the vote's account line can say it did
        // instead of a passed-over calibration failure reading like any other 0.7.
        bool preferred_complete = false;
    };

    // ---- #1555: WHICH PATH PUBLISHES, and the census that decided it -------------------
    //
    // Two paths can name this dart. The STRING VOTE scores a tip per camera and picks a
    // camera's score STRING (chooseScore, above). The GEOMETRIC path transports every
    // reliable camera's fitted shaft axis to the board plane and intersects them into one
    // entry point, scored once (entry_intersection.hpp, #1512). Until this issue the vote
    // published and the geometry ran shadowed behind OD_GEO_SCORE=on.
    //
    // THE MAINTAINER'S RULE IS THAT THE CENSUS DECIDES. Both paths were run over both
    // ground-truthed fixtures, in both calibration windows (#1551: a registry build
    // calibrates at a 3 s seek; OD_SEEK_VIDEO=off holds the same binary at the clip's
    // opening, and on rig-20260922 that is the difference between two admitted cameras
    // and three). The numbers, per fixture, per window, with denominators, are the
    // I1555 SCORECARD block in testers/i1555_census.py's output and are transcribed at
    // `publishedPathIsGeometry()` below, beside the decision they bought.
    //
    // WHAT "THE GEOMETRIC PATH PUBLISHES" MEANS, exactly. It is not "geometry instead of
    // the vote": the solver REFUSES by name (fewer than two usable constraints, near
    // parallel, inconsistent) and a refusal is not a score. The published path is
    // therefore geometry WHERE IT SOLVED and the vote where it did not -- and #1512's
    // contract is that the second half says so out loud: a fallback is labelled as a
    // fallback, never presented as a triangulated position. `PublishDecision::account`
    // is where it is said, and `decidePublishedPath` is the whole of the rule.

    /** #1555: which of the two paths named the dart that is being published. */
    enum class ScorePath
    {
        Vote,    // chooseScore's string vote -- one camera's reading, by consensus or alone
        Geometry // entry_intersection's solved board-plane entry, scored once
    };

    inline const char *scorePathWord(ScorePath p)
    {
        return p == ScorePath::Geometry ? "geometry" : "vote";
    }

    /**
     * #1555's falsification switch, in the od_fix shape #1339, #1348, #1495, #1518 and
     * #1552 established: one binary, the rule chosen at run time, so "different build"
     * is never a confound -- and so the losing path stays reachable and measurable.
     *
     * `OD_SCORE_PATH=vote` publishes the string vote for every dart, exactly as every
     * build before this issue did, with the geometric solve not consulted at all.
     * Anything else, unset included, publishes the census winner.
     */
    inline bool voteIsPinned()
    {
        static const bool v = []
        {
            const char *e = std::getenv("OD_SCORE_PATH");
            return e != nullptr && std::string(e) == "vote";
        }();
        return v;
    }

    /**
     * #1555: THE CENSUS, and the verdict it carries.
     *
     * ===================================================================================
     * PLACEHOLDER -- the constant below is `false` until the fixtures have been measured
     * on THIS tree and the table replaces this paragraph. Nothing is wired on an
     * argument; the numbers go here or the vote keeps publishing.
     * ===================================================================================
     */
    inline constexpr bool kGeometryWonTheCensus = false;

    /**
     * #1555: whether the geometric path is the published one on this tree.
     *
     * A function rather than a bare constant because the decision is a measurement and
     * the pin has to be able to move it on one binary: `OD_SCORE_PATH=vote` restores the
     * losing path without a rebuild, which is what keeps both paths measurable.
     */
    inline bool publishedPathIsGeometry()
    {
        return kGeometryWonTheCensus && !voteIsPinned();
    }

    /** What a published reading is worth, per path. */
    struct PublishDecision
    {
        ScorePath path = ScorePath::Vote;
        bool geometry_asked = false;  // the solver was consulted for this dart at all
        bool geometry_solved = false; // it answered with an entry point AND a score
        // #1512's contract, as a field: why the vote is publishing although the
        // geometric path is the published one. Empty on a geometric publish and on a
        // run where the vote is pinned -- a fallback is a thing that HAPPENED, not the
        // ordinary state of affairs.
        std::string fallback_reason;
        std::string account; // the one sentence the SCORE log prints about the choice
    };

    /**
     * #1555: the publish decision, pure, and deliberately over PRIMITIVES rather than
     * over an EntrySolution. score_processing.hpp is included by four pure checks that
     * link no extra translation unit; entry_intersection.hpp reaches board_model.hpp and
     * wire_model.cpp at link time, and unit_check.sh's own comment records what a header
     * dragging that in costs (1451-scorable did not LINK for two issues). The caller
     * unpacks the solution; this function holds the rule.
     *
     * `geometryEnabled` is publishedPathIsGeometry(). `geometrySolved` is the solver
     * having BOTH an entry point and a valid score for it -- Outcome::Solved and
     * Outcome::UncertainAcrossWire are the two that qualify, and WIRE-UNCERTAIN
     * qualifies deliberately: it is a solved position whose sigma reaches a wire, which
     * is a statement ABOUT a score rather than a refusal to make one, and on
     * rig-20260918 eleven of thirteen solves carry the flag (at ~6 mm precision most
     * darts sit within one sigma of some wire). Refusing it would leave the geometric
     * path publishing two darts in a clip and would be a different decision from the one
     * the census measured.
     */
    inline PublishDecision decidePublishedPath(bool geometryEnabled, bool geometrySolved,
                                               const std::string &outcomeWord,
                                               const std::string &refusalStory)
    {
        PublishDecision out;
        if (!geometryEnabled)
        {
            out.path = ScorePath::Vote;
            out.account = voteIsPinned()
                              ? "PATH: the string vote publishes -- OD_SCORE_PATH=vote is "
                                "pinned, so the geometric solve was not consulted"
                              : "PATH: the string vote publishes";
            return out;
        }
        out.geometry_asked = true;
        if (geometrySolved)
        {
            out.path = ScorePath::Geometry;
            out.geometry_solved = true;
            out.account = "PATH: the geometric entry publishes (" + outcomeWord + ")";
            return out;
        }
        // #1512's contract: reached honestly and labelled as itself. The word DEGRADED
        // and the solver's own refusal are both in the line, because the one thing this
        // must never read as is a triangulated position.
        out.path = ScorePath::Vote;
        out.fallback_reason = outcomeWord + (refusalStory.empty() ? "" : ": " + refusalStory);
        out.account = "PATH: DEGRADED -- no geometric entry (" + out.fallback_reason +
                      "), so the string vote publishes this dart; it is one camera's "
                      "reading and not a triangulated position";
        return out;
    }

    /**
     * #1555: what a GEOMETRIC publish's confidence means, said here because the three
     * numbers mean something and #1489 is why.
     *
     * 0.9 and 0.7 count CAMERAS on the vote's path -- two or more readings agreeing, or
     * one standing alone -- and a solved entry is not a count of agreeing strings. What
     * carries over is the sense of the numbers rather than their mechanism: 0.9 is a
     * call nothing in the measurement argues with, 0.7 is a call with a named reservation
     * beside it. So a solved entry whose one-sigma ellipse clears every call-flipping
     * wire publishes at 0.9, and one whose sigma REACHES such a wire publishes at 0.7 --
     * the reservation being the wire, stated rather than averaged over.
     *
     * A fourth number was refused for #1489's reason, measured one issue on: it would
     * change what a published float means to every client of the WebSocket API while
     * still not telling a reader WHICH kind of 0.9 this was. The subject is a field
     * instead -- `ScoreResult::path` and `ScoreResult::geometry_outcome` -- so the count
     * and the subject stay two questions with two answers.
     */
    inline float geometricConfidence(bool sigmaReachesAWire)
    {
        return sigmaReachesAWire ? 0.7f : 0.9f;
    }

    /**
     * #1555: the census line testers/i1555_census.py parses -- one per called dart,
     * printed only under the census pin (OD_GEO_SCORE=on) so an ordinary run stays as
     * quiet as it was. The `PATH:` account beside it prints unconditionally; this line
     * is the same fact in a shape a parser can read.
     *
     * It is a NEW line rather than two more fields on `I1512ENTRY`, and the reason is
     * mechanical: that line's parser matches the whole head contiguously, so a field
     * inserted anywhere before `story=` silently stops every #1512 census reading
     * anything. It also settles a word that would otherwise have drifted --
     * `I1512ENTRY`'s `published=` has always carried what the STRING VOTE said (at
     * #1512 the vote was the published path, so the two were one thing), and since this
     * issue they can differ. `vote=` here is that same string under its own name, and
     * `score=` is what the board really published.
     */
    inline std::string publishCensusLine(long window, ScorePath path, const std::string &score,
                                         float confidence, bool degraded,
                                         const std::string &outcomeWord,
                                         const std::string &voteScore, float voteConfidence,
                                         const std::string &geometryScore)
    {
        char line[400];
        snprintf(line, sizeof(line),
                 "I1555PUBLISH window=%ld path=%s score=%s conf=%.2f degraded=%d "
                 "outcome=%s vote=%s voteConf=%.2f geo=%s",
                 window, scorePathWord(path), score.c_str(), confidence, degraded ? 1 : 0,
                 outcomeWord.empty() ? "-" : outcomeWord.c_str(),
                 voteScore.c_str(), voteConfidence,
                 geometryScore.empty() ? "NONE" : geometryScore.c_str());
        return line;
    }

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
     * #1505: the physical board's radius over the scoring area's, 225.5/170 -- the same
     * ratio dart_processing's tip mask is widened by (#1364), spelled here because the
     * scorer must agree with the mask about where a tip can BE at all. A tip between
     * the outer double and this rim is on the surround; one beyond it is not a place a
     * dart can land, and on this fixture every such "tip" was a flight or a shaft.
     * The boundary does NOT separate honest surround tips from artifacts -- the real
     * binary read one flight at 1.177 of the board, inside it -- which is half of why
     * `aVoteIsCast` refuses the repair this measurement makes possible.
     * (dart_processing carries its own copy of the ratio; it is another slice's file,
     * so the two are held together by the sentence in both places rather than by an
     * include.)
     */
    inline constexpr float kPhysicalRimOverBoard = 225.5f / 170.0f;

    /**
     * #1505's pin, in the shape #1492 established for a repair that was MEASURED AND
     * REFUSED: OD_SURROUND=votes lets a MISS measured on the surround vote, on the
     * shipping binary, so the refusal below can be re-measured rather than re-argued.
     * It is a pin and nothing reads it on an ordinary run.
     */
    inline bool surroundMissesVote()
    {
        static const bool v = []
        {
            const char *e = std::getenv("OD_SURROUND");
            return e != nullptr && std::string(e) == "votes";
        }();
        return v;
    }

    /**
     * #1505: whether this reading is a vote -- what processScore has always required, a
     * reading that is not a MISS, stated as a decision so the measurement below stays
     * attached to it.
     *
     * THE REPAIR ANYBODY WOULD REACH FOR WAS MEASURED AND REFUSED, and the refusal is
     * the finding. Camera 1 measured visit 6's off-board dart ON THE SURROUND at
     * 215 mm, said MISS, and this rule silenced it, so a camera whose tip -- the same
     * dart's, out of the board plane -- projected 20 mm INSIDE the board stood alone
     * and a dart that never hit the board published S7 at 0.7. Admitting the surround
     * MISS as a reading (OD_SURROUND=votes) repairs exactly that dart: MISS at 0.7,
     * the census's one on-board error gone.
     *
     * Measured on the real binary over the whole of mocks/rig-20260918, the SAME run
     * then flipped visit 3's second dart -- a thrown 7, scored CORRECTLY as S7 -- to
     * MISS: camera 1 held a "tip" on the new dart's own flight at ruler radius 1.177,
     * on the surround by every measure this file has, and outvoted the correct lone S7
     * by the fallback's index order. The honest witness reads 1.147 and the artifact
     * 1.177, three percent apart with the honest one NEARER the board, so no radius
     * separates them -- and the artifact is detection noise: the synchronous replay's
     * window read the same dart's flight at 1.44 and refused it, so the verdict on a
     * correct dart would flicker run to run. One dart repaired, one correct dart
     * broken, in one run: the trade is 1:1 and the acceptance criterion (nothing that
     * is right may go wrong) refuses it. What separates the two readings is WHICH
     * OBJECT the tip was found on, which is the tip machinery's question
     * (#1494/#1495's family), not the vote's.
     *
     * Inline for #1338's reason: a tester holds the decision without a detector.
     */
    inline bool aVoteIsCast(const PointScore &point)
    {
        if (point.score != "MISS")
        {
            return true;
        }
        return point.on_surround && surroundMissesVote();
    }

    /**
     * #1489: how a published reading came by its wedge, in the words the BOARD line has
     * always used. The first two spellings are byte-for-byte what they were; the third
     * is the state that had no name and was printed as the second.
     *
     * #1505: and a published MISS is the fourth state -- the tip was measured off the
     * board and no wedge is any part of the reading. It must not print "wedge by
     * default": that is the census's needle for #1346's asserted 20 (i1484 reads the
     * BOARD line as the independent witness for the 0.5 bucket), and a measured
     * off-board reading printed in those words would count as an assertion nobody made.
     */
    inline string howTheWedgeWasRead(const PointScore &point)
    {
        if (point.score == "MISS")
        {
            return "no wedge, the tip is off the board";
        }
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
     * participated in the window, is calibrated, found a tip, and cast a vote --
     * `aVoteIsCast`, the same MISS exclusion it always was, stated as a decision
     * (#1505). Under that issue's pin a surround MISS needs nothing special here: its
     * string is "MISS", it is neither asserted nor ring-only, so it lands in
     * `readings` and votes like any other -- which is how the refused repair was
     * measured without touching this function. Inline for #1338's reason: a tester
     * holds the vote without building the detector.
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
                // #1517: no two cameras agree -- the first camera that can tell every
                // ring apart, and the first of all only when none can. Taking
                // readings[0] by index alone let a camera whose treble ring the band
                // check had zeroed (#1485) stand alone with the single it is
                // structurally bound to read, over a whole camera's treble beside it,
                // on every dart, at the 0.7 of any lone reading. The preference moves
                // the CHOICE and never the count or the confidence; a consensus is
                // deliberately left alone (re-weighing agreement by completeness would
                // be a new vote, #797's open territory), and so is the asserted-wedge
                // fallback below (a complete constant is still a constant).
                out.camera = readings[0];
                for (int index : readings)
                {
                    if (points[index].rings_complete)
                    {
                        out.camera = index;
                        break;
                    }
                }
                out.preferred_complete = out.camera != readings[0];
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
                              orientation_processing::DerivedAnchor());

    // Process dart scoring from tip detection results
    ScoreResult processScore(
        const vector<Mat> &background_frames,
        const dart_processing::DartStateResult &dart_result,
        const vector<DartboardCalibration> &calib,
        bool debug_mode = false);

} // namespace score_processing
