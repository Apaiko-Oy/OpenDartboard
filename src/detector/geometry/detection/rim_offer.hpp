#pragma once
// turnaus#1821: A DART IN THE BOARD PUBLISHED AS AN UNFLAGGED MISS BY THE RIM-ONLY CARRY.
//
// Live on casual board 20, 2026-10-11 00:59:28 (build 0.1.13-1798, OD_LONE_CAMERA=on), the
// third dart of a visit was thrown into single 13 and published `MISS` at 0.5, unflagged; the
// thrower typed S13. The window, verbatim in testers/fixtures/i1821/:
//
//   I1707 RIM CARRIED: 3 camera(s) moved up and 1 of them cleared the floor in the scoring
//     area, under the quorum of 2 -- the rim-only votes carried this dart, so it is at or
//     beyond the double ring (#1707)
//   PATH: DEGRADED -- no geometric entry (TOO-FEW-CONSTRAINTS: no entry: 1 usable
//     constraint(s) of 3 cameras ... cam 1: ... rim only ...; cam 2: ... rim only ...)
//   SCORE: MISS | Position: (-1,-1) | Confidence: 0.500000 | Camera: -1
//
// What that says, read against the code that wrote it:
//   - cameras 1 and 2 were rim only (#1689): their fresh change cleared the floor out to the
//     physical rim and not in the scoring area, so they voted the arrival and offered no
//     figure. Camera 3 is the one that cleared the scoring floor, and the solver's refusal
//     names no reason for it, so its line was the ONE usable constraint;
//   - one line is anywhere along itself: the entry needs two, so there was no geometric
//     answer (TOO-FEW-CONSTRAINTS), and the string vote published;
//   - the vote had no reading at all: no LONE-WIRE line, no "single camera score" line, and
//     the score is the no-winner MISS (`State changed but no valid scores found`, Camera
//     -1). Camera 3 therefore either found no tip, or found one whose own reading was MISS
//     (a tip beyond the outer double -- the flight end of a figure that crosses the
//     double, #1505's measured artifact -- or a wedge walk that failed); `aVoteIsCast`
//     refuses a MISS a vote. The log does not say which; the kept frames would.
//   - #1707's carry decided only that the advance stands. It did not decide MISS: the MISS
//     is what the vote publishes when no camera reads anything, and the carry's sentence
//     "at or beyond the double ring" is the reason nobody looked twice.
//
// THE RULE (OD_RIM_OFFER=on, default off). On a dart the rim-only votes carried, that the
// vote publishes as the no-reading MISS, where EXACTLY ONE camera cleared the scoring floor
// and that camera offered a usable line and found a tip whose wedge its own angular ruler
// reads, the MISS publishes FLAGGED with that wedge's single as the alternative. The score
// is untouched -- MISS stays MISS, at 0.5 -- so no window this rule touches can publish a
// scoring dart; what it changes is that the thrower is offered one tap instead of typing.
//
// Why each clause, against #1802's three phantom misses (01:02:06, 01:02:08, 01:02:10 on
// the same board, a body at the board between two takeouts):
//   - exactly one camera cleared the scoring floor: 01:02:08 had none, so it has no camera
//     to read and nothing to offer; two would not have been carried (quorum 2 of 3);
//   - a usable line: a dart is a straight figure, and a body is not. Both phantoms with one
//     camera clearing the floor (01:02:06, 01:02:10) had 0 usable constraints; the S13 had 1.
//     This is the only clause separating the S13 from those two on the log's own figures;
//   - a tip whose wedge is READ: an asserted 20 (#1346) or a camera with no anchor is a
//     constant, not an observation of where the dart is;
//   - the single and not the double: the one camera that saw the dart in the scoring area
//     is the only evidence that it is inside the outer wire at all, and the rim carry
//     already says "at or near the double". Where a MISS measured a place past the double,
//     #1721's candidate ranking still offers that double after this alternative.
//
// Pure and std-only so testers/i1821_offer_check.cpp holds it without a detector.

#include <cstdlib>
#include <string>
#include <vector>

namespace rim_offer
{
    /** One camera, as the rule needs it. */
    struct Witness
    {
        int camera = -1;              // 0-based slot, as the score line numbers it
        bool cleared_scoring = false; // voted the arrival with its scoring-area count over the floor
        bool usable_line = false;     // the solver's constraint for this camera was usable
        bool tip_found = false;       // the tip search placed a tip
        bool wedge_read = false;      // its own angular ruler read the wedge at that tip
        int segment = -1;             // 1..20 where wedge_read
    };

    struct Offer
    {
        bool flag = false;
        int camera = -1;
        std::string alternative; // "S<n>" where flag
        std::string reason;      // why it fired or why not, one phrase, for the census
    };

    inline Offer decide(bool on, bool rim_carried, bool vote_read, const std::vector<Witness> &witnesses)
    {
        Offer o;
        if (!rim_carried)
        {
            o.reason = "not-rim-carried";
            return o;
        }
        if (vote_read)
        {
            o.reason = "a-camera-reading-published";
            return o;
        }
        int clearing = 0;
        const Witness *w = nullptr;
        for (const Witness &x : witnesses)
        {
            if (x.cleared_scoring)
            {
                clearing++;
                w = &x;
            }
        }
        if (clearing != 1)
        {
            o.reason = clearing == 0 ? "no-camera-cleared-the-scoring-floor" : "more-than-one-camera-cleared";
            return o;
        }
        o.camera = w->camera;
        if (!w->usable_line)
        {
            o.reason = "no-usable-line";
            return o;
        }
        if (!w->tip_found)
        {
            o.reason = "no-tip";
            return o;
        }
        if (!w->wedge_read || w->segment < 1 || w->segment > 20)
        {
            o.reason = "wedge-not-read";
            return o;
        }
        o.alternative = "S" + std::to_string(w->segment);
        if (!on)
        {
            o.reason = "would-offer-switch-off";
            return o;
        }
        o.flag = true;
        o.reason = "offered";
        return o;
    }

    /** OD_RIM_OFFER=on, the exact word; anything else is off. */
    inline bool isOn()
    {
        static const bool v = []
        {
            const char *e = std::getenv("OD_RIM_OFFER");
            return e != nullptr && std::string(e) == "on";
        }();
        return v;
    }

    /** OD_RIM_OFFER_CENSUS=1: one I1821RIM line per rim-carried dart, on and off alike. */
    inline bool censusOn()
    {
        static const bool v = []
        {
            const char *e = std::getenv("OD_RIM_OFFER_CENSUS");
            return e != nullptr && std::string(e) == "1";
        }();
        return v;
    }
} // namespace rim_offer
