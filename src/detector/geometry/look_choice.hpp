#pragma once

#include <vector>

// #1456: WHICH LOOK A REFUSED CAMERA SEALS. Two rules, pure, and the whole of the decision.
//
// THE DEFECT. #1445's retry adopted the FIRST look on which a refused camera calibrated.
// That made the retry an admission rule and, silently, a selection rule too: the geometry
// the board then sealed was whichever frame first cleared the gate, which nobody chose.
// Filed when the gate was #1442's count of twenty; since #1467 it is the twenty-fold fit's
// coherence R >= 0.60, and the sealed look was still the first to clear it.
//
// THE DECISION (maintainer, 2026-09-24): seal the best look of the budget. R is the first
// measure that ranks one look against another, so the retry now looks at a refused camera
// for the whole budget, keeps every look that passes, and seals the one with the highest
// R. A tie goes to the EARLIEST look: a later look is only preferred when it measured
// strictly better, so "best" never depends on the order two equal readings arrived in.
// Cost accepted there: up to twelve fits, paid only by a camera the averaged frame refused.
//
// WHICH BUDGET IS "THE BUDGET". #1445's twelve is the selection window. #1605's 31 looks
// (the default since #1631; OD_LOOK_BUDGET=12 pins the old twelve) exist for ADMISSION --
// to outlast a dart standing in the board -- and are spent past look twelve only on a
// camera that has not passed on any of the first twelve, which is #1605's own guarantee
// (1605-looks section E: a start whose cameras all passed within twelve is byte-identical
// under the default and the pin). Such a camera is then looked at to the end of the
// budget and seals the best of the looks
// that passed there. So best-of-budget holds over whichever budget is active, and the
// longer budget still never touches a camera the shorter one admitted.
namespace look_choice
{
    // One look on which the camera calibrated: its 1-based index in the budget and the R
    // its wire fit measured.
    struct Passed
    {
        int look = 0;
        double r = 0.0;
    };

    // The position in `passed` of the look to seal: the highest R, a tie broken by the
    // earliest look. -1 when nothing passed. `passed` is in the order the looks were taken,
    // but the rule does not rely on that -- the tie-break compares look indices.
    inline int best(const std::vector<Passed> &passed)
    {
        int chosen = -1;
        for (int i = 0; i < (int)passed.size(); i++)
        {
            if (chosen < 0 || passed[i].r > passed[chosen].r ||
                (passed[i].r == passed[chosen].r && passed[i].look < passed[chosen].look))
            {
                chosen = i;
            }
        }
        return chosen;
    }

    // Whether a camera the averaged frame refused is looked at on look `look` (1-based).
    // `selection` is #1445's twelve, `budget` the active budget (#1605's 31 by default, twelve under the pin),
    // `passed_within_selection` whether this camera already passed on a look <= selection.
    // Every camera is looked at through the selection window; past it, only one that has
    // not passed yet, and then to the end of the budget.
    inline bool looksAt(int look, int selection, int budget, bool passed_within_selection)
    {
        if (look < 1 || look > budget)
        {
            return false;
        }
        if (look <= selection)
        {
            return true;
        }
        return !passed_within_selection;
    }

    // THE FALSIFIER. OD_LOOK_SEAL=first puts back the rule every build before #1456 had:
    // a camera stops being looked at on the first look that passes, and that look is the
    // one sealed. So the two rules can be run on ONE binary and the difference is this
    // switch and nothing else (#1340's shape). The pinned rule, as pure functions, so the
    // check can hold both:
    inline bool looksAtFirstWins(int look, int budget, bool passed_yet)
    {
        return look >= 1 && look <= budget && !passed_yet;
    }
    inline int firstWins(const std::vector<Passed> &passed)
    {
        return passed.empty() ? -1 : 0;
    }
}
