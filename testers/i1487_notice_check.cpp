// #1487: a dart whose wedge nobody measured is said to be one -- once per run, in words.
//
// #1346's fallback publishes the 20 at 0.5 where no camera can name a wedge, and that
// decision stands. What this asks is the sentence that goes with it:
//
//   1. an asserted dart is noticed, and a measured one, a ring-only one and a MISS are not;
//   2. the notice is said ONCE: a latch fed five asserted darts says one line (#1457);
//   3. the severity reuses #1501/#1449 rather than repeating them: INFO pointing back at
//      the start-up WARNING where no camera could be read at start, a WARNING of its own
//      where some camera could and this dart reached none of them;
//   4. ScoreResult carries the fact as a field, false by default.
//
// THE FALSIFIER, on this same binary: `OD_ASSERTED_WEDGE=unsaid` is the board before
// #1487 -- the asserted 20 publishes and nothing says so. The check reads the switch and
// asserts the OTHER answers under it, so neither run is green by refusing everything.
//
//   i1487_notice_check                            # this tree's decision
//   OD_ASSERTED_WEDGE=unsaid i1487_notice_check   # the same binary, before #1487
#include <iostream>
#include <string>

#include "score_processing.hpp"

using score_processing::AssertedWedgeNotice;
using score_processing::noticeAnAssertedWedge;

static int failures = 0;

static void say(bool ok, const std::string &what)
{
    std::cout << (ok ? "OK   " : "FAIL ") << what << std::endl;
    if (!ok)
    {
        failures++;
    }
}

static bool has(const std::string &s, const std::string &needle)
{
    return s.find(needle) != std::string::npos;
}

int main()
{
    const bool unsaid = score_processing::assertedWedgeGoesUnsaid();
    std::cout << "mode: " << (unsaid ? "OD_ASSERTED_WEDGE=unsaid (before #1487)" : "this tree")
              << std::endl;

    // 4. the field
    score_processing::ScoreResult blank;
    say(!blank.wedge_asserted, "a ScoreResult is not asserted until something says it is");

    // 1. only an asserted dart is noticed
    const AssertedWedgeNotice measured =
        noticeAnAssertedWedge(false, false, 3, 3, "T19", 0.9f, 1, unsaid);
    say(!measured.say, "a measured dart is never noticed");
    const AssertedWedgeNotice miss = noticeAnAssertedWedge(false, false, 0, 3, "MISS", 0.5f, -1, unsaid);
    say(!miss.say, "a no-winner MISS is never noticed");
    const AssertedWedgeNotice outer = noticeAnAssertedWedge(false, false, 0, 3, "OUTER", 0.7f, 0, unsaid);
    say(!outer.say, "a ring-only reading (OUTER) is never noticed");

    const AssertedWedgeNotice blind = noticeAnAssertedWedge(true, false, 0, 3, "S20", 0.5f, 0, unsaid);
    const AssertedWedgeNotice partial = noticeAnAssertedWedge(true, false, 1, 3, "D20", 0.5f, 2, unsaid);
    if (!unsaid)
    {
        say(blind.say, "the first asserted dart on a board no camera can read is noticed");
        say(!blind.warn, "... at INFO, because the start-up WARNING already said every dart would be");
        say(has(blind.sentence, "ASSERTED WEDGE: ") && has(blind.sentence, "S20 at 0.5") &&
                has(blind.sentence, "camera 0") && has(blind.sentence, "NO measured wedge"),
            "... naming the score, the confidence and the camera: " + blind.sentence);
        say(has(blind.sentence, "BOARD RECOGNITION") && has(blind.sentence, "OD_CAMERA_WEDGES"),
            "... and pointing back at #1501's line and its remedy rather than repeating them");
        say(has(blind.sentence, "Said once"), "... and saying it will not be said again");

        say(partial.say && partial.warn,
            "an asserted dart on a board where 1 of 3 cameras CAN be read is a WARNING: start said nothing");
        say(has(partial.sentence, "1 of 3 cameras can be read") && has(partial.sentence, "D20 at 0.5"),
            "... and it says how many could, and what published: " + partial.sentence);
    }
    else
    {
        say(!blind.say && !partial.say, "under the falsifier no asserted dart is noticed at all");
    }

    // 2. once, through the latch the caller keeps
    bool latch = false;
    int lines = 0;
    for (int dart = 0; dart < 5; dart++)
    {
        const AssertedWedgeNotice n = noticeAnAssertedWedge(true, latch, 0, 3, "S20", 0.5f, dart % 3, unsaid);
        if (n.say)
        {
            latch = true;
            lines++;
        }
    }
    std::cout << "     five asserted darts through the latch said " << lines << " line(s)" << std::endl;
    if (!unsaid)
    {
        say(lines == 1, "five asserted darts in one run are said ONCE, not once per dart (#1457)");
    }
    else
    {
        say(lines == 0, "five asserted darts under the falsifier are said not at all");
    }

    std::cout << (failures == 0 ? "i1487 check: PASS" : "i1487 check: FAIL") << " (" << failures
              << " failed)" << std::endl;
    return failures == 0 ? 0 : 1;
}
