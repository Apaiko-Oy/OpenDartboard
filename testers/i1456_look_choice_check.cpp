// #1456: which look a camera refused on its averaged frame seals, held as arithmetic.
//
// look_choice.hpp is the whole of the rule, pure, and this is what it must do:
//
//   1. BEST, NOT FIRST: of the looks that passed, the one with the highest R is sealed --
//      including when the first to pass is not the best, which is the issue.
//   2. THE TIE-BREAK, STATED: equal R goes to the EARLIEST look, whatever order the looks
//      are listed in. Nothing passed means nothing is sealed (-1).
//   3. THE WINDOW: every refused camera is looked at through #1445's twelve; past twelve
//      (only under #1605's budget of 31, the default since #1631) only a camera that
//      passed on none of the first twelve, and then to the end of the budget -- #1605's
//      guarantee that its longer budget never touches a camera the twelve admitted.
//      (#1631 relabelled the checks below "default"/"pin OD_LOOK_BUDGET=12"; the budgets
//      and expected values are unchanged.)
//   4. THE LOOK CENSUSES, replayed through the rule: rig-20260922 camera 1 from the dev
//      seek (looks 1-24 refused, 25+ calibrate; #1605's census) is set aside by the pin
//      OD_LOOK_BUDGET=12 and admitted by the default 31, sealing the best of looks 25-31, never a look the
//      census refused.
//   5. THE FALSIFIER, OD_LOOK_SEAL=first: the pinned rule seals the first look that passed
//      and stops looking at that camera, which is what every build before #1456 did.
//
// THE MUTATION (the issue's proof): make `best` return the first passing look (`return
// passed.empty() ? -1 : 0;`) and exactly the assertions that a later look outranks an
// earlier one go red, named: "1a", "1b", "2b" and "4b". The ties-to-earliest assertions
// ("2a", "2c") and the window's stay green, because the first look IS the earliest.
//
//   compiled by unit_check.sh (row 1456), no extra translation units.

#include <iostream>
#include <string>
#include <vector>

#include "detector/geometry/look_choice.hpp"

using look_choice::Passed;

static int failures = 0;
static void check(bool ok, const std::string &what)
{
    std::cout << (ok ? "OK   " : "FAIL ") << what << "\n";
    if (!ok)
        failures++;
}

// Replays a camera's census through the tree's loop: `passes(look)` says whether that look
// calibrated and `r(look)` its R. Returns the sealed look (0: none) and counts looks taken.
struct Replay
{
    int sealed = 0;
    int looked = 0;
    int passed = 0;
};
template <typename Passes, typename R>
static Replay replay(int selection, int budget, bool first_wins, Passes passes, R r)
{
    Replay out;
    std::vector<Passed> got;
    bool within = false;
    for (int look = 1; look <= budget; look++)
    {
        const bool at = first_wins ? look_choice::looksAtFirstWins(look, budget, !got.empty())
                                   : look_choice::looksAt(look, selection, budget, within);
        if (!at)
            continue;
        out.looked++;
        if (!passes(look))
            continue;
        got.push_back({look, r(look)});
        if (look <= selection)
            within = true;
    }
    const int k = first_wins ? look_choice::firstWins(got) : look_choice::best(got);
    out.sealed = k < 0 ? 0 : got[k].look;
    out.passed = (int)got.size();
    return out;
}

