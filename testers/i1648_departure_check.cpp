// #1648: the re-reported-departure rule held to its own numbers, without a fixture.
//
// `readsAsReReportedDeparture` is pure and inline in dart_processing.hpp, so every claim
// below is the rule itself. The numbers are rig-20260922's opening window under #1646's
// OD_SETTLE_EXPOSURE=hold: window 2 (the visit-1 takeout), where camera 2's only new tip
// (718,212) sat 1 px from the S16 tip it had already reported, and its cumulative board
// figure went 1373 -> 1208 px (a fall of 165, under its 215 px CLEAN ceiling, so #1518's
// readsAsReversion cannot fire).
#include <cstdio>
#include <string>

#include "detector/geometry/detection/dart_processing.hpp"

static int failed = 0;
static void say(bool ok, const std::string &what)
{
    std::printf("%s%s\n", ok ? "OK   " : "FAIL ", what.c_str());
    if (!ok) failed++;
}

int main()
{
    using dart_processing::readsAsReReportedDeparture;
    using dart_processing::readsAsReversion;

    say(!readsAsReversion(1373, 1208, 215),
        "camera 2's 1373 -> 1208 px is not a #1518 reversion (fall 165 < ceiling 215): the gap this rule fills");
    say(readsAsReReportedDeparture(true, 1373, 1208),
        "a re-report with the cumulative figure falling 1373 -> 1208 reads as a departure");
    say(!readsAsReReportedDeparture(true, 1208, 1373),
        "a re-report while the figure RISES (a dart arriving) does not");
    say(!readsAsReReportedDeparture(true, 1373, 1373),
        "nor while it holds still");
    say(!readsAsReReportedDeparture(false, 1373, 1208),
        "a fall without a re-report (the thrower's shadow on one camera) does not");
    say(!readsAsReReportedDeparture(true, -1, 1208),
        "no completed window to fall from is no verdict");
    std::printf("CHECK_RC=%d\n", failed);
    return failed;
}
