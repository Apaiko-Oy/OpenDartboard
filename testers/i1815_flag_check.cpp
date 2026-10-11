// turnaus#1815: six in seven flagged darts were right as published. Under OD_FLAG_SIGMAS=<K>
// (0 < K < 1, default off) a geometric flag is kept only where the solve is within K of its
// own across-wire sigmas of the wire; the rest publish clear. No score moves.
//
// The rule is `score_processing::tightenBoundaryCall`, pure, over #1556's BoundaryCall and the
// solve's own z (EntrySolution::crossingSigmas), and `flagSigmasFrom`, the switch's parse.
// This file holds them on the REAL darts that decided the figure:
//
//   * every geometric flag of the first two real sessions that a marker acted on -- casual
//     boards 17 (2026-10-10) and 20 (2026-10-11), 14 darts, read from the truth lines'
//     `margin_mm`, `sigma_mm`, `wire_kind`, `published` and `alternative` (the data stays
//     on the maintainer's box; the fields are copied here, no reference or time);
//   * the five wrongly-scored flagged darts of the #1555 bakeoff's six default runs
//     (rig-20260918 dev/opening v5.1, rig-20260929 dev/opening v3.1, rig-20260929 dev
//     v9.3), from their I1556PUBLISH lines (runs on #1782's binary, 2026-10-11);
//   * flags that stood as published, from both sessions and the fixtures, which K = 0.7
//     clears.
//
//   g++ -std=c++17 -I src -I src/utils -o flag_check testers/i1815_flag_check.cpp
//       $(pkg-config --cflags --libs opencv4)
//
// FOUR PREFIXES, and the mutation harness (i1815_check.sh) counts them:
//   edge:  a true flag ABOVE z = 0.5 (two: board 20's S20/T20 at 0.524, rig-20260929 dev
//          v9.3 at 0.569) -- kept at K = 0.7, and the first to go if K's comparison slips;
//   keep:  a true flag at z <= 0.5, kept at K = 0.7;
//   cut:   a flag that stood, z > 0.7, cleared at K = 0.7 -- unflagged, no alternative;
//   parse: the switch's text: what turns it on, and what leaves #1556's 1.0;
//   pure:  none of those (the score never moves, K = 1 is #1556's call byte for byte).
// Each assertion carries exactly one prefix, so a mutation's prediction is a count.

#include <cmath>
#include <iostream>
#include <string>
#include <vector>

#include "detector/geometry/detection/score_processing.hpp"

using namespace score_processing;

static int failures = 0;
static int counts[5] = {0, 0, 0, 0, 0};

static void say(bool ok, const std::string &what)
{
    std::cout << (ok ? "OK   " : "FAIL ") << what << std::endl;
    if (!ok)
    {
        failures++;
    }
    const char *p[5] = {"edge:", "keep:", "cut:", "parse:", "pure:"};
    for (int i = 0; i < 5; i++)
    {
        if (what.rfind(p[i], 0) == 0)
        {
            counts[i]++;
        }
    }
}

struct Flag
{
    const char *where;
    const char *published;
    const char *alternative;
    const char *kind;
    double marginMm;
    double sigmaMm;
};

