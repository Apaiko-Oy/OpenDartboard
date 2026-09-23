// #1535: the re-report rule held to its own census, without a fixture.
//
//   i1535_identity_check fresh     the rule's decisions, pin unset
//   i1535_identity_check pinned    run under OD_TIP_IDENTITY=off: the pin is alive
//
// `isAReReportOfAnEarlierTip` is pure and inline in dart_processing.hpp (#1338's
// shape), so every claim below is the rule itself, not a reimplementation. The numbers
// are the fixture's: rig-20260918 visit 4's third dart, where camera 2 reported a "new"
// tip at (847,336) -- 2.2 px from the (848,338) it had already reported for the
// previous dart -- while the fresh diff's nearest point sat 82 px away (I1492TIP
// tipGap=82), and that false second witness earned an off-board dart S20@0.9.
//
// The pin claims run in a separate invocation because tipIdentityIsOff() memoises its
// first read, the way every od_fix pin does.
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "detector/geometry/detection/dart_processing.hpp"

static int failed = 0;
static void say(bool ok, const std::string &what)
{
    std::printf("%s%s\n", ok ? "OK   " : "FAIL ", what.c_str());
    if (!ok) failed++;
}

int main(int argc, char **argv)
{
    const std::string mode = argc > 1 ? argv[1] : "fresh";
    using dart_processing::isAReReportOfAnEarlierTip;

    if (mode == "pinned")
    {
        say(dart_processing::tipIdentityIsOff(),
            "under OD_TIP_IDENTITY=off the pin reads off -- the unguarded machinery is "
            "restorable on this binary, so before/after is two runs and not two builds");
        std::printf("CHECK_RC=%d\n", failed);
        return failed;
    }

    say(!dart_processing::tipIdentityIsOff(),
        "with the pin unset the rule is live (tipIdentityIsOff is false)");

    const cv::Point2f needle(847, 336);
    const std::vector<cv::Point2f> previous_dart = {cv::Point2f(848, 338)}; // 2.24 px away

    // The fixture's own numbers: visit 4, dart 3, camera 2.
    say(isAReReportOfAnEarlierTip(needle, 82.0, previous_dart),
        "the needle: a tip 2.2 px from one already reported this visit, with the fresh "
        "figure 82 px away, is a re-report (rig-20260918 v4 d3 cam 2)");

    // The legitimate neighbour: a dart really landing beside an earlier one has its tip
    // ON the fresh figure, so its gap is 0 whatever its distance to the earlier tip.
    say(!isAReReportOfAnEarlierTip(needle, 0.0, previous_dart),
        "a tip 2.2 px from an earlier one but ON the fresh figure (gap 0) is a real "
        "adjacent dart and is kept");

    // Fresh change elsewhere but nothing previously reported nearby: not a re-report.
    const std::vector<cv::Point2f> far_dart = {cv::Point2f(900, 400)}; // 83.2 px away
    say(!isAReReportOfAnEarlierTip(needle, 82.0, far_dart),
        "a tip 83 px from every earlier tip is not a re-report however far the fresh "
        "figure sits");

    // The first dart of a visit has nothing to re-report.
    say(!isAReReportOfAnEarlierTip(needle, 82.0, {}),
        "an empty visit memory never reads as a re-report");

    // The thresholds, pinned at their boundaries THROUGH THE DEFAULT ARGUMENTS, so a
    // moved default is a red claim and not a silent widening. near_px = 12:
    // (835,336) -> (847,336) is exactly 12 px; elsewhere_px = 40.
    const std::vector<cv::Point2f> at_twelve = {cv::Point2f(835, 336)};
    say(isAReReportOfAnEarlierTip(cv::Point2f(847, 336), 40.0, at_twelve),
        "at exactly near_px (12) and exactly elsewhere_px (40) the rule fires");
    say(!isAReReportOfAnEarlierTip(cv::Point2f(847.5, 336), 40.0, at_twelve),
        "12.5 px from the earlier tip is outside near_px and is kept");
    say(!isAReReportOfAnEarlierTip(cv::Point2f(847, 336), 39.9, at_twelve),
        "a fresh-figure gap of 39.9 px is under elsewhere_px and is kept");
    // The census's closest legitimate adjacency, verbatim: v5 d3 cam 1 landed 27.8 px
    // from its camera's earlier tip with its own gap at 13 px, and is kept twice over.
    const std::vector<cv::Point2f> real_neighbour = {cv::Point2f(484, 281)};
    say(!isAReReportOfAnEarlierTip(cv::Point2f(484 + 27.8f, 281), 13.0, real_neighbour),
        "the fixture's closest legitimate adjacency (27.8 px, gap 13) is kept");

    std::printf("CHECK_RC=%d\n", failed);
    return failed;
}