int main()
{
    // ---- 1. best, not first ------------------------------------------------------------
    {
        // The issue's own shape: looks 2 and 3 refused, and R rising over the looks that pass.
        std::vector<Passed> p = {{4, 0.71}, {7, 0.93}, {9, 0.88}};
        const int k = look_choice::best(p);
        check(k == 1 && p[k].look == 7, "1a the highest R (look 7, 0.93) is sealed, not the first to pass (look 4, 0.71)");
        std::vector<Passed> q = {{3, 0.927525}, {5, 0.935095}};
        check(look_choice::best(q) == 1, "1b a later look that measured strictly better wins (0.935095 over 0.927525)");
        std::vector<Passed> one = {{11, 0.61}};
        check(look_choice::best(one) == 0, "1c one passing look is the look sealed, however near the gate");
    }

    // ---- 2. the tie-break -----------------------------------------------------------------
    {
        std::vector<Passed> tie = {{3, 0.9}, {8, 0.9}};
        check(look_choice::best(tie) == 0, "2a equal R: the earliest look (3) is sealed, not look 8");
        std::vector<Passed> rev = {{8, 0.9}, {3, 0.9}, {5, 0.5}};
        check(look_choice::best(rev) == 1, "2b equal R listed out of order: still the earliest look (3), by index not by position");
        std::vector<Passed> zero = {{2, 0.0}, {6, 0.0}};
        check(look_choice::best(zero) == 0, "2c no R to rank (OD_WIRE_MODEL=count reads 0 for every look): the earliest, i.e. the old rule's pick");
        check(look_choice::best({}) == -1, "2d nothing passed: nothing is sealed");
    }

    // ---- 3. the window ---------------------------------------------------------------------
    {
        bool all = true;
        for (int look = 1; look <= 12; look++)
            all = all && look_choice::looksAt(look, 12, 12, true) && look_choice::looksAt(look, 12, 31, true);
        check(all, "3a every look 1..12 is taken, passed or not, under either budget");
        check(!look_choice::looksAt(13, 12, 12, false), "3b the OD_LOOK_BUDGET=12 pin ends at 12 for a camera still refused");
        check(!look_choice::looksAt(13, 12, 31, true), "3c the default 31's look 13 is NOT spent on a camera that passed within 12 (#1605 E)");
        check(look_choice::looksAt(13, 12, 31, false) && look_choice::looksAt(31, 12, 31, false),
              "3d the default 31's looks 13..31 ARE spent on a camera that passed on none of the first 12");
        check(!look_choice::looksAt(32, 12, 31, false) && !look_choice::looksAt(0, 12, 31, false),
              "3e nothing outside 1..budget");
    }

    // ---- 4. the look censuses through the rule ---------------------------------------------
    {
        // rig-20260922 camera 1, dev seek: looks 1-24 refused, 25+ calibrate (#1605's census).
        // R on the passing looks is unknown here and immaterial to WHICH looks are candidates;
        // a made-up profile peaking at 28 checks that the seal can be any of them.
        auto passes = [](int look) { return look >= 25; };
        auto r = [](int look) { return 0.90 - 0.001 * (look - 28) * (look - 28); };
        const Replay d = replay(12, 12, false, passes, r);
        check(d.sealed == 0 && d.looked == 12, "4a pin OD_LOOK_BUDGET=12: camera 1 is looked at 12 times and set aside, the default before #1631");
        const Replay o = replay(12, 31, false, passes, r);
        check(o.sealed == 28 && o.passed == 7 && o.looked == 31,
              "4b the default 31: looks 25..31 pass (7) and the best of them (28) is sealed, not look 25");
        const Replay of = replay(12, 31, true, passes, r);
        check(of.sealed == 25 && of.looked == 25, "4c the same under OD_LOOK_SEAL=first: look 25, and looking stops there (#1605's run)");

        // rig-20260922 camera 2 at the opening passes from look 3 (and the dev window's from 9).
        auto from3 = [](int look) { return look >= 3; };
        auto flat = [](int) { return 0.93; };
        const Replay c2 = replay(12, 31, false, from3, flat);
        check(c2.sealed == 3 && c2.looked == 12 && c2.passed == 10,
              "4d a camera passing from look 3 at a flat R is looked at 12 times, not 31, and seals look 3 on the tie");
        const Replay c2d = replay(12, 12, false, from3, flat);
        check(c2d.sealed == c2.sealed && c2d.looked == c2.looked,
              "4e ... identically under the default and the OD_LOOK_BUDGET=12 pin (#1605 E holds under #1456)");
    }

    // ---- 5. the falsifier ----------------------------------------------------------------------
    {
        std::vector<Passed> p = {{4, 0.71}, {7, 0.93}};
        check(look_choice::firstWins(p) == 0 && look_choice::firstWins({}) == -1,
              "5a OD_LOOK_SEAL=first seals the first look that passed");
        check(look_choice::looksAtFirstWins(4, 12, false) && !look_choice::looksAtFirstWins(5, 12, true),
              "5b ... and stops looking at that camera once it has passed");
    }

    std::cout << "RESULT: " << failures << " failure(s)\n";
    return failures == 0 ? 0 : 1;
}
