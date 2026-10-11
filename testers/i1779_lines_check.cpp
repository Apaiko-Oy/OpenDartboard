// turnaus#1779: a geometric publish from fewer lines than cameras says, at INFO, which
// camera offered no line and why -- in the words the DEGRADED story already ends on.
//
// Live on 2026-10-10 16:17:44 (build 2b56b48) a lone dart in S10 solved from cameras 1 and
// 3 at 24.6 deg and published S6 flagged, and nothing at INFO said why camera 2 offered no
// line. `missingLinesAccount` is that line; this holds it on hand-built solutions (it is
// pure over EntrySolution, so no board has to be planted) and holds the TOO-FEW story,
// whose list it now shares, byte-for-byte through a real `solveEntry`.
//
// PREDICTIONS, STATED FIRST:
//   1. live:      the live shape -- three offered, camera 2 refused as not a shaft, two lines
//                 solved -- reads exactly
//                 "LINES: 2 of 3 cameras' lines solved this dart (cam 2: no usable axis: not a
//                 shaft: extent 365.0 px over median width 132.5 px is 2.75, under the 3.0
//                 elongation gate (...))".
//   2. solver:    a line the solver EXCLUDED (usable, then refused as inconsistent) is named
//                 by its camera and its exclusion, and the cameras whose lines solved are not.
//   3. three:     a three-line solve prints nothing.
//   4. unsolved:  a TOO-FEW refusal prints nothing (the DEGRADED line already says it).
//   5. too-few:   the TOO-FEW story a real solveEntry writes is byte-for-byte what it was
//                 before the list was hoisted out of its lambda.
//
//   compiled and mutated by i1779_check.sh, with wire_model.cpp for unit_check.sh row
//   1512's reason (entry_intersection.hpp's board fit calls into it at link time).

#include <iostream>
#include <string>
#include <vector>

#include "detector/geometry/detection/entry_intersection.hpp"

using namespace entry_intersection;

static int failures = 0;

static void say(bool ok, const std::string &label, const std::string &what)
{
    std::cout << (ok ? "OK   " : "FAIL ") << label << ": " << what << std::endl;
    if (!ok)
    {
        failures++;
    }
}

static const std::string kNotAShaft =
    "no usable axis: not a shaft: extent 365.0 px over median width 132.5 px is 2.75, under "
    "the 3.0 elongation gate (a near-end-on dart or a blob, not a line of evidence)";
static const std::string kInconsistent =
    "inconsistent with the other cameras: residual 9.1 mm (3.2 sigma)";

static Constraint line(int camera)
{
    Constraint c;
    c.camera = camera;
    c.offered = true;
    c.usable = true;
    return c;
}

static Constraint refused(int camera, const std::string &why)
{
    Constraint c;
    c.camera = camera;
    c.offered = true;
    c.exclusion = why;
    return c;
}

static EntrySolution solved(std::vector<Constraint> cons)
{
    EntrySolution s;
    s.outcome = Outcome::UncertainAcrossWire;
    s.solved = true;
    s.offeredConstraints = (int)cons.size();
    for (const Constraint &c : cons)
    {
        s.usableConstraints += (c.usable && !c.excluded) ? 1 : 0;
    }
    s.constraints = cons;
    return s;
}

int main()
{
    // ---- 1. the live shape --------------------------------------------------------------
    {
        const std::string got =
            missingLinesAccount(solved({line(0), refused(1, kNotAShaft), line(2)}));
        const std::string want = "LINES: 2 of 3 cameras' lines solved this dart (cam 2: " + kNotAShaft + ")";
        say(got == want, "live", got);
    }
    // ---- 2. a line the solver excluded --------------------------------------------------
    {
        Constraint liar = line(2);
        liar.excluded = true;
        liar.exclusion = kInconsistent;
        const std::string got = missingLinesAccount(solved({line(0), line(1), liar}));
        say(got == "LINES: 2 of 3 cameras' lines solved this dart (cam 3: " + kInconsistent + ")" &&
                got.find("cam 1") == std::string::npos && got.find("cam 2") == std::string::npos,
            "solver", got);
    }
    // ---- 3. a three-line solve ----------------------------------------------------------
    {
        const std::string got = missingLinesAccount(solved({line(0), line(1), line(2)}));
        say(got.empty(), "three", "a three-line solve prints nothing (got \"" + got + "\")");
    }
    // ---- 4. an unsolved dart ------------------------------------------------------------
    {
        EntrySolution s = solved({line(0), refused(1, kNotAShaft), refused(2, kNotAShaft)});
        s.solved = false;
        s.outcome = Outcome::TooFewConstraints;
        const std::string got = missingLinesAccount(s);
        say(got.empty(), "unsolved", "a TOO-FEW refusal prints nothing here (got \"" + got + "\")");
    }
    // ---- 5. the TOO-FEW story, through a real solve -------------------------------------
    {
        std::vector<CameraEvidence> ev(3);
        for (int i = 0; i < 3; i++)
        {
            ev[i].camera = i;
        }
        const EntrySolution s = solveEntry(board_model::profileFromSpec(perspective_processing::DartboardSpec()), ev);
        const std::string fit = "no accepted board fit, so nothing this camera saw can be placed on the board";
        const std::string want = "no entry: 0 usable constraint(s) of 3 cameras, and one line is anywhere along "
                                 "itself (cam 1: " + fit + "; cam 2: " + fit + "; cam 3: " + fit + ")";
        say(s.story == want && missingLinesAccount(s).empty(), "too-few", s.story);
    }

    std::cout << (failures == 0 ? "ALL OK" : "FAILURES: " + std::to_string(failures)) << std::endl;
    return failures == 0 ? 0 : 1;
}
