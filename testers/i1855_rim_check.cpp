// #1855: the to-MISS half of #1707's rim fallback reads the lone reading's radius. A lone
// camera's reading on a dart the rim-only votes carried, with no usable axis on any camera,
// publishes MISS only when its own ruler puts it inside kRimMissBelowMm -- the middle of the
// outer single band, 134.5 mm -- and publishes as read at or beyond it.
//
// The rule is `score_processing::rimCarriedFallback`, pure, in score_processing.hpp. This
// file holds it on every rim-carried lone reading with no usable line that the live
// sessions have recorded with a truth, each by reference, and on the cases the radius must
// not reach.
//
//   g++ -std=c++17 -I src -I src/utils -o rim_check testers/i1855_rim_check.cpp
//       $(pkg-config --cflags --libs opencv4)
//
// THREE PREFIXES, and the mutation harness (i1855_check.sh) counts them:
//   kept:  a reading the radius rule must leave a score (at or beyond 134.5 mm). The 0.926
//          S20 is one. Mutation A (the radius never keeps anything -- #1707's rule as it
//          was) turns every one of these red and nothing else.
//   miss:  a reading the radius rule must send to MISS that a rule keeping everything off
//          the bull would publish (15.9 mm < radius < 134.5 mm). The 0.562 miss is one.
//          Mutation B (the line moved in to the 25's wire) turns every one red and nothing
//          else.
//   pure:  structure neither mutation touches: the switch off, a dart not rim-carried, a
//          reading without a radius, a bull, the double's flag half.
//
// The live readings (rim-carried, 0 usable constraints, `No consensus, using single camera
// score`), as #1707 and docs/rig.md record them:
//   L1  2026-10-10 15:39:19, build 2b56b48: S20 from camera 0, radius 0.926, thrown S20.
//   L2  2026-10-10 15:39:23, same visit:     S20 from camera 0, radius 0.562, thrown MISS.
//   L3  2026-09-30 run 2 dart 2.6.3, #1689 build: S5,  radius 0.47,  thrown MISS.
//   L4  2026-09-30 run 2 dart 2.9.3:                S18, radius 0.658, thrown MISS.
//   L5  2026-09-30 run 3:                           S3,  radius 0.461, thrown MISS.
//   L6  2026-09-30 run 3:                           D14, radius 0.981, thrown MISS (the
//       flag half: rim-carried and unsupported, but in the double).

#include <cmath>
#include <iostream>
#include <string>

#include "detector/geometry/detection/score_processing.hpp"

using namespace score_processing;

static int failures = 0;
static int keptN = 0;
static int missN = 0;

static void say(bool ok, const std::string &what)
{
    std::cout << (ok ? "OK   " : "FAIL ") << what << std::endl;
    if (!ok)
    {
        failures++;
    }
    if (what.rfind("kept:", 0) == 0)
    {
        keptN++;
    }
    if (what.rfind("miss:", 0) == 0)
    {
        missN++;
    }
}

// What the publish site does with the rule's answer (score_processing.cpp, the vote branch).
static std::string published(const RimFallback &f, const std::string &read)
{
    return f.to_miss ? std::string("MISS") : read;
}

