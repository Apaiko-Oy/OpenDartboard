// turnaus#1867: a dart the rim-only votes carried, with no usable axis on any camera, whose
// lone camera reads it inside 134.5 mm, publishes as read FLAGGED WITH MISS -- by default,
// where #1855 would have sent it to MISS behind OD_RIM_FALLBACK=on.
//
// The rule is `score_processing::rimContradictsTheReading`, pure, in score_processing.hpp.
// The full replay is not possible: the 2026-10-15 kept frames are three settled stills per
// dart, not the footage a window is detected from. So this holds the rule at the seam that
// takes the logged evidence -- rim carried, the solver's usable-constraint count, the lone
// camera's ring and radius -- with every row read from
// ~/opendartboard/sessions/2026-10-15/device-2.log (board device/2), each by its dart
// number in the day and its reference. `published()` is what the vote branch of
// score_processing.cpp does with the answer, beside #1773's RING-WIRE alternative the live
// board published.
//
//   g++ -std=c++17 -I src -I src/utils -o contra testers/i1867_contradiction_check.cpp
//       $(pkg-config --cflags --libs opencv4)
//
// FIVE PREFIXES, and the mutation harness (i1867_check.sh) counts them:
//   offer:   a window the rule must flag with MISS: the three misses (2787, 2788, 3002) and
//            the three uncorrected hits with the same story (2707, 2881, 2946). Mutation A
//            (the rule never fires) turns exactly these red.
//   double:  a rim-carried lone reading IN the double the rule must not reach -- a real
//            double stood on 18 such darts in the checked span. Mutation B (the rule fires
//            on every unsupported rim-carried reading, the radius ignored) turns these red.
//   kept:    a rim-carried lone single at or beyond 134.5 mm, which #1855 keeps and this
//            rule leaves alone. Mutation B turns these red too.
//   stands:  the score string, which this rule never moves. No mutation reaches these.
//   pure:    the switch on, a dart not rim-carried, a supported dart.

#include <iostream>
#include <string>

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

struct Published
{
    std::string score;
    bool flagged = false;
    std::string alternative;
};

// One logged window. `ring_wire_alt` is what #1773's RING-WIRE offered on the live board
// ("" where the reading was clear of every ring wire's sigma).
struct Window
{
    int dart;
    const char *reference;
    bool rim_carried;
    int usable;
    const char *read;
    const char *ring;
    float radius;
    const char *ring_wire_alt;
    const char *truth; // "MISS", a score, or "stood" (uncorrected in the checked span)
};

// The vote branch, OD_RIM_FALLBACK unset, as score_processing.cpp writes it: the reading
// publishes; RING-WIRE flags it where it offered; the rim contradiction then offers MISS.
static Published published(const Window &w, bool rim_fallback_on = false)
{
    Published p;
    p.score = w.read;
    if (w.ring_wire_alt[0] != '\0')
    {
        p.flagged = true;
        p.alternative = w.ring_wire_alt;
    }
    const RimContradiction c =
        rimContradictsTheReading(!rim_fallback_on, w.rim_carried && w.usable == 0, w.ring, w.radius);
    if (c.flag)
    {
        p.flagged = true;
        p.alternative = "MISS";
    }
    return p;
}

static std::string row(const Window &w)
{
    return std::to_string(w.dart) + " " + w.reference + " " + w.read + " at " + std::to_string(w.radius) +
           " (truth " + w.truth + ")";
}

