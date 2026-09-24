// #1555: WHICH PATH PUBLISHES, held as a decision rather than as a branch.
//
// Two paths can name a dart -- chooseScore's string vote and entry_intersection's solved
// board-plane entry -- and the maintainer's rule is that the census decides which one the
// board publishes. `score_processing::decidePublishedPath` is the whole of the rule and
// this file is what holds it: pure, over primitives, with no detector, no footage and no
// container of its own (#1338's reason, and unit_check.sh's link note -- the decision
// deliberately does not take an EntrySolution, so score_processing.hpp stays includable
// by a check that links nothing).
//
// What is asserted here, in the order it matters:
//
//   1. THE RULE. Geometry publishes where it solved; the vote publishes where it did not;
//      and a run where geometry is not the published path never reports a degradation at
//      all, because a vote-only board is not a board whose geometry failed.
//   2. #1512'S CONTRACT, which is the half a reader can be misled by: a fallback says
//      DEGRADED, names the solver's own refusal, and says in words that what is being
//      published is one camera's reading and NOT a triangulated position. A fallback that
//      reads like a solve is the whole failure this contract exists to prevent.
//   3. THE PIN. `OD_SCORE_PATH=vote` restores the losing path on the same binary, and it
//      may move the wiring and the account's wording and NOTHING pure -- every `pure:`
//      assertion below holds identically with the pin set, which is what i1552's third
//      run measures one file over.
//   4. THE CONFIDENCES, whose meaning per path is the thing #1489 refused a fourth number
//      for: the two must differ, or a published float is saying nothing.
//
// Modes: `tree` (no pin) and `pinned` (run under OD_SCORE_PATH=vote). Asking the pinned
// binary the tree's questions is the mutation proof, and testers/i1555_check.sh states
// its predicted failure count before running it.
//
//   g++ -std=c++17 -I src -I src/utils -o publish_check
//       testers/i1555_publish_check.cpp $(pkg-config --cflags --libs opencv4)

#include <iostream>
#include <string>

#include "detector/geometry/detection/score_processing.hpp"

using score_processing::decidePublishedPath;
using score_processing::geometricConfidence;
using score_processing::kGeometryWonTheCensus;
using score_processing::PublishDecision;
using score_processing::publishedPathIsGeometry;
using score_processing::ScorePath;
using score_processing::voteIsPinned;

static int failures = 0;

static void say(bool ok, const std::string &what)
{
    std::cout << (ok ? "OK   " : "FAIL ") << what << std::endl;
    if (!ok)
    {
        failures++;
    }
}

static bool has(const std::string &haystack, const std::string &needle)
{
    return haystack.find(needle) != std::string::npos;
}

// The solver's own words, as the caller hands them over: the outcome word and the story.
static const char *kTooFew = "TOO-FEW-CONSTRAINTS";
static const char *kTooFewStory =
    "no entry: 1 usable constraint(s) of 3 cameras, and one line is anywhere along "
    "itself (cam 2: no usable axis: not straight)";
static const char *kNearParallel = "NEAR-PARALLEL";
static const char *kInconsistent = "INCONSISTENT";
static const char *kSolved = "SOLVED";
static const char *kWireUncertain = "WIRE-UNCERTAIN";