// The marker acted on every one of these: the score was wrong.
static const std::vector<Flag> kTrue = {
    {"board 17 S20 -> S5", "S20", "S5", "wedge", 0.94, 5.65},
    {"board 17 T1 -> T20", "T1", "T20", "wedge", 0.39, 5.00},
    {"board 17 S20 -> S5 (2)", "S20", "S5", "wedge", 0.72, 5.45},
    {"board 17 S6 -> S10", "S6", "S10", "wedge", 1.63, 16.64},
    {"board 17 T5 -> T12", "T5", "T12", "wedge", 1.40, 5.00},
    {"board 17 T12 -> S12", "T12", "S12", "ring", 1.73, 5.00},
    {"board 17 S11 -> S8", "S11", "S8", "wedge", 2.48, 5.32},
    {"board 20 T13 -> S13", "T13", "S13", "ring", 0.80, 5.99},
    {"board 20 D20 -> S20", "D20", "S20", "ring", 0.71, 8.40},
    {"board 20 S7 -> D7", "S7", "D7", "ring", 3.46, 8.07},
    {"board 20 S20 -> None (typed)", "S20", "T20", "ring", 2.87, 5.48},
    {"board 20 S4 -> S13", "S4", "S13", "wedge", 2.14, 15.57},
    {"board 20 S4 -> S18", "S4", "S18", "wedge", 1.14, 5.00},
    {"board 20 S19 -> S3", "S19", "S3", "wedge", 0.99, 5.43},
    {"rig-20260918 dev v5.1 T15 for S15", "T15", "S15", "ring", 1.48, 5.00},
    {"rig-20260918 opening v5.1 T15 for S15", "T15", "S15", "ring", 1.53, 5.00},
    {"rig-20260929 dev v3.1 S6 for S13", "S6", "S13", "wedge", 0.36, 5.96},
    {"rig-20260929 opening v3.1 S6 for S13", "S6", "S13", "wedge", 0.03, 5.94},
    {"rig-20260929 dev v9.3 S19 for T19", "S19", "T19", "ring", 3.32, 5.83},
};

// Each stood as published, and sits further than 0.7 of its sigmas from the wire.
static const std::vector<Flag> kStood = {
    {"board 17 18:33:20 25", "25", "S13", "ring", 4.47, 5.47},
    {"board 17 18:35:41 S5", "S5", "S20", "wedge", 4.72, 5.00},
    {"board 17 18:48:47 T5", "T5", "S5", "ring", 4.05, 5.55},
    {"board 20 21:46:32 S1", "S1", "S18", "wedge", 5.51, 6.13},
    {"board 20 21:46:45 S17", "S17", "T17", "ring", 7.00, 7.33},
    {"board 20 21:51:28 S20", "S20", "S5", "wedge", 4.25, 5.00},
    {"rig-20260918 dev v3.2 S7", "S7", "S16", "wedge", 5.83, 6.65},
    {"rig-20260918 dev v3.3 T20", "T20", "S20", "ring", 5.20, 5.24},
    {"rig-20260922 dev v4.2 T15", "T15", "T10", "wedge", 4.55, 5.00},
};

static BoundaryCall flagOf(const Flag &f)
{
    // The solver's verdict at #1556's 1.0: z <= 1, so these are flagged as published.
    return decideBoundaryCall(true, f.marginMm / f.sigmaMm <= 1.0, f.published, f.alternative, f.kind,
                              f.marginMm, f.sigmaMm);
}