int main()
{
    // ---- the six misses the issue names (darts 2630..3002) ------------------------------
    const Window six[] = {
        {2630, "01M50FHBTRMBQR9FWGEYW67J4G", false, 0, "D19", "double", 0.98805f, "MISS", "MISS"},
        {2642, "01M50FPEVCVNA2YHQQ4KS43K7P", true, 1, "D11", "double", 0.958701f, "S11", "MISS"},
        {2728, "01M50GFXQ04G06ZQ8CH62GEH6X", true, 0, "D18", "double", 0.955984f, "S18", "MISS"},
        {2787, "01M50GWGE7Z19YSKYCKM30FA88", true, 0, "T6", "triple", 0.585485f, "S6", "MISS"},
        {2788, "01M50GWK2YKGBZ45FWKV8STC70", true, 0, "S18", "single", 0.668277f, "", "MISS"},
        {3002, "01M50KY05BBER4JBDDK66XE5K7", true, 0, "S6", "single", 0.114557f, "25", "MISS"},
    };
    // ---- the three hits with 2787's story, uncorrected in the checked span ---------------
    const Window hits[] = {
        {2707, "01M50G8NQXJTHXM2Z9FXR8V2JV", true, 0, "S19", "single", 0.220157f, "", "stood"},
        {2881, "01M50JZZN5TJWBFKEMKBTFJQEA", true, 0, "S3", "single", 0.465202f, "", "stood"},
        {2946, "01M50KH0Y6JTSHYTKADH4A6F68", true, 0, "S2", "single", 0.647834f, "T2", "stood"},
    };
    // ---- rim-carried, unsupported, in the double: real doubles stood, and one S20 --------
    const Window doubles[] = {
        {2611, "01M50FE5A4NYXHAVKFXXJC138V", true, 0, "D5", "double", 0.998849f, "MISS", "stood"},
        {2683, "01M50G43ACBH536WT7ZGA1TPP9", true, 0, "D1", "double", 0.981442f, "MISS", "stood"},
        {2939, "01M50KETMGH2328JDFT5C9AXWP", true, 0, "D20", "double", 0.954389f, "S20", "S20"},
    };
    // ---- rim-carried, unsupported, a single at or beyond 134.5 mm: #1855 keeps them ------
    const Window kept[] = {
        {2689, "01M50G5TY61JR17Z8Y1F8Y5C5W", true, 0, "S20", "single", 0.914996f, "", "stood"},
        {2828, "01M50JES3ZWS2EHYHA1DTR8PXP", true, 0, "S16", "single", 0.81656f, "", "stood"},
    };

    int asHit = 0, missOffered = 0;
    for (const Window &w : six)
    {
        const Published p = published(w);
        const bool hit = p.score != "MISS";
        asHit += hit ? 1 : 0;
        missOffered += (hit && p.flagged && p.alternative == "MISS") ? 1 : 0;
        std::cout << "I1867 SIX " << w.dart << " " << w.reference << " publishes " << p.score
                  << (p.flagged ? " flagged with " + p.alternative : std::string(" unflagged")) << std::endl;
    }
    std::cout << "I1867 SIX published-as-hit=" << asHit << " of 6, miss-offered=" << missOffered << " of them"
              << std::endl;

    // offer: the rule fires on these six and no others -----------------------------------
    for (const Window &w : {six[3], six[4], six[5], hits[0], hits[1], hits[2]})
    {
        const Published p = published(w);
        say(p.flagged && p.alternative == "MISS", "offer: " + row(w) + " is flagged with MISS");
    }
    // stands: the score string never moves ------------------------------------------------
    for (const Window &w : {six[3], six[4], six[5], hits[0], hits[1], hits[2]})
    {
        say(published(w).score == w.read, "stands: " + row(w) + " publishes " + w.read + " as read");
    }
    // the issue's survivors, each explained: the rule does not reach them ------------------
    say(published(six[0]).alternative == "MISS" && published(six[0]).flagged,
        "pure: 2630 D19 was not rim-carried (2 cameras cleared the scoring floor), and RING-WIRE already "
        "offered MISS (2.0 mm from the outer wire)");
    say(published(six[1]).alternative == "S11",
        "pure: 2642 D11 had 1 usable constraint, so it is supported and this rule does not ask; RING-WIRE's S11 "
        "stands");
    // double: the rim agrees with a double, and real doubles stood ------------------------
    for (const Window &w : {six[2], doubles[0], doubles[1], doubles[2]})
    {
        const Published p = published(w);
        say(p.alternative == w.ring_wire_alt, "double: " + row(w) + " keeps RING-WIRE's " + w.ring_wire_alt);
    }
    // kept: the outer single's outer half is where the rim sees a real single --------------
    for (const Window &w : kept)
    {
        const Published p = published(w);
        say(!p.flagged, "kept: " + row(w) + " publishes unflagged, as read");
    }
    // pure: what the rule must not reach ---------------------------------------------------
    say(!rimContradictsTheReading(false, true, "single", 0.585f).flag,
        "pure: OD_RIM_FALLBACK=on (applies=false) -- #1855 decides, this is not asked");
    say(!rimContradictsTheReading(true, false, "single", 0.585f).flag,
        "pure: a dart not rim-carried-and-unsupported is left alone");
    say(rimContradictsTheReading(true, true, "bull", 3.0f / 170.0f).flag,
        "offer: a lone bull on a rim-carried unsupported dart is offered MISS");
    say(rimContradictsTheReading(true, true, "single", -1.0f).flag &&
            rimContradictsTheReading(true, true, "single", -1.0f).radius_mm < 0.0f,
        "offer: a reading with no radius is offered MISS, as #1707 sent it, with no radius said");

    std::cout << "FAILURES: " << failures << std::endl;
    return failures == 0 ? 0 : 1;
}