int main(int argc, char **argv)
{
    const std::string mode = argc > 1 ? argv[1] : "tree";
    if (mode != "tree" && mode != "pinned")
    {
        std::cerr << "usage: i1555_publish_check [tree|pinned]" << std::endl;
        return 2;
    }
    const bool expect_pin = mode == "pinned";

    std::cout << "CENSUS-CONSTANT geometryWon=" << (kGeometryWonTheCensus ? 1 : 0)
              << " mode=" << mode << std::endl;

    // ---- 1. the rule ------------------------------------------------------------------
    {
        const PublishDecision d = decidePublishedPath(true, true, kSolved, "");
        say(d.path == ScorePath::Geometry, "pure: a solved entry publishes the geometric score");
        say(d.geometry_asked && d.geometry_solved,
            "pure: and the decision records that the solver was asked and answered");
        say(d.fallback_reason.empty(),
            "pure: a geometric publish reports no fallback reason -- nothing fell back");
        say(has(d.account, "geometric entry publishes") && has(d.account, kSolved),
            "pure: the account names the path and the solver's outcome");
    }
    {
        const PublishDecision d = decidePublishedPath(true, true, kWireUncertain, "");
        say(d.path == ScorePath::Geometry,
            "pure: WIRE-UNCERTAIN is a solved position, not a refusal, and publishes");
        say(has(d.account, kWireUncertain),
            "pure: and the account carries the reservation by name");
    }
    {
        const PublishDecision d = decidePublishedPath(true, false, kTooFew, kTooFewStory);
        say(d.path == ScorePath::Vote, "pure: a refused solve falls back to the string vote");
        say(d.geometry_asked && !d.geometry_solved,
            "pure: the decision records that the solver was asked and refused");
        say(!d.fallback_reason.empty() && has(d.fallback_reason, kTooFew) &&
                has(d.fallback_reason, "usable constraint"),
            "pure: the fallback reason is the solver's own outcome and its own story");
    }

    // ---- 2. #1512's contract: a fallback may never read as a solve ----------------------
    {
        const PublishDecision d = decidePublishedPath(true, false, kTooFew, kTooFewStory);
        say(has(d.account, "DEGRADED"),
            "pure: a fallback labels itself DEGRADED in the line a reader sees");
        say(has(d.account, "not a triangulated position"),
            "pure: and says in words that this is not a triangulated position (#1512)");
        say(has(d.account, "one camera's reading"),
            "pure: and says what it IS -- one camera's reading");
        say(!has(d.account, "geometric entry publishes"),
            "pure: a fallback account never claims the geometric entry published");
    }
    {
        // Every refusal the solver has, not just the one that is common: NEAR-PARALLEL
        // and INCONSISTENT are solves that produced no trustworthy entry either, and a
        // contract that only covers the frequent refusal is not a contract.
        const PublishDecision par = decidePublishedPath(true, false, kNearParallel,
                                                        "the best crossing angle is 8.1 deg");
        const PublishDecision inc = decidePublishedPath(true, false, kInconsistent,
                                                        "the joint residuals read chi2 19.4");
        say(par.path == ScorePath::Vote && has(par.account, "DEGRADED") &&
                has(par.account, kNearParallel),
            "pure: NEAR-PARALLEL falls back and is labelled by its own name");
        say(inc.path == ScorePath::Vote && has(inc.account, "DEGRADED") &&
                has(inc.account, kInconsistent),
            "pure: INCONSISTENT falls back and is labelled by its own name");
    }
    {
        // The asymmetry that is easy to get wrong: a board whose published path IS the
        // vote has not degraded. `degraded` must mean "the geometry was asked and
        // refused", so a vote-only run cannot report a fallback on every dart of its life.
        const PublishDecision d = decidePublishedPath(false, false, kTooFew, kTooFewStory);
        say(d.path == ScorePath::Vote, "pure: with geometry not the published path, the vote publishes");
        say(!d.geometry_asked,
            "pure: and the solver is recorded as not asked, so nothing reads as a refusal");
        say(d.fallback_reason.empty() && !has(d.account, "DEGRADED"),
            "pure: a vote-only board never reports a degradation -- nothing fell back");
    }
    {
        // The same asymmetry from the other side: a SOLVED entry on a vote-only board
        // changes nothing. This is the assertion that would go red if the flag were ever
        // read as a hint rather than as the decision.
        const PublishDecision d = decidePublishedPath(false, true, kSolved, "");
        say(d.path == ScorePath::Vote && !d.geometry_solved,
            "pure: a solved entry cannot publish on a board whose path is the vote");
    }

    // ---- 3. the pin, and what it may move ----------------------------------------------
    say(voteIsPinned() == expect_pin,
        std::string("the pin reads as ") + (expect_pin ? "SET" : "unset") +
            " in this run, which is what this mode was started for");
    say(publishedPathIsGeometry() == (kGeometryWonTheCensus && !expect_pin),
        "the published path is the census winner unless OD_SCORE_PATH=vote says otherwise");
    {
        const PublishDecision d = decidePublishedPath(false, false, kSolved, "");
        say(has(d.account, "the string vote publishes"),
            "pure: the vote-only account always says the vote published");
        say(has(d.account, "OD_SCORE_PATH=vote is pinned") == expect_pin,
            std::string("the vote-only account names the pin exactly when the pin is ") +
                (expect_pin ? "set" : "unset"));
    }

    // ---- 4. the confidences -------------------------------------------------------------
    say(geometricConfidence(false) > geometricConfidence(true),
        "pure: a solve whose sigma clears every wire is worth more than one whose sigma reaches one");
    say(geometricConfidence(false) == 0.9f && geometricConfidence(true) == 0.7f,
        "pure: and the two numbers are the vocabulary that was already published (#1489)");

    std::cout << (failures ? "FAILURES: " : "ALL OK: ") << failures << std::endl;
    return failures ? 1 : 0;
}