int main()
{
    const double K = 0.7;

    // ---- the switch -------------------------------------------------------------------
    say(flagSigmasFrom(nullptr) == 1.0, "parse: unset is #1556's 1.0");
    say(flagSigmasFrom("") == 1.0, "parse: empty is #1556's 1.0");
    say(std::fabs(flagSigmasFrom("0.7") - 0.7) < 1e-12, "parse: 0.7 turns it on at 0.7");
    say(std::fabs(flagSigmasFrom("0.55") - 0.55) < 1e-12, "parse: 0.55 turns it on at 0.55");
    say(flagSigmasFrom("1.5") == 1.0, "parse: 1.5 would LOOSEN the flag, and is #1556's 1.0");
    say(flagSigmasFrom("0") == 1.0 && flagSigmasFrom("-0.3") == 1.0, "parse: 0 and a negative are #1556's 1.0");
    say(flagSigmasFrom("on") == 1.0 && flagSigmasFrom("0.7x") == 1.0, "parse: a word or trailing text is #1556's 1.0");

    // ---- the true flags: every one kept at K = 0.7 ------------------------------------
    for (const Flag &f : kTrue)
    {
        const BoundaryCall byDefault = flagOf(f);
        const double z = f.marginMm / f.sigmaMm;
        bool tightened = true;
        const BoundaryCall at = tightenBoundaryCall(byDefault, z, K, std::string(), &tightened);
        char what[200];
        snprintf(what, sizeof(what), "%s %s at z %.3f is still flagged with %s", z > 0.5 ? "edge:" : "keep:",
                 f.where, z, f.alternative);
        say(byDefault.flagged && at.flagged && !tightened && at.alternative == f.alternative &&
                at.account == byDefault.account,
            what);
    }

    // ---- the flags that stood: cleared at K = 0.7 -------------------------------------
    for (const Flag &f : kStood)
    {
        const BoundaryCall byDefault = flagOf(f);
        const double z = f.marginMm / f.sigmaMm;
        bool tightened = false;
        const BoundaryCall at = tightenBoundaryCall(byDefault, z, K, std::string(), &tightened);
        char what[200];
        snprintf(what, sizeof(what), "cut: %s at z %.3f publishes clear, nothing offered", f.where, z);
        say(byDefault.flagged && !at.flagged && tightened && at.alternative.empty() && at.others.empty(), what);
        if (&f == &kStood.front())
        {
            std::cout << "  " << at.account << std::endl;
        }
    }

    // ---- what never moves -------------------------------------------------------------
    {
        bool same = true, identical = true;
        for (const std::vector<Flag> *set : {&kTrue, &kStood})
        {
            for (const Flag &f : *set)
            {
                const BoundaryCall byDefault = flagOf(f);
                const double z = f.marginMm / f.sigmaMm;
                for (double k : {0.3, 0.5, 0.7, 0.9})
                {
                    same = same && tightenBoundaryCall(byDefault, z, k).published == byDefault.published;
                }
                const BoundaryCall one = tightenBoundaryCall(byDefault, z, 1.0);
                identical = identical && one.flagged == byDefault.flagged && one.alternative == byDefault.alternative &&
                            one.account == byDefault.account && one.kind == byDefault.kind;
            }
        }
        say(same, "pure: the published score is #1556's at every K, on every dart here");
        say(identical, "pure: at K = 1.0 (the switch off) every call is #1556's, sentence and all");
    }
    {
        // A clear call and a vote publish are never touched, whatever K says.
        const BoundaryCall clear = decideBoundaryCall(true, false, "S20", "S5", "wedge", 7.5, 5.0);
        const BoundaryCall vote = decideBoundaryCall(false, false, "", "", "", -1.0, -1.0);
        const BoundaryCall c2 = tightenBoundaryCall(clear, 1.5, 0.3);
        const BoundaryCall v2 = tightenBoundaryCall(vote, -1.0, 0.3);
        say(!c2.flagged && c2.account == clear.account, "pure: a clear solve stays clear, its sentence unchanged");
        say(!v2.flagged && v2.account.empty(), "pure: a vote publish stays silent");
        // An undefined z (-1) is never read as inside or outside.
        const BoundaryCall f = flagOf(kStood.front());
        say(tightenBoundaryCall(f, -1.0, 0.3).flagged, "pure: a flag with no measured z is left flagged");
    }
    {
        // A corner's others go with the flag (#1782): nothing may be offered unflagged.
        BoundaryCall corner = flagOf(kStood.front());
        corner.others = {"T13", "T6"};
        const BoundaryCall at = tightenBoundaryCall(corner, 0.95, 0.7);
        say(at.others.empty() && at.alternative.empty() && !at.flagged,
            "cut: a corner beyond K offers neither the alternative nor the corner's rest");
        // The sentence says the threshold that cleared it and what the default offered.
        const BoundaryCall s = tightenBoundaryCall(flagOf(kStood[1]), 4.72 / 5.0, 0.7, "two lines");
        say(!s.flagged && s.account.find("S5 clears its nearest wedge wire") != std::string::npos &&
                s.account.find("outside the flag's 0.70") != std::string::npos &&
                s.account.find("would have flagged it with S20") != std::string::npos &&
                s.account.find("(two lines)") != std::string::npos,
            "cut: the sentence names the threshold, the alternative it withheld and the sigma's provenance");
    }

    std::cout << "SENSITIVE edge=" << counts[0] << " keep=" << counts[1] << " cut=" << counts[2]
              << " parse=" << counts[3] << " pure=" << counts[4] << std::endl;
    std::cout << "FAILURES: " << failures << std::endl;
    return failures == 0 ? 0 : 1;
}