int main()
{
    std::cout << "kRimMissBelowMm=" << kRimMissBelowMm << " (radius " << kRimMissBelowMm / kScoringRadiusMm
              << ")" << std::endl;

    // ---- the live readings, each by reference -------------------------------------------
    {
        const RimFallback f = rimCarriedFallback(true, true, "single", 20, 0.926f);
        say(!f.to_miss && f.kept && !f.flag && published(f, "S20") == "S20",
            "kept: L1 2026-10-10 15:39:19 S20 at 0.926 (157.4 mm), thrown S20, publishes S20");
    }
    {
        const RimFallback f = rimCarriedFallback(true, true, "single", 20, 0.562f);
        say(f.to_miss && !f.kept && published(f, "S20") == "MISS",
            "miss: L2 2026-10-10 15:39:23 S20 at 0.562 (95.5 mm), thrown MISS, publishes MISS");
    }
    {
        const RimFallback f = rimCarriedFallback(true, true, "single", 5, 0.47f);
        say(f.to_miss && published(f, "S5") == "MISS",
            "miss: L3 2026-09-30 2.6.3 S5 at 0.47 (79.9 mm), thrown MISS, publishes MISS");
    }
    {
        const RimFallback f = rimCarriedFallback(true, true, "single", 18, 0.658f);
        say(f.to_miss && published(f, "S18") == "MISS",
            "miss: L4 2026-09-30 2.9.3 S18 at 0.658 (111.9 mm), thrown MISS, publishes MISS");
    }
    {
        const RimFallback f = rimCarriedFallback(true, true, "single", 3, 0.461f);
        say(f.to_miss && published(f, "S3") == "MISS",
            "miss: L5 2026-09-30 run 3 S3 at 0.461 (78.4 mm), thrown MISS, publishes MISS");
    }
    {
        const RimFallback f = rimCarriedFallback(true, true, "double", 14, 0.981f);
        say(!f.to_miss && !f.kept && f.flag && f.alternative == "MISS" && published(f, "D14") == "D14",
            "pure: L6 2026-09-30 run 3 D14 at 0.981, thrown MISS: the flag half, D14 flagged with MISS");
    }

    // The real singles the same records name beside a rim-only camera that were NOT
    // rim-carried with no usable line, so neither #1707 nor this rule ever reached them:
    // 2026-09-30 run 2 S20 at 0.871 (1 usable line), run 2 S3 at 0.84 and run 3 S18 at
    // 0.75 (2 cameras in the scoring area). The S18 is 127.5 mm, INSIDE the line: had it
    // been rim-carried and unsupported, this rule would have published it MISS.
    {
        const RimFallback a = rimCarriedFallback(true, false, "single", 20, 0.871f);
        const RimFallback b = rimCarriedFallback(true, false, "single", 3, 0.84f);
        const RimFallback c = rimCarriedFallback(true, false, "single", 18, 0.75f);
        say(!a.to_miss && !b.to_miss && !c.to_miss && !a.kept && !b.kept && !c.kept,
            "pure: 2026-09-30 S20 0.871, S3 0.84, S18 0.75 (supported or not rim-carried) are never asked");
    }

    // ---- the line itself, either side ---------------------------------------------------
    {
        const RimFallback f = rimCarriedFallback(true, true, "single", 20, 135.0f / 170.0f);
        say(f.kept && !f.to_miss, "kept: a single at 135.0 mm, just outside the line, publishes as read");
    }
    {
        const RimFallback f = rimCarriedFallback(true, true, "single", 20, 134.0f / 170.0f);
        say(f.to_miss && !f.kept, "miss: a single at 134.0 mm, just inside the line, publishes MISS");
    }
    {
        const RimFallback f = rimCarriedFallback(true, true, "single", 20, 0.926f);
        say(f.kept && std::fabs(f.radius_mm - 157.42f) < 0.01f,
            "kept: the kept reading carries its radius in mm for the log line (157.42)");
    }
    {
        // A treble is inside the line by construction (its outer wire is 107).
        const RimFallback f = rimCarriedFallback(true, true, "triple", 20, 104.0f / 170.0f);
        say(f.to_miss, "miss: a treble at 104 mm publishes MISS");
    }

    // ---- what the radius must not reach -------------------------------------------------
    {
        const RimFallback f = rimCarriedFallback(true, true, "bull", -1, 3.0f / 170.0f);
        say(f.to_miss && !f.kept, "pure: a bull at 3 mm publishes MISS");
    }
    {
        const RimFallback f = rimCarriedFallback(true, true, "single", 20, -1.0f);
        say(f.to_miss && !f.kept, "pure: a single with no radius has no ruler to ask and publishes MISS, as #1707");
    }
    {
        const RimFallback f = rimCarriedFallback(false, true, "single", 20, 0.562f);
        say(!f.to_miss && !f.kept && !f.flag, "pure: OD_RIM_FALLBACK unset (applies=false) changes nothing");
    }
    {
        const RimFallback f = rimCarriedFallback(true, false, "single", 20, 0.562f);
        say(!f.to_miss && !f.kept && !f.flag, "pure: a dart not rim-carried-and-unsupported is left alone");
    }
    {
        const RimFallback f = rimCarriedFallback(true, false, "double", 2, 0.954f);
        say(f.flag && f.alternative == "S2" && !f.to_miss, "pure: the flag half, D2 at 0.954, offers S2");
    }
    {
        const RimFallback f = rimCarriedFallback(true, true, "double", 3, 0.994f);
        say(f.flag && f.alternative == "MISS" && !f.to_miss && !f.kept,
            "pure: the flag half, D3 at 0.994 unsupported, offers MISS and is not kept");
    }

    std::cout << "KEPT n=" << keptN << " MISS n=" << missN << std::endl;
    std::cout << "FAILURES: " << failures << std::endl;
    return failures == 0 ? 0 : 1;
}
