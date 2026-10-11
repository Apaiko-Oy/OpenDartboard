#pragma once

#include <opencv2/opencv.hpp>
#include <algorithm>
#include <cmath>
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
        // #1556: THE CROSSING, IN THE PUBLISHED OUTPUT, in the idiom the three fields
        // above established -- additive fields rather than a new value of an old one. A
        // dart whose one-sigma position uncertainty reaches a call-flipping wire
        // publishes the MORE PROBABLE candidate immediately (the maintainer's decision on
        // #1557: play never blocks) at the demoted confidence `geometricConfidence`
        // already had, and names the other candidate so a consumer can ask instead of
        // guessing. There is deliberately NO new confidence float meaning "flagged":
        // #1489 refused a fourth number for a reason that holds exactly here -- the count
        // and the subject are two questions, and a float cannot answer both.
        //
        // `uncertainty_mm` is the one-sigma position uncertainty RESOLVED ACROSS the
        // boundary named by `boundary_kind`, floored by measurement; `boundary_mm` is how
        // far that boundary is. Both are -1 on a vote publish, where no millimetre
        // uncertainty was measured at all and inventing one would be worse than silence.
        bool boundary_flagged = false;
        string alternative_score; // the other candidate; empty where nothing is flagged
        string boundary_kind;     // "ring" | "wedge"; empty where nothing could flip
        float boundary_mm = -1.0f;
        float uncertainty_mm = -1.0f;
        // #1487: the published reading's wedge was ASSERTED -- #1346's fallback, the
        // camera the vote published had no anchor and the 20 is what it says rather than
        // what it read. The PointScore flag, carried to the one result that publishes so
        // the fact is a field and not a reading of `confidence == 0.5`. Deliberately NOT
        // copied into DetectorResult: docs/api.md (#1556 rule 1, held on #1628) is the
        // outward contract and says what a vote publish carries; this field feeds the log
        // (`noticeAnAssertedWedge`) and nothing that leaves the board.
        bool wedge_asserted = false;
        // #1721: where else this dart may be, best first, in this vocabulary and uncapped
        // (dart_candidates.hpp ranks it; TurnausClient::detectionBody translates and caps
        // it for the door). Decided AFTER `score` and from it, so it can move nothing that
        // publishes. Not published on the socket: docs/api.md is unchanged.
        vector<string> candidates;
        // #1787: the account's remaining numbers, filled beside the log lines that print
        // them and copied to DetectorResult for the Turnaus body (detector_interface.hpp).
        vector<int> cameras_used;   // 1-based, as the log numbers cameras
        float crossing_deg = -1.0f; // a two-line solve's pair angle (#1766); -1 otherwise
        int agreeing = 0;           // a vote publish's agreeing cameras
        float lone_wire_mm = -1.0f; // LONE-WIRE's published-reading margin; -1 unmeasured
        float ring_wire_mm = -1.0f; // RING-WIRE's (#1773) margin, filled where that line is written; -1 unmeasured
        long window_ordinal = -1;   // the window that called this dart
    };

    /**
     * #1556: WHAT A FLAGGED DART PUBLISHES, pure and over PRIMITIVES for
     * `decidePublishedPath`'s reason -- this header is included by five checks that link
     * no extra translation unit, and entry_intersection.hpp reaches board_model.hpp and
     * wire_model.cpp at link time (unit_check.sh's own comment records what that costs:
     * 1451-scorable did not LINK for two issues). The solver measures the crossing; this
     * function holds what may be said about it.
     */
    struct BoundaryCall
    {
        bool flagged = false;
        std::string published;   // the candidate that publishes -- the more probable one
        std::string alternative; // the other candidate a tap can pick; empty unless flagged
        std::string kind;        // "ring" | "wedge"; empty where nothing could flip
        float boundaryMm = -1.0f;
        float uncertaintyMm = -1.0f;
        std::string account; // the one sentence the SCORE log prints about the crossing
        // #1782: on a corner, the corner's remaining cells after `alternative`, best
        // first. Empty on every other call. dart_candidates ranks them straight after the
        // alternative, so the door's three carry the corner before anything else.
        std::vector<std::string> others;
    };

    /**
     * #1556: the publication rule, and the three things it must never do.
     *
     * 1. A VOTE PUBLISH CARRIES NO MILLIMETRE UNCERTAINTY. The string vote picks a
     *    camera's score STRING; nothing in it measures a board-millimetre position or its
     *    error, so a `boundary_mm` beside one would be a number with no measurement
     *    behind it. Every field stays absent, and a degraded dart is therefore silent
     *    about the crossing rather than confidently clear of it.
     * 2. A FLAG NAMES TWO CANDIDATES OR IT IS NOT A FLAG. #1557's decision is that the
     *    dart publishes the more probable candidate and a tap affirms it or picks the
     *    other; a demotion whose alternative is empty, or is the published score again,
     *    has nothing for a consumer to ask about. Such a reading publishes UNFLAGGED,
     *    and the account says the crossing was measured and could not be named.
     * 3. THE MEASUREMENT IS PUBLISHED WHETHER OR NOT IT FLAGGED. A geometric publish that
     *    clears every wire still carries its `boundary_mm` and `uncertainty_mm`: "how
     *    close was this call" is the question the issue is about, and answering it only
     *    when the answer is "close" makes the absence of a flag unreadable.
     */
    inline BoundaryCall decideBoundaryCall(bool geometryPublished, bool uncertaintyCrossesWire,
                                           const std::string &publishedScore,
                                           const std::string &alternativeScore,
                                           const std::string &boundaryKind,
                                           double boundaryMm, double uncertaintyMm,
                                           // #1766: what the sigma IS when no third line
                                           // checked it (entry_intersection::sigmaProvenance).
                                           // Appended to the sentence whether the call is
                                           // flagged or clear, never to a vote publish (rule 1
                                           // above), and empty on a controlled solve.
                                           const std::string &sigmaProvenance = std::string())
    {
        BoundaryCall out;
        if (!geometryPublished)
        {
            out.account = "";
            return out;
        }
        // #1766: the sentence says what kind of sigma it is spending. Live on 2026-10-08 a
        // two-line solve "cleared" the 3/19 wire at 17.9 mm across a 16.5 mm sigma (1.08
        // sigma) while its six siblings on the same wedge were flagged at 12-15 mm, and
        // nothing in the sentence said the 16.5 was the two cameras' crossing angle on that
        // wedge -- the rig's figure, measured to 0.2% on every fixture two-line solve -- and
        // not a dart's scatter. The threshold is #1556's, unchanged; the reader is told what
        // the sigma is.
        const std::string provenance =
            sigmaProvenance.empty() ? std::string() : " (" + sigmaProvenance + ")";
        out.published = publishedScore;
        out.kind = boundaryKind;
        out.boundaryMm = (float)boundaryMm;
        out.uncertaintyMm = (float)uncertaintyMm;
        const bool nameable = !alternativeScore.empty() && alternativeScore != publishedScore;
        if (uncertaintyCrossesWire && nameable)
        {
            out.flagged = true;
            out.alternative = alternativeScore;
            char margin[64];
            snprintf(margin, sizeof(margin), "%.1f mm across a %.1f mm one-sigma",
                     boundaryMm, uncertaintyMm);
            out.account = "UNCERTAINTY: " + publishedScore + " or " + alternativeScore +
                          " -- the nearest " + (boundaryKind.empty() ? "scoring" : boundaryKind) +
                          " wire is " + margin +
                          ", so " + publishedScore +
                          " publishes now as the more probable candidate and is flagged; a "
                          "tap affirms it or appends the other" + provenance;
            return out;
        }
        if (uncertaintyCrossesWire)
        {
            // Measured as crossing, and unnameable. Said out loud rather than published
            // as a clear call: the reading a reader must not mistake for a comfortable
            // one is exactly this.
            out.account = "UNCERTAINTY: " + publishedScore +
                          " -- the uncertainty reaches a wire and no second candidate "
                          "could be named, so nothing is flagged";
            return out;
        }
        char margin[96];
        snprintf(margin, sizeof(margin), "%.1f mm away across a %.1f mm one-sigma",
                 boundaryMm, uncertaintyMm);
        out.account = "UNCERTAINTY: " + publishedScore + " clears its nearest " +
                      (boundaryKind.empty() ? std::string("scoring") : boundaryKind) +
                      " wire -- " + margin + provenance;
        return out;
    }

    /**
     * #1556: the flag's own census line, printed from the same one place `publishCensusLine`
     * is and parsed by testers/i1556_census.py.
     *
     * `board=` IS THE SCORE THE BOARD PUBLISHED and `score=` is the crossing's subject,
     * and they are two fields for a measured reason. `BoundaryCall::published` is empty on
     * a VOTE publish by rule 1 above -- a string vote measures no board-millimetre
     * position, so there is no crossing and nothing to be the subject of one. The first
     * run of this census read that empty field as the published score, and the four darts
     * rig-20260918's dev window publishes by the vote came back as `S-`: three of them
     * correctly scored, all four counted WRONG, turning a catch table of 1 of 2 into 1 of
     * 5. A census cannot judge what was published from a field about something else.
     */
    inline std::string flagCensusLine(long window, const BoundaryCall &call, float confidence,
                                      bool fromGeometry, const std::string &boardScore)
    {
        char line[440];
        snprintf(line, sizeof(line),
                 "I1556PUBLISH window=%ld geometry=%d flagged=%d score=%s alt=%s kind=%s "
                 "boundary=%.2f sigma=%.2f conf=%.2f board=%s",
                 window, fromGeometry ? 1 : 0, call.flagged ? 1 : 0,
                 call.published.empty() ? "-" : call.published.c_str(),
                 call.alternative.empty() ? "-" : call.alternative.c_str(),
                 call.kind.empty() ? "-" : call.kind.c_str(),
                 call.boundaryMm, call.uncertaintyMm, confidence,
                 boardScore.empty() ? "-" : boardScore.c_str());
        return line;
    }

    // ---- turnaus#1815: THE FLAG'S THRESHOLD, TIGHTENED BEHIND A SWITCH ------------------
    //
    // The first two real sessions (casual boards 17 and 20, 2026-10-10/11, 820 darts, read
    // back through turnaus#1786/#1789) flagged 396 darts and 340 of them stood as published:
    // a false-alarm rate of 0.865 and 0.849. Recomputed from the truth lines' own fields
    // (the geometric flag is `margin_mm <= sigma_mm`, #1556's z <= Params::crossingSigmas;
    // that reproduces the boards' verdict on 819 of 820 darts, the one a 5.00/5.00 rounding
    // tie), every flagged geometric dart a marker corrected -- 14 through the flag, one
    // typed -- sat at z <= 0.52, and 117 of the 288 false geometric flags sat above 0.6.
    // The fixtures' bakeoff (rig-20260918/22/29, six default runs, 71 flagged geometric
    // darts) has its nearest true flag at z = 0.57 (rig-20260929 dev v9.3, S19 published
    // for a thrown T19, T19 offered).
    //
    // So the tightening keeps a geometric flag only where the solve is within K of its own
    // across-wire sigmas of the wire, K < 1, and publishes the rest as the clear call they
    // are at that threshold -- unflagged, at 0.9, with a sentence saying which threshold
    // cleared it and what the default would have offered. THE PUBLISHED SCORE NEVER MOVES:
    // only `flagged`, `alternative`, the corner's `others` and the demoted confidence do.
    // The vote path's flags (#1773's ring wire, #1707's rim) are not touched: their false
    // flags spread over every margin with the true ones among them (1.3 to 4.8 mm), and
    // nothing in the sessions separates them.
    //
    // OD_FLAG_SIGMAS=<K>, 0 < K < 1, turns it on; unset, empty, unparseable or outside that
    // range is #1556's 1.0, byte for byte. docs/rig.md records the measurement and the value
    // it proposes (0.7: the fixtures' 0.57 with 0.13 sigma to spare). A figure read off
    // fifteen session darts and five fixture ones is why it is not the default.

    /** turnaus#1815: the switch's value from its text, pure; 1.0 (#1556's) unless a K in (0, 1). */
    inline double flagSigmasFrom(const char *text)
    {
        if (text == nullptr || *text == '\0')
        {
            return 1.0;
        }
        char *end = nullptr;
        const double k = std::strtod(text, &end);
        if (end == text || *end != '\0' || !(k > 0.0) || !(k < 1.0))
        {
            return 1.0;
        }
        return k;
    }

    /** turnaus#1815: OD_FLAG_SIGMAS, read once. */
    inline double flagCrossingSigmas()
    {
        static const double v = flagSigmasFrom(std::getenv("OD_FLAG_SIGMAS"));
        return v;
    }

    /**
     * turnaus#1815: #1556's call, re-asked at the tightened threshold. `crossingSigmas` is
     * the solve's own z (EntrySolution::crossingSigmas: boundaryAcrossMm / sigmaAcrossMm,
     * -1 undefined) and `k` the switch's value. Returns `call` untouched unless it is
     * flagged, k < 1 and z > k; then the same call unflagged, with the sentence a clear
     * call prints and what the default would have offered. `*tightened` says which.
     */
    inline BoundaryCall tightenBoundaryCall(const BoundaryCall &call, double crossingSigmas, double k,
                                            const std::string &sigmaProvenance = std::string(),
                                            bool *tightened = nullptr)
    {
        if (tightened != nullptr)
        {
            *tightened = false;
        }
        if (!call.flagged || !(k < 1.0) || crossingSigmas < 0.0 || crossingSigmas <= k)
        {
            return call;
        }
        BoundaryCall out = call;
        out.flagged = false;
        out.alternative.clear();
        out.others.clear();
        char margin[240];
        snprintf(margin, sizeof(margin),
                 " wire -- %.1f mm away across a %.1f mm one-sigma (%.2f sigma), outside the flag's "
                 "%.2f (OD_FLAG_SIGMAS, #1815); #1556's 1.0 would have flagged it with %s",
                 call.boundaryMm, call.uncertaintyMm, crossingSigmas, k, call.alternative.c_str());
        out.account = "UNCERTAINTY: " + call.published + " clears its nearest " +
                      (call.kind.empty() ? std::string("scoring") : call.kind) + margin +
                      (sigmaProvenance.empty() ? std::string() : " (" + sigmaProvenance + ")");
        if (tightened != nullptr)
        {
            *tightened = true;
        }
        return out;
    }

    /**
     * turnaus#1815's census line, one per called dart under the census pin: the solve's z,
     * the threshold in force, what #1556's 1.0 decided and what published. Read by
     * testers/i1815_census.py beside the truth tables.
     */
    inline std::string flagThresholdCensusLine(long window, const BoundaryCall &byDefault,
                                               const BoundaryCall &published, double crossingSigmas, double k)
    {
        char line[320];
        snprintf(line, sizeof(line),
                 "I1815FLAG window=%ld z=%.3f k=%.2f default=%d flagged=%d score=%s alt=%s kind=%s",
                 window, crossingSigmas, k, byDefault.flagged ? 1 : 0, published.flagged ? 1 : 0,
                 byDefault.published.empty() ? "-" : byDefault.published.c_str(),
                 byDefault.alternative.empty() ? "-" : byDefault.alternative.c_str(),
                 byDefault.kind.empty() ? "-" : byDefault.kind.c_str());
        return line;
    }

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
     * #1555: THE CENSUS, AND THE VERDICT IT CARRIES.
     *
     * Measured by testers/i1555_run.sh on this tree (origin/main at e5509c3 merged in),
     * five whole-clip replays, every one with OD_GEO_SCORE=on OD_SHAFT_CENSUS=1 so both
     * paths answer about the same dart in the same process. Exact score against the
     * fixtures' ground truth, aligned by the spatial matcher i1511_census::assign_events
     * -- the same one #1511 and #1512 use, so no two censuses here can disagree about
     * which detection was which dart. Denominators are MATCHED darts; segmentation is
     * counted apart (#1552) and enters none of them.
     *
     *   fixture          window   matched  VOTE     GEOM-ONLY  GEOMETRY-FIRST
     *   ---------------  -------  -------  -------  ---------  --------------
     *   rig-20260918     dev           17  13/17    12/13      15/17
     *   rig-20260918     opening       17  13/17    13/14      15/17
     *   rig-20260922     dev            6   0/6      0/2        0/6
     *   rig-20260922     opening        5   0/5      0/4        0/5
     *   POOLED                         45  26/45               30/45   (+4)
     *
     * GEOMETRY-FIRST is the column the decision is taken on, and the other two are there
     * to say why. GEOMETRY-ONLY (92-93%) is a figure about the instrument on its own
     * denominator -- the solver chooses which darts it answers about, and three of the
     * four it refused on rig-18 are darts the vote gets right, so a path is not more
     * accurate for refusing a quarter of a clip. GEOMETRY-FIRST carries the vote's own
     * denominator: the solve where there is one, the vote where the solver refused by
     * name. It wins on rig-20260918 by the same +2 in BOTH calibration windows,
     * independently, and pooled by +4 of 45.
     *
     * WHAT MOVES, on rig-20260918, and the two windows agree on the wins and differ on
     * the loss. THREE darts the vote gets wrong come back right in BOTH windows: v1.1,
     * a thrown T13 the vote publishes S13 (a ring error, #1492's 5.5 mm short tip), and
     * v2.1 and v4.1, both thrown 19 and both published S7 (wedge errors). ONE DART THE
     * VOTE GETS RIGHT GOES WRONG IN EACH WINDOW, AND IT IS NOT THE SAME DART:
     *
     *   dev      v2.3, a thrown S5, solves D12 -- position error 67.5/46.2 mm, ZERO of
     *            three placed tips corroborating. The SAME dart solves S5 correctly in
     *            the opening window.
     *   opening  v5.1, a thrown S15, solves T15 -- a ring error at the treble band. The
     *            SAME dart solves S15 correctly in the dev window.
     *
     * So the loss is one marginal call per window rather than a stable defect, and the
     * trade is 3:1 in both. That is NOT #1505's acceptance criterion satisfied -- that
     * issue refused a 1:1 trade on the rule that nothing which is right may go wrong.
     * This is taken under the rule the maintainer set for THIS issue, which is pooled
     * accuracy, and the regression is written here because a census that listed only its
     * wins would not be one.
     *
     * RIG-20260922 DECIDED NOTHING, AND ITS ZERO IS THE REFERENCE RATHER THAN EITHER
     * PATH. Both paths read 0 in both windows, so no reading of that fixture can change
     * the ordering -- but the number is not a scoring fact. Only 9 of its 24 throws are
     * annotated at all (truth visits 1-3, testers/i1511_annotations/rig-20260922.csv),
     * two of those nine are --no-arrival, and the matcher maps detected visits onto
     * consecutive truth-visit RANGES: with the annotation stopping at truth visit 3
     * while the opening window detects 8 visits, the annotated range is free to slide
     * along the run, and it does. Measured on the census's own page: truth v1.2, a
     * thrown 16, is matched to a detection that published T9 while the detection that
     * published S16 is left unclaimed, and the same one-visit shift stands a thrown T8
     * beside a published S1 with the T8 unclaimed. `I1555 REFERENCE-GAP` is the census
     * saying so on every run. The remedy is annotations for that fixture's visits 4-8,
     * which is not this issue.
     *
     * So: geometry-first wins pooled, wins on the one fixture that can tell the two
     * paths apart, wins in both of that fixture's calibration windows separately, and is
     * beaten on no fixture and in no window. That is neither a tie nor a split, so the
     * constant is true. It is one word to reverse, and OD_SCORE_PATH=vote reverses it on
     * a shipped binary with no rebuild at all.
     */
    inline constexpr bool kGeometryWonTheCensus = true;

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
     *
     * #1556 CHANGED WHAT `sigmaReachesAWire` MEASURES AND DELIBERATELY NOT WHAT IT SAYS.
     * The sentence above is unchanged and the two numbers are unchanged; what moved is
     * that the sigma is now resolved ACROSS the wire it is measured against rather than
     * taken as the longest axis of the ellipse in every direction, and that a demotion is
     * only published where the other candidate can be NAMED. That same issue is where a
     * fourth number would have been invented if the flag had been given one -- it is a
     * field pair (`boundary_flagged`, `alternative_score`), for exactly #1489's reason.
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
     * #1487: the falsifier. `OD_ASSERTED_WEDGE=unsaid` is the board before this issue --
     * an asserted 20 published and the notice below never said -- reachable on one binary
     * so the tester can show the notice is doing something. It changes no score: the
     * fallback, the confidence and every per-dart line are what they are either way.
     */
    inline bool assertedWedgeGoesUnsaid()
    {
        static const bool v = []
        {
            const char *e = std::getenv("OD_ASSERTED_WEDGE");
            return e != nullptr && std::string(e) == "unsaid";
        }();
        return v;
    }

    /**
     * #1487: A DART WHOSE WEDGE NOBODY MEASURED IS SAID TO BE ONE -- ONCE.
     *
     * #1346 decided the fallback: where no camera can name a wedge the vote publishes
     * the 20 at 0.5, because a Marker correcting a wrong 20 is a better evening than a
     * board that drops darts. That stands. What it lacked was a sentence: per dart the
     * log says `wedge by default` on the BOARD line and "No measured wedge" beside it,
     * both INFO and both easy to read past, and the float is the only thing that says it
     * to anybody else. This is the board-level half: the FIRST asserted dart of a run is
     * said in words, and no later one is (#1457: a refusal re-said on every look is
     * noise an operator learns to skip).
     *
     * WHICH SEVERITY, AND WHY IT REUSES #1501 RATHER THAN REPEATING IT. Where no camera
     * could be read for a wedge at start (`readable == 0`), the board has already said so
     * as a WARNING -- #1449's "every dart will publish as an asserted 20", beside #1501's
     * BOARD RECOGNITION verdict naming OD_CAMERA_WEDGES -- so this is INFO and points
     * back at those lines instead of saying them a third time. Where some camera CAN be
     * read, start said nothing alarming, and a dart that reached only unanchored cameras
     * is news: that is the WARNING. Pure and over primitives, so a tester holds it.
     *
     * `published_asserted` is ScoreResult::wedge_asserted; `already_said` is the
     * caller's latch; `unsaid` is assertedWedgeGoesUnsaid().
     */
    struct AssertedWedgeNotice
    {
        bool say = false;
        bool warn = false;
        std::string sentence;
    };

    inline AssertedWedgeNotice noticeAnAssertedWedge(bool published_asserted, bool already_said,
                                                     int readable, int cameras,
                                                     const std::string &score, float confidence,
                                                     int camera, bool unsaid)
    {
        AssertedWedgeNotice n;
        if (unsaid || !published_asserted || already_said)
        {
            return n;
        }
        n.say = true;
        n.warn = readable > 0;
        char conf[16];
        snprintf(conf, sizeof(conf), "%.1f", (double)confidence);
        const std::string first =
            "ASSERTED WEDGE: a dart was published as " + score + " at " + conf +
            " from camera " + std::to_string(camera) +
            " with NO measured wedge -- the 20 is #1346's assertion, not a reading, and " +
            "the ring and radius are all that were measured. ";
        const std::string once =
            " Said once for this run: every later dart like it says `wedge by default` on "
            "its BOARD line and nothing more.";
        if (n.warn)
        {
            n.sentence = first + std::to_string(readable) + " of " + std::to_string(cameras) +
                         " cameras can be read for a wedge, and none of them placed this dart." +
                         once;
        }
        else
        {
            n.sentence = first + "No camera on this board can be read for a wedge: the "
                                 "start-up WARNING said every dart would be asserted, the "
                                 "BOARD RECOGNITION line beside it says why, and "
                                 "OD_CAMERA_WEDGES is the remedy." +
                         once;
        }
        return n;
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

    // ---- #1628: a lone reading that sits on a wire does not stand alone over a clear one --
    //
    // WHAT WENT WRONG, measured on rig-20260922 dev v7.2 (a thrown S3) with #1605's budget
    // and #1618's alignment on. The geometric entry REFUSED the dart -- TOO-FEW-CONSTRAINTS,
    // every camera's axis "not straight" (5.06, 4.70 and 3.53 px RMS against the 2.5 px
    // gate: the new dart's silhouette merged with v7.1's beside it) -- so the vote
    // published it. No two cameras agreed, and #1517's fallback took the first camera
    // that fitted every ring, which is camera 1. Camera 1's tip is 2-4 px from the hand
    // annotation, and its angular ruler put it at 189.7 degrees -- 0.7 degrees, 0.3 mm of
    // arc at a 21 mm radius, past the 3/19 wire -- so S19 published at 0.7 with nothing
    // in the output saying the call was a coin toss. Camera 2 read S3 in the middle of
    // its wedge. The fallback asked WHICH camera, never HOW CLOSE its reading was.
    //
    // THE RULE. Where no two cameras agree, the reading the fallback would publish is
    // checked against the wedge wires by its own rulers: its distance to the nearest
    // wedge wire in board millimetres is `radius * 170 mm * sin(angle to that wire)`.
    // If that distance is inside kLoneReadingSigmaMm, and another voting camera read a
    // DIFFERENT score whose own distance to every wedge wire is at least that sigma --
    // and it is no less ring-complete than the reading it would replace (#1517's
    // preference is not undone) -- the clearest such reading publishes instead, at the
    // same 0.7. Otherwise nothing changes. A consensus is never touched.
    //
    // WHY WEDGE WIRES ONLY. The wedge a reading names IS its angular fraction: the
    // distance above is measured by the same ruler that made the call. The ring is not --
    // scorePoint decides it by ellipse containment in pixels, and the radial ruler that
    // gives `radius` is a separate instrument with its own measured biases (#1492's short
    // tip, #1553's treble bloom), so a "ring margin" from it would be a number about a
    // different measurement than the call. A bull or an outer bull has no wedge and is
    // never near a wedge wire nor clear of one; the rule leaves it where it is.
    //
    // WHY 5.0 mm. It is #1556's measured floor on the SOLVED entry's across-boundary
    // sigma (entry_intersection Params::sigmaAcrossFloorMm; the instrument reads ~6 mm).
    // A lone camera's tip read on its own rulers is not a better instrument than several
    // cameras intersected, so the solve's floor is the least a lone reading's one-sigma
    // can be. It is reused, not fitted: score_processing.cpp asserts the two are equal.
    //
    // MEASURED AND REFUSED AS THE DEFAULT. Over #1555's five replays with #1605's budget
    // and #1618's alignment on (testers/i1628_census.py over every dart of both fixtures
    // in both windows), the rule reselects on the vote path exactly twice: rig-20260922
    // dev v7.2 S19 -> S3 (wrong to right) and rig-20260922 OPENING v7.2 S3 -> S11 (right
    // to wrong -- the dart named at risk before the run). In the opening window camera 1
    // reads the correct S3 0.86 mm from the same wire, and camera 2 reads S11 9.18 mm
    // clear of ITS wires: a clear reading of the wrong wedge. Camera 2's tip sits at a
    // ruler radius of ~55 mm for a dart at ~21 mm, and its angular ruler moves that same
    // pixel from 180 degrees (dev) to the 11 (opening); a margin measured by a ruler says
    // nothing about whether the ruler is right. Pooled accuracy stays 67/84, a 1:1 trade,
    // and no sigma separates the two (2.5 mm: 1 gained, 2 lost; 5 and 7.5: 1 and 1; 10:
    // nothing moves). The nothing-correct-may-regress rule refuses it.
    //
    // So the DEFAULT is #1517's fallback, unchanged, and the check runs only to SAY how
    // close the lone reading was (the LONE-WIRE account, the I1628LONE census line).
    // OD_LONE_WIRE=clear turns the reselection on, on the same binary, so the refusal can
    // be re-measured rather than re-argued (#1505's shape for a refused repair).
    inline constexpr float kLoneReadingSigmaMm = 5.0f;
    inline constexpr float kScoringRadiusMm = 170.0f; // DartboardSpec::outerDoubleRadius

    inline bool loneWireReselectIsOn()
    {
        static const bool v = []
        {
            const char *e = std::getenv("OD_LONE_WIRE");
            return e != nullptr && std::string(e) == "clear";
        }();
        return v;
    }

    /**
     * #1628: this reading's distance to its nearest WEDGE wire in board millimetres, by
     * its own rulers; -1 where the reading measured no wedge (asserted, ring-only, a
     * MISS) or has no radius or angle to measure it with. Wedge wires sit at 9 + 18k
     * degrees in BoardPosition's frame.
     */
    inline float wedgeWireMarginMm(const PointScore &p)
    {
        if (p.score == "MISS" || !p.wedge_measured || p.ring_only || p.wedge_asserted ||
            !p.board.has_angle || !p.board.has_radius || p.board.radius < 0.0f)
        {
            return -1.0f;
        }
        float d = std::fmod(p.board.angle - 9.0f + 720.0f, 18.0f);
        d = std::min(d, 18.0f - d);
        return p.board.radius * kScoringRadiusMm * std::sin(d * (float)CV_PI / 180.0f);
    }

    // ---- #1773: the RING wires, which #1628's check never measured ----------------------
    //
    // Live on 2026-10-10 (build 2b56b48, 56 darts) three vote publishes went out
    // unflagged at 0.7 and 0.9 with a ring wire inside one sigma of the reading:
    //   15:51:49  a thrown S19 published T19, lone camera 1, `radius=0.600382`: 102.1 mm
    //             from the bull, 3.1 mm inside the treble's inner wire at 99 and 4.9 mm
    //             inside its outer one at 107. LONE-WIRE said "14.4 mm from a wedge wire,
    //             clear of the 5 mm sigma" -- true, and about the wrong wire.
    //   16:06:25  a thrown S1 published OUTER, lone camera 0, `radius=0.050671`: 8.6 mm,
    //             2.3 mm outside the bull's wire at 6.35. No LONE-WIRE line at all: a
    //             ring-only reading has no wedge, so wedgeWireMarginMm returns -1 and the
    //             check returned before it said anything.
    //   16:47:22  a thrown BULL published OUTER from TWO cameras agreeing,
    //             `radius=0.040479`: 6.9 mm, 0.5 mm outside the bull's wire. No LONE-WIRE
    //             line because `agreeing == 2`, and nothing else on the vote path
    //             measures a ring wire.
    // The only ring-wire awareness the vote path had was #1707's rim fallback: the DOUBLE
    // only, and only under OD_RIM_FALLBACK=on. The rule below is that fallback's flag shape
    // (`boundary_flagged`, `alternative_score`, `boundary_kind == "ring"`, the confidence
    // demoted to #1556's 0.7) for EVERY ring wire, on by default, and it applies to a
    // consensus reading as well as a lone one: two cameras agreeing on the STRING "OUTER"
    // is not a second measurement of the radius -- the published radius is one camera's
    // ruler -- and a millimetre from the wire is a millimetre whichever camera read it.
    //
    // THE SCORE DOES NOT MOVE. #1628 measured a reselection on the vote path at 1:1 and
    // refused it; this is not a reselection. The dart publishes what it always did, the
    // ring across the wire is offered as #1556's alternative, and the thrower taps.
    //
    // THE SIGMA IS #1628's 5 mm (kLoneReadingSigmaMm): the floor on what one camera's
    // ruler can say about a tip's position. The wire radii are the spec's, the same
    // model `boardRadius` reads the radius against; score_processing.cpp static_asserts
    // each against DartboardSpec, the way kScoringRadiusMm already is.

    inline constexpr float kBullWireMm = 6.35f;        // bull | outer
    inline constexpr float kBull25WireMm = 15.9f;      // outer | single
    inline constexpr float kInnerTrebleWireMm = 99.0f; // single | triple
    inline constexpr float kOuterTrebleWireMm = 107.0f; // triple | single
    inline constexpr float kInnerDoubleWireMm = 162.0f; // single | double
    // double | MISS is kScoringRadiusMm, 170.

    /**
     * #1773: this reading's distance to the nearest RING wire of the ring it was scored
     * in, in board millimetres by its own ruler, with the wire named and the ring across
     * it. `marginMm` is -1 where the reading is a MISS or has no radius. `across` is the
     * ring word on the other side of that wire ("single", "triple", "double", "bull",
     * "outer", or "miss" past the outer double).
     *
     * The wires asked are the published RING's own two edges, not the two nearest the
     * radius: the ring is decided by the camera's fitted ellipses and the radius by its
     * radial ruler, and when the two disagree (a treble by the ellipse at a ruler radius
     * of 95 mm) the margin is the distance to the ring's edge the ruler has crossed, and
     * the alternative is the ring on the far side of it -- which is what the thrower
     * should be offered.
     */
    struct RingWireMargin
    {
        float marginMm = -1.0f; // |radius - wire|; -1 where nothing was measured
        float radiusMm = -1.0f; // the reading's radius by its own ruler, in mm
        float wireMm = -1.0f;   // the wire's radius
        std::string wire;       // "the treble's inner wire", "the bull's wire", ...
        std::string across;     // the ring on the other side of the wire
    };

    inline RingWireMargin ringWireMarginMm(const PointScore &p)
    {
        RingWireMargin out;
        if (p.score == "MISS" || p.ring.empty() || !p.board.has_radius || p.board.radius < 0.0f)
        {
            return out;
        }
        out.radiusMm = p.board.radius * kScoringRadiusMm;
        struct Wire
        {
            float mm;
            const char *name;
            const char *across;
        };
        Wire inner{-1.0f, "", ""}, outer{-1.0f, "", ""};
        if (p.ring == "bull")
        {
            outer = {kBullWireMm, "the bull's wire", "outer"};
        }
        else if (p.ring == "outer")
        {
            inner = {kBullWireMm, "the bull's wire", "bull"};
            outer = {kBull25WireMm, "the 25 ring's wire", "single"};
        }
        else if (p.ring == "single")
        {
            // A single spans two bands, 15.9..99 inside the treble and 107..162 outside
            // it; the ruler says which the tip is in, split at the treble's middle (a
            // ruler radius inside the treble band on a reading the ellipses called
            // single goes to the nearer treble wire either way).
            if (out.radiusMm < (kInnerTrebleWireMm + kOuterTrebleWireMm) / 2.0f)
            {
                inner = {kBull25WireMm, "the 25 ring's wire", "outer"};
                outer = {kInnerTrebleWireMm, "the treble's inner wire", "triple"};
            }
            else
            {
                inner = {kOuterTrebleWireMm, "the treble's outer wire", "triple"};
                outer = {kInnerDoubleWireMm, "the double's inner wire", "double"};
            }
        }
        else if (p.ring == "triple")
        {
            inner = {kInnerTrebleWireMm, "the treble's inner wire", "single"};
            outer = {kOuterTrebleWireMm, "the treble's outer wire", "single"};
        }
        else if (p.ring == "double")
        {
            inner = {kInnerDoubleWireMm, "the double's inner wire", "single"};
            outer = {kScoringRadiusMm, "the double's outer wire", "miss"};
        }
        else
        {
            return out;
        }
        const float dIn = inner.mm > 0.0f ? std::fabs(out.radiusMm - inner.mm) : -1.0f;
        const float dOut = outer.mm > 0.0f ? std::fabs(out.radiusMm - outer.mm) : -1.0f;
        const Wire &nearer = (dIn >= 0.0f && (dOut < 0.0f || dIn <= dOut)) ? inner : outer;
        out.marginMm = (&nearer == &inner) ? dIn : dOut;
        out.wireMm = nearer.mm;
        out.wire = nearer.name;
        out.across = nearer.across;
        return out;
    }

    /**
     * #1773: the score string on the other side of a ring wire, in the vote's own
     * vocabulary; empty where it cannot be named. A single, treble or double across the
     * wire needs the reading's segment, and a ring-only reading (a BULL or an OUTER) has
     * none: its angle is filled by `scorePoint` from slot 0 when the camera is not
     * anchored -- an asserted 20 that `wedge_asserted` does not mark, because #1346
     * marks only the asserted scores -- so naming a single from it would be naming the
     * 20 by default. #1556 rule 2: a flag that cannot name its second candidate is not a
     * flag, and the account says so instead.
     */
    inline std::string scoreAcrossRingWire(const PointScore &p, const std::string &across)
    {
        if (across == "bull")
        {
            return "BULL";
        }
        if (across == "outer")
        {
            return "OUTER";
        }
        if (across == "miss")
        {
            return "MISS";
        }
        if (p.ring_only || p.segment < 1 || p.segment > 20)
        {
            return std::string();
        }
        const char prefix = across == "single" ? 'S' : across == "triple" ? 'T' : across == "double" ? 'D' : '\0';
        if (prefix == '\0')
        {
            return std::string();
        }
        return std::string(1, prefix) + std::to_string(p.segment);
    }

    /** #1773: what the ring-wire check said about the vote's reading. */
    struct RingWireCall
    {
        bool checked = false;     // a vote reading with a measured radius was asked
        bool near_wire = false;   // the ring margin is inside the sigma
        bool flagged = false;     // ... and the ring across it could be named
        std::string alternative;  // the score across the wire; empty unless flagged
        float confidence = 0.5f;  // what publishes: the choice's, demoted to 0.7 if flagged
        RingWireMargin ring;      // the measurement
        float wedge_margin_mm = -1.0f; // #1628's margin of the same reading, for the sentence
        std::string account;      // the RING-WIRE log sentence; empty where nothing was checked
    };

    /**
     * #1773: the rule, pure, over the vote's chosen reading. It applies to every vote
     * publish that measured a radius -- lone or consensus -- and not to a wedge-by-default
     * reading (`choice.by_default`), which already publishes at 0.5 as "nobody measured
     * the wedge": a T20 whose 20 is asserted has an S20 across its wire that is asserted
     * too, and offering one asserted score as the alternative to another is not a
     * two-candidate flag about anything measured.
     *
     * WHEN BOTH A WEDGE WIRE AND A RING WIRE ARE INSIDE THE SIGMA, the ring across is
     * what is offered and the sentence says the wedge wire is there, and nearer if it is.
     * The wedge neighbour is not offered: #1628 measured acting on the wedge margin and
     * refused it, and the same question on the geometric path is #1782's. A reading can
     * only carry one alternative (#1556's shape; docs/api.md), and this issue adds the
     * ring one.
     */
    inline RingWireCall checkVoteReadingAgainstRingWires(const PointScore &p, const ScoreChoice &choice,
                                                         float sigmaMm = kLoneReadingSigmaMm)
    {
        RingWireCall out;
        out.confidence = choice.confidence;
        if (choice.camera < 0 || choice.agreeing < 1 || choice.by_default)
        {
            return out;
        }
        out.ring = ringWireMarginMm(p);
        out.wedge_margin_mm = wedgeWireMarginMm(p);
        if (out.ring.marginMm < 0.0f)
        {
            return out;
        }
        out.checked = true;
        out.near_wire = out.ring.marginMm < sigmaMm;
        char head[200];
        snprintf(head, sizeof(head), "camera %d's %s (%s) sits %.1f mm from %s by its own ruler, %.1f mm from the bull",
                 choice.camera, p.score.c_str(),
                 choice.agreeing >= 2 ? (std::to_string(choice.agreeing) + " cameras agreeing").c_str() : "alone",
                 out.ring.marginMm, out.ring.wire.c_str(), out.ring.radiusMm);
        char sigma[48];
        snprintf(sigma, sizeof(sigma), "%.0f mm", sigmaMm);
        if (!out.near_wire)
        {
            out.account = std::string("RING-WIRE: ") + head + ", clear of the " + sigma + " sigma";
            return out;
        }
        const std::string alternative = scoreAcrossRingWire(p, out.ring.across);
        std::string wedge;
        if (out.wedge_margin_mm >= 0.0f && out.wedge_margin_mm < sigmaMm)
        {
            char w[160];
            snprintf(w, sizeof(w),
                     "; its nearest wedge wire is inside the sigma too, at %.1f mm%s, said by LONE-WIRE and not offered (#1628)",
                     out.wedge_margin_mm, out.wedge_margin_mm < out.ring.marginMm ? " and nearer" : "");
            wedge = w;
        }
        if (alternative.empty() || alternative == p.score)
        {
            out.account = std::string("RING-WIRE: ") + head + ", inside the " + sigma +
                          " sigma, but the " + out.ring.across +
                          " across it cannot be named from a reading that never asked the angular ruler, "
                          "so nothing is flagged (#1556 rule 2)" + wedge;
            return out;
        }
        out.flagged = true;
        out.alternative = alternative;
        out.confidence = std::min(choice.confidence, geometricConfidence(true));
        char tail[200];
        snprintf(tail, sizeof(tail),
                 ", inside the %s sigma, so %s publishes flagged with %s across that wire at %.1f (#1773)",
                 sigma, p.score.c_str(), alternative.c_str(), out.confidence);
        out.account = std::string("RING-WIRE: ") + head + tail + wedge;
        return out;
    }

    /**
     * #1773's census line, one per called dart under the census pin, beside I1628LONE:
     * the vote's chosen reading and its ring-wire margin, whether the ring check flagged
     * it and with what. Parsed by testers/i1773_census.py, which joins it to I1555PUBLISH
     * for the path and to the truth tables for the verdict.
     */
    inline std::string ringWireCensusLine(long window, const ScoreChoice &choice, const PointScore &p,
                                          const RingWireCall &call)
    {
        char line[320];
        snprintf(line, sizeof(line),
                 "I1773RING window=%ld agreeing=%d cam=%d score=%s ring=%s radius_mm=%.2f wire_mm=%.2f "
                 "margin=%.2f wedge_margin=%.2f checked=%d near=%d flagged=%d alt=%s conf=%.2f",
                 window, choice.agreeing, choice.camera + 1,
                 choice.camera >= 0 ? p.score.c_str() : "-",
                 p.ring.empty() ? "-" : p.ring.c_str(),
                 call.ring.radiusMm, call.ring.wireMm, call.ring.marginMm, call.wedge_margin_mm,
                 call.checked ? 1 : 0, call.near_wire ? 1 : 0, call.flagged ? 1 : 0,
                 call.alternative.empty() ? "-" : call.alternative.c_str(), call.confidence);
        return line;
    }

    // ---- #1782: A CORNER OFFERS THE CORNER ------------------------------------------------
    //
    // Live on 2026-10-10 16:38:53 (build 2b56b48) a thrown S19 published T3 from a two-line
    // solve at 188.03 deg and radius 0.6346, 5.3 mm sigma: 0.6 mm inside the treble's outer
    // wire and 1.8 mm on the 3 side of the 3/19 wire. #1556 measures the crossing to the
    // NEAREST wire in sigmas and names the one cell across it, so the flag said "T3 or S3"
    // -- and the four cells of that corner are T3, S3, T19 and S19, all within one sigma.
    //
    // THE RULE. Where the solve is within the flag's threshold (Params::crossingSigmas, 1.0)
    // of a ring wire AND a wedge wire, and the cell across each can be named:
    //   1. the three other cells are ranked by their probability under the solve's own
    //      covariance -- a bivariate normal over the offsets toward the two wires, with the
    //      two floored across-wire sigmas and the unfloored correlation between them. The
    //      ellipse is anisotropic and generally tilted to the radial frame, so the cell
    //      across the nearer wire in mm is not necessarily the likelier one;
    //   2. where a camera the solve did NOT use read one of those cells with its own
    //      reading clear of every wire -- its wedge margin and its ring margin both at
    //      least #1628's 5 mm (kLoneReadingSigmaMm) -- that cell goes first (#1675's shape:
    //      a camera with a clear view of a cell is a measurement, not a coin). Where two
    //      such cameras disagree, the one with the larger smaller margin wins;
    //   3. the first is the flag's `alternative` (Turnaus's `alternative` field); the rest
    //      are `others`, and dart_candidates puts them next.
    // THE PUBLISHED SCORE NEVER MOVES: it stays the cell the solve is in, whatever the
    // ranking says about the other three. Nothing that is not flagged becomes flagged:
    // a corner is inside the nearest wire's sigma by construction, so the flagged set is
    // #1556's. OD_CORNER_FLAG=nearest restores #1556's one alternative on the same binary.
    inline bool cornerFlagIsOn()
    {
        static const bool v = []
        {
            const char *e = std::getenv("OD_CORNER_FLAG");
            return !(e != nullptr && std::string(e) == "nearest");
        }();
        return v;
    }

    /** #1782: the standard normal CDF. */
    inline double normalCdf(double x)
    {
        return 0.5 * std::erfc(-x / std::sqrt(2.0));
    }

    /**
     * #1782: P(X < a, Y < b) for a standard bivariate normal with correlation rho, by
     * Simpson's rule over x of phi(x) Phi((b - rho x) / sqrt(1 - rho^2)). Accurate to
     * ~1e-6 at 400 panels, which is far finer than any ranking here needs.
     */
    inline double bivariateNormalCdf(double a, double b, double rho)
    {
        rho = std::max(-0.999, std::min(0.999, rho));
        if (std::fabs(rho) < 1e-9)
        {
            return normalCdf(a) * normalCdf(b);
        }
        const double lo = -9.0;
        if (a <= lo)
        {
            return 0.0;
        }
        const double s = std::sqrt(1.0 - rho * rho);
        const int n = 400; // even
        const double h = (a - lo) / n;
        auto f = [&](double x)
        { return std::exp(-0.5 * x * x) / std::sqrt(2.0 * CV_PI) * normalCdf((b - rho * x) / s); };
        double sum = f(lo) + f(a);
        for (int i = 1; i < n; i++)
        {
            sum += f(lo + i * h) * (i % 2 == 1 ? 4.0 : 2.0);
        }
        return sum * h / 3.0;
    }

    /** #1782: a corner, as the solver measured it, in primitives. */
    struct CornerCells
    {
        bool corner = false; // all three other cells' names could be read
        std::string ringAlt, wedgeAlt, diagonal;
        double ringMm = -1.0, wedgeMm = -1.0;           // to each wire
        double sigmaRingMm = -1.0, sigmaWedgeMm = -1.0; // floored, across each wire
        double rho = 0.0; // correlation of the offsets toward the two wires
    };

    /** #1782: another camera's own reading, for rule 2. */
    struct CameraReading
    {
        int camera = -1;          // 0-based, as LONE-WIRE numbers cameras
        bool used = false;        // its line was in the solve
        std::string score;
        float wedgeMarginMm = -1.0f; // #1628's
        float ringMarginMm = -1.0f;  // #1773's
    };

    inline bool readingIsClear(const CameraReading &r, float sigmaMm)
    {
        return r.wedgeMarginMm >= sigmaMm && r.ringMarginMm >= sigmaMm;
    }

    /** #1782: one ranked cell. */
    struct CornerCell
    {
        std::string score;
        double probability = 0.0;
    };

    /**
     * #1782: the three other cells of a corner and their probabilities, merged where two
     * name the same score (a single beside the 25 ring: the diagonal is OUTER again),
     * most probable first. The published cell's own probability is returned in
     * `publishedProbability`. Empty where `cells.corner` is false.
     */
    /**
     * #1782: whether the solve is within `k` of its own across-wire sigma of BOTH wires --
     * #1556's threshold, Params::crossingSigmas, passed by the caller so this header
     * stays clear of entry_intersection.hpp.
     */
    inline bool insideBothSigmas(const CornerCells &cells, double k)
    {
        return cells.corner && cells.sigmaRingMm > 0.0 && cells.sigmaWedgeMm > 0.0 &&
               cells.ringMm >= 0.0 && cells.wedgeMm >= 0.0 &&
               cells.ringMm / cells.sigmaRingMm <= k && cells.wedgeMm / cells.sigmaWedgeMm <= k;
    }

    inline std::vector<CornerCell> rankCornerCells(const CornerCells &cells,
                                                   const std::string &published,
                                                   double *publishedProbability = nullptr)
    {
        std::vector<CornerCell> out;
        if (!cells.corner || !(cells.sigmaRingMm > 0.0) || !(cells.sigmaWedgeMm > 0.0))
        {
            return out;
        }
        const double a = cells.ringMm / cells.sigmaRingMm;
        const double b = cells.wedgeMm / cells.sigmaWedgeMm;
        const double both = bivariateNormalCdf(a, b, cells.rho); // stays in the cell
        const double pa = normalCdf(a), pb = normalCdf(b);
        if (publishedProbability != nullptr)
        {
            *publishedProbability = both;
        }
        const CornerCell raw[3] = {{cells.ringAlt, pb - both},
                                   {cells.wedgeAlt, pa - both},
                                   {cells.diagonal, 1.0 - pa - pb + both}};
        for (const CornerCell &c : raw)
        {
            if (c.score.empty() || c.score == published)
            {
                continue;
            }
            auto it = std::find_if(out.begin(), out.end(),
                                   [&](const CornerCell &o) { return o.score == c.score; });
            if (it == out.end())
            {
                out.push_back(c);
            }
            else
            {
                it->probability += c.probability;
            }
        }
        std::stable_sort(out.begin(), out.end(), [](const CornerCell &x, const CornerCell &y)
                         { return x.probability > y.probability; });
        return out;
    }

    /**
     * #1782: the rule above, applied to #1556's call. `nearest` is decideBoundaryCall's
     * answer; this returns it unchanged unless it is flagged, the solve is a corner and
     * the switch is on. `readings` is every voting camera's own reading.
     */
    inline BoundaryCall decideCornerCall(const BoundaryCall &nearest, const CornerCells &cells,
                                         const std::vector<CameraReading> &readings,
                                         // #1766's sentence tail, as decideBoundaryCall had it
                                         const std::string &sigmaProvenance = std::string(),
                                         double crossingSigmas = 1.0,
                                         bool on = cornerFlagIsOn(),
                                         float sigmaMm = kLoneReadingSigmaMm)
    {
        if (!nearest.flagged || !insideBothSigmas(cells, crossingSigmas))
        {
            return nearest;
        }
        double pPublished = 0.0;
        std::vector<CornerCell> ranked = rankCornerCells(cells, nearest.published, &pPublished);
        if (ranked.empty())
        {
            return nearest;
        }
        // Rule 2: an unused camera's clear reading of a corner cell goes first.
        int clearCamera = -1;
        float clearMargin = -1.0f;
        size_t clearAt = 0;
        for (const CameraReading &r : readings)
        {
            if (r.used || !readingIsClear(r, sigmaMm))
            {
                continue;
            }
            for (size_t i = 0; i < ranked.size(); i++)
            {
                const float m = std::min(r.wedgeMarginMm, r.ringMarginMm);
                if (ranked[i].score == r.score && m > clearMargin)
                {
                    clearCamera = r.camera;
                    clearMargin = m;
                    clearAt = i;
                }
            }
        }
        std::vector<CornerCell> order = ranked;
        if (clearCamera >= 0 && clearAt > 0)
        {
            const CornerCell first = order[clearAt];
            order.erase(order.begin() + (long)clearAt);
            order.insert(order.begin(), first);
        }

        auto pct = [](double p)
        {
            char b[16];
            snprintf(b, sizeof(b), "%.0f%%", 100.0 * p);
            return std::string(b);
        };
        std::string names, probs;
        for (const CornerCell &c : order)
        {
            names += (names.empty() ? "" : ", ") + c.score;
        }
        for (const CornerCell &c : ranked)
        {
            probs += (probs.empty() ? "" : ", ") + c.score + " " + pct(c.probability);
        }
        char wires[160];
        snprintf(wires, sizeof(wires),
                 "a ring wire %.1f mm across a %.1f mm one-sigma and a wedge wire %.1f mm across a "
                 "%.1f mm one-sigma",
                 cells.ringMm, cells.sigmaRingMm, cells.wedgeMm, cells.sigmaWedgeMm);
        std::string why;
        if (clearCamera >= 0)
        {
            char c[200];
            snprintf(c, sizeof(c),
                     "; camera %d, which the solve did not use, read %s %.1f mm clear of every wire, so it "
                     "is offered first",
                     clearCamera, order.front().score.c_str(), clearMargin);
            why = c;
        }

        if (!on)
        {
            BoundaryCall out = nearest;
            out.account += " [a corner: " + std::string(wires) + "; OD_CORNER_FLAG=nearest offers only " +
                           nearest.alternative + ", where the corner would offer " + names + "]";
            return out;
        }
        BoundaryCall out = nearest;
        out.alternative = order.front().score;
        out.others.clear();
        for (size_t i = 1; i < order.size(); i++)
        {
            out.others.push_back(order[i].score);
        }
        // The sentence keeps #1556's opening ("UNCERTAINTY: <published> or <alternative>")
        // so every reader of that shape still finds the pair; the rest is the corner's.
        const std::string provenance =
            sigmaProvenance.empty() ? std::string() : " (" + sigmaProvenance + ")";
        out.account = "UNCERTAINTY: " + nearest.published + " or " + names + " -- a corner, " + wires +
                      ", so " + nearest.published + " publishes now as the most probable cell (" +
                      pct(pPublished) + ") and is flagged; by the solve's covariance " + probs + why +
                      "; a tap affirms it or appends another" + provenance;
        return out;
    }

    /**
     * #1782's census line, one per called dart under the census pin: whether the solve
     * was a corner, its cells and probabilities, #1556's nearest alternative and what the
     * corner offers, and every voting camera's own reading with whether the solve used it
     * and whether it was clear. Parsed by testers/i1782_census.py.
     */
    inline std::string cornerCensusLine(long window, const BoundaryCall &nearest, const BoundaryCall &corner,
                                        const CornerCells &cells, const std::vector<CameraReading> &readings,
                                        double crossingSigmas = 1.0, float sigmaMm = kLoneReadingSigmaMm)
    {
        double pPublished = 0.0;
        const std::vector<CornerCell> ranked = rankCornerCells(cells, nearest.published, &pPublished);
        std::string cellText;
        for (const CornerCell &c : ranked)
        {
            char b[48];
            snprintf(b, sizeof(b), "%s%s:%.3f", cellText.empty() ? "" : ",", c.score.c_str(), c.probability);
            cellText += b;
        }
        std::string offered = corner.alternative;
        for (const std::string &o : corner.others)
        {
            offered += "," + o;
        }
        std::string cams;
        for (const CameraReading &r : readings)
        {
            char b[96];
            snprintf(b, sizeof(b), " cam%d=%s/%d/%.2f/%.2f/%d", r.camera + 1, r.score.c_str(), r.used ? 1 : 0,
                     r.wedgeMarginMm, r.ringMarginMm, readingIsClear(r, sigmaMm) ? 1 : 0);
            cams += b;
        }
        char line[512];
        snprintf(line, sizeof(line),
                 "I1782CORNER window=%ld flagged=%d corner=%d score=%s nearest_alt=%s offered=%s "
                 "ring_mm=%.2f wedge_mm=%.2f sigma_ring=%.2f sigma_wedge=%.2f rho=%.3f p_published=%.3f "
                 "cells=%s",
                 window, nearest.flagged ? 1 : 0, insideBothSigmas(cells, crossingSigmas) ? 1 : 0,
                 nearest.published.empty() ? "-" : nearest.published.c_str(),
                 nearest.alternative.empty() ? "-" : nearest.alternative.c_str(),
                 offered.empty() ? "-" : offered.c_str(), cells.ringMm, cells.wedgeMm, cells.sigmaRingMm,
                 cells.sigmaWedgeMm, cells.rho, pPublished, cellText.empty() ? "-" : cellText.c_str());
        return std::string(line) + cams;
    }

    /** #1628: what the wire check did to the vote's choice. */
    struct LoneWireCheck
    {
        ScoreChoice choice;          // what publishes
        bool checked = false;        // a no-consensus measured reading was asked at all
        bool near_wire = false;      // the fallback's reading is inside the sigma
        bool reselected = false;     // a clear reading replaced it
        int passed_over = -1;        // the camera whose near-wire reading was replaced
        float margin_mm = -1.0f;     // the fallback's reading's margin
        float chosen_margin_mm = -1.0f; // the published reading's margin
        std::string account;         // the log sentence; empty when nothing was checked
    };

    /**
     * #1628: the rule above, pure, applied AFTER chooseScore so the vote itself (and
     * #1346's and #1517's checks of it) is unchanged. `pinnedOff` is
     * !loneWireReselectIsOn() at the call site -- true by default, because the
     * reselection was measured and refused -- and a parameter here so a tester can hold
     * both answers in one process.
     */
    inline LoneWireCheck checkLoneReadingAgainstWires(const vector<PointScore> &points,
                                                     const vector<bool> &may_vote,
                                                     const ScoreChoice &choice,
                                                     bool pinnedOff,
                                                     float sigmaMm = kLoneReadingSigmaMm)
    {
        LoneWireCheck out;
        out.choice = choice;
        if (choice.camera < 0 || choice.agreeing != 1 || choice.by_default ||
            choice.camera >= (int)points.size())
        {
            return out;
        }
        const PointScore &lone = points[choice.camera];
        out.margin_mm = wedgeWireMarginMm(lone);
        out.chosen_margin_mm = out.margin_mm;
        if (out.margin_mm < 0.0f)
        {
            return out;
        }
        out.checked = true;
        out.near_wire = out.margin_mm < sigmaMm;
        // #1773: and the nearest RING wire beside it, naming whichever is nearer. Live on
        // 2026-10-10 this sentence read "14.4 mm from a wedge wire, clear of the 5 mm
        // sigma" about a T19 3.1 mm inside the treble's inner wire -- true, and about the
        // wrong wire. The rest of the sentence is still about the wedge check; the ring
        // wire's flag is checkVoteReadingAgainstRingWires's and prints as RING-WIRE.
        const RingWireMargin ringWire = ringWireMarginMm(lone);
        char head[240];
        if (ringWire.marginMm >= 0.0f)
        {
            snprintf(head, sizeof(head), "camera %d's %s is %.1f mm from a wedge wire and %.1f mm from %s (the %s is nearer)",
                     choice.camera, lone.score.c_str(), out.margin_mm, ringWire.marginMm, ringWire.wire.c_str(),
                     ringWire.marginMm < out.margin_mm ? "ring wire" : "wedge wire");
        }
        else
        {
            snprintf(head, sizeof(head), "camera %d's %s is %.1f mm from a wedge wire",
                     choice.camera, lone.score.c_str(), out.margin_mm);
        }
        if (!out.near_wire)
        {
            out.account = std::string("LONE-WIRE: ") + head + ", clear of the " +
                          std::to_string((int)sigmaMm) + " mm sigma";
            return out;
        }
        int best = -1;
        float bestMargin = -1.0f;
        for (size_t i = 0; i < points.size(); i++)
        {
            if ((int)i == choice.camera || i >= may_vote.size() || !may_vote[i])
            {
                continue;
            }
            const PointScore &p = points[i];
            if (p.score == lone.score || (lone.rings_complete && !p.rings_complete))
            {
                continue;
            }
            const float m = wedgeWireMarginMm(p);
            if (m >= sigmaMm && m > bestMargin)
            {
                best = (int)i;
                bestMargin = m;
            }
        }
        if (best < 0)
        {
            out.account = std::string("LONE-WIRE: ") + head +
                          ", inside the sigma, and no other camera read a different score "
                          "clear of every wedge wire, so it stands";
            return out;
        }
        if (pinnedOff)
        {
            out.account = std::string("LONE-WIRE: ") + head + ", inside the sigma; camera " +
                          std::to_string(best) + "'s " + points[best].score +
                          " is clear, but the reselection is off (OD_LONE_WIRE=clear turns it on; "
                          "#1628 measured it 1:1), so it stands";
            return out;
        }
        out.reselected = true;
        out.passed_over = choice.camera;
        out.chosen_margin_mm = bestMargin;
        out.choice.camera = best;
        out.choice.preferred_complete = false;
        out.choice.ring_only = points[best].ring_only;
        char tail[200];
        snprintf(tail, sizeof(tail),
                 ", inside the %.1f mm sigma, so camera %d's %s, %.1f mm clear of every "
                 "wedge wire, publishes instead",
                 sigmaMm, best, points[best].score.c_str(), bestMargin);
        out.account = std::string("LONE-WIRE: ") + head + tail;
        return out;
    }

    /**
     * #1628's census line, one per called dart under the census pin: every voting
     * camera's reading and wedge-wire margin, what #1517's fallback chose and what
     * publishes. Parsed by testers/i1628_census.py.
     */
    inline std::string loneWireCensusLine(long window, const vector<PointScore> &points,
                                          const vector<bool> &may_vote,
                                          const ScoreChoice &before, const LoneWireCheck &after)
    {
        std::string cams;
        for (size_t i = 0; i < points.size(); i++)
        {
            char c[96];
            const bool votes = i < may_vote.size() && may_vote[i];
            snprintf(c, sizeof(c), " cam%zu=%s/%.2f/%d/%d", i + 1,
                     votes ? points[i].score.c_str() : "-", wedgeWireMarginMm(points[i]),
                     points[i].rings_complete ? 1 : 0, votes ? 1 : 0);
            cams += c;
        }
        char line[200];
        snprintf(line, sizeof(line),
                 "I1628LONE window=%ld agreeing=%d fallback=%d published=%d near=%d "
                 "reselected=%d margin=%.2f",
                 window, before.agreeing, before.camera + 1, after.choice.camera + 1,
                 after.near_wire ? 1 : 0, after.reselected ? 1 : 0, after.margin_mm);
        return std::string(line) + cams;
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
