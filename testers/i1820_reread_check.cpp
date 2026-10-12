// turnaus#1820: a takeout reconciled CLEAN with darts still in the board, and the board then
// published them again as they were pulled.
//
// Live on casual board 20, 2026-10-11 (the session's log, kept off GitHub; the lines this
// check needs are quoted below):
//
//   00:57:03.664  SCORE: S7  | Position: (568,266) | Camera: 0
//   00:57:07.614  SCORE: T10 | Position: (744,396) | Camera: 0
//   00:57:09.748  SCORE: S4  | Position: (750,299) | Camera: 0
//   00:57:20.905  CLEAN BY REVERSION: camera 1's cumulative board change fell from 17927 to 10468 px
//   00:57:20.912  CLEAN BY REVERSION: camera 2's ... fell from 13153 to 9461 px
//   00:57:20.920  CLEAN BY REVERSION: camera 3's ... fell from 12789 to 11124 px
//   00:57:20.920  CLEAN REFERENCE ADOPTED: ... camera(s) 1, 2, 3 re-based their clean reference
//   00:57:20.921  SCORE: END
//   00:57:23.331  SCORE: S7  | Position: (568,266) | Camera: 0     removed by the thrower
//   00:57:25.415  SCORE: S13 | Position: (743,305) | Camera: 0     removed by the thrower
//   00:58:20.132  CLEAN BY REVERSION: camera 1's ... fell from 16691 to 10461 px   (and 9464,
//                 11126: the 00:57:20 residue to within 10 px -- the S7 and the S4, absent now
//                 against the reference that held them)
//
// The rule (dart_processing.hpp, applied in geometry_detector.cpp under OD_REREAD_HOLD):
// a dart is held when the last END reconciled by reversion, nothing has published since,
// the board advances from CLEAN, the dart comes within rereadHorizonMs() of the END, and it
// is within rereadRadiusPx() of a dart of the closed visit through the same camera.
//
// Every assertion is labelled by what can turn it red:
//   pure:      the predicates' shape, with both figures passed explicitly
//   radius:    reads rereadRadiusPx() (the horizon passed explicitly, wide open)
//   horizon:   reads rereadHorizonMs() (the radius passed explicitly, wide open)
//   arming:    reads RereadMemory::ended's reversion clause, through a replayed visit
//   disarm:    reads RereadMemory::published's disarming, through a replayed visit
//   camera:    reads nearestSameCameraPx's same-camera clause
//   sequence:  a live visit replayed through the memory with the tree's constants -- reads
//              every figure, so a mutation of either constant may turn one red as well
// and each names its side: `live`/`held` (must be held) or the real dart that must stand.
// testers/i1820_check.sh mutates each and predicts exactly which assertions go red.
//
//   g++ -std=c++17 -O1 -I src -I src/utils -o i1820_reread_check testers/i1820_reread_check.cpp
//       $(pkg-config --cflags --libs opencv4)      (one command line)

#include <iostream>
#include <string>
#include <vector>

#include "detector/geometry/detection/dart_processing.hpp"

using namespace dart_processing;

static int failures = 0;

static void say(bool ok, const std::string &what)
{
    std::cout << (ok ? "OK   " : "FAIL ") << what << std::endl;
    if (!ok)
    {
        failures++;
    }
}

static const long long kWide = 1LL << 40; // a horizon nothing reaches
static const double kAnywhere = 1.0e6;    // a radius everything is inside

struct Publish
{
    std::string score;
    int camera;
    float x, y;
};

// A visit published, then an END, then darts after it, each `ms` after the END: replays the
// memory the way geometry_detector.cpp drives it, and answers which of the later darts were
// held. A held dart is not published, so the memory stays armed for the next one.
static std::vector<bool> replay(const std::vector<Publish> &visit, bool reversion_end,
                                const std::vector<std::pair<Publish, long long>> &after)
{
    RereadMemory m;
    const long long end_ms = 1000000;
    for (const Publish &p : visit)
        m.published(p.camera, cv::Point2f(p.x, p.y));
    m.ended(reversion_end, end_ms);
    std::vector<bool> held;
    for (const auto &d : after)
    {
        const cv::Point2f at(d.first.x, d.first.y);
        const bool h = isARereadAfterTakeout(m, DartBoardState::CLEAN, d.first.camera, at, end_ms + d.second);
        held.push_back(h);
        if (!h)
            m.published(d.first.camera, at);
    }
    return held;
}

int main()
{
    // ---- the live figures (board 20's log; camera indices as the SCORE line prints them) --
    const std::vector<Publish> v0057 = {{"S7", 0, 568, 266}, {"T10", 0, 744, 396}, {"S4", 0, 750, 299}};
    const Publish s7again{"S7", 0, 568, 266};   // 00:57:23.331, 2,410 ms after the END, removed
    const Publish s13{"S13", 0, 743, 305};      // 00:57:25.415, 4,494 ms, 9.2 px from the S4, removed
    const Publish s1next{"S1", 0, 874, 250};    // 00:58:01.941, the next visit's real first dart
    const std::vector<Publish> v0100 = {{"MISS", -1, -1, -1}, {"S16", 0, 626, 256}, {"T4", 0, 857, 339}};
    const Publish s16again{"S16", 0, 626, 256}; // 01:00:42.074, 2,174 ms after the 01:00:39 END
    const Publish t4again{"T4", 0, 857, 339};   // 01:00:44.192, 4,292 ms
    // The two repeats after a reversion END that were real throws (a late pull and the throw
    // in one window), which the thrower corrected rather than removed:
    const std::vector<Publish> v0055 = {{"T18", 0, 869, 311}, {"D15", 0, 649, 504}, {"T13", 0, 840, 354}};
    const Publish t13{"T13", 0, 837, 362};      // 00:56:04.942, 7,276 ms, 8.5 px; typed S7
    const std::vector<Publish> v0058 = {{"T19", 0, 522, 286}, {"D1", 1, 500, 455}, {"D20", 1, 667, 494}};
    const Publish d20{"D20", 1, 670, 491};      // 00:59:20.148, 7,840 ms, 4.2 px; corrected to a miss
    // The nearest real first dart inside the horizon of a reversion END:
    const std::vector<Publish> v0122 = {{"S20", 0, 895, 241}, {"S1", 0, 912, 260}, {"S20", 0, 880, 233}};
    const Publish s5{"S5", 0, 834, 212};        // 01:23:06.545, 4,041 ms after the 01:23:02 END, 50.6 px
    // The thrower's grouping after an END that reconciled under the CLEAN ceiling (no reversion):
    const std::vector<Publish> v0127 = {{"S19", 0, 572, 305}, {"S1", 0, 849, 259}, {"S15", 0, 696, 421}};
    const Publish s1group{"S1", 0, 850, 258};   // 01:27:40.218, 5,044 ms after the 01:27:35 END, 1.4 px

    // ---- pure: the shape, with both figures given ---------------------------------------
    {
        std::vector<PublishedPixel> closed(1);
        closed[0].camera = 0;
        closed[0].at = cv::Point2f(100, 100);
        say(nearestSameCameraPx(0, cv::Point2f(103, 104), closed) == 5.0, "pure: the distance is Euclidean in pixels");
        say(nearestSameCameraPx(-1, cv::Point2f(100, 100), closed) < 0.0, "pure: a MISS (camera -1) repeats nothing");
        say(nearestSameCameraPx(0, cv::Point2f(-1, -1), closed) < 0.0, "pure: a dart with no pixel repeats nothing");
        RereadMemory m;
        m.published(0, cv::Point2f(100, 100));
        m.ended(true, 0);
        say(isARereadAfterTakeout(m, DartBoardState::CLEAN, 0, cv::Point2f(105, 100), 999, 5.0, 1000),
            "pure: exactly at the radius, one ms inside the horizon, is held");
        say(!isARereadAfterTakeout(m, DartBoardState::CLEAN, 0, cv::Point2f(106, 100), 0, 5.0, 1000),
            "pure: just outside the radius is not");
        say(!isARereadAfterTakeout(m, DartBoardState::CLEAN, 0, cv::Point2f(100, 100), 1000, 5.0, 1000),
            "pure: at the horizon it is not");
        say(!isARereadAfterTakeout(m, DartBoardState::DART_1, 0, cv::Point2f(100, 100), 0, 5.0, 1000),
            "pure: a board not at CLEAN is never holding a re-read");
        RereadMemory empty;
        empty.ended(true, 0);
        say(!empty.armed, "pure: an END closing a visit that published nothing arms nothing");
    }

    // ---- radius: the figure, against the live S13 and the nearest real dart --------------
    {
        RereadMemory m;
        for (const Publish &p : v0057)
            m.published(p.camera, cv::Point2f(p.x, p.y));
        m.ended(true, 0);
        say(isARereadAfterTakeout(m, DartBoardState::CLEAN, s13.camera, cv::Point2f(s13.x, s13.y), 4494,
                                  rereadRadiusPx(), kWide),
            "radius: live -- the S13 9.2 px from the S4 it repeats is within the radius, so it is held");
        RereadMemory r;
        for (const Publish &p : v0122)
            r.published(p.camera, cv::Point2f(p.x, p.y));
        r.ended(true, 0);
        say(!isARereadAfterTakeout(r, DartBoardState::CLEAN, s5.camera, cv::Point2f(s5.x, s5.y), 4041,
                                   rereadRadiusPx(), kWide),
            "radius: real -- 01:23:06's S5, 50.6 px from the visit before, inside the horizon of a reversion END, stands");
    }

    // ---- horizon: the figure, against the latest pull and the earliest merged throw ------
    {
        RereadMemory m;
        for (const Publish &p : v0057)
            m.published(p.camera, cv::Point2f(p.x, p.y));
        m.ended(true, 0);
        say(isARereadAfterTakeout(m, DartBoardState::CLEAN, s13.camera, cv::Point2f(s13.x, s13.y), 4494, kAnywhere),
            "horizon: live -- the S13, 4,494 ms after the END (the latest pull), is inside the horizon");
        RereadMemory t;
        for (const Publish &p : v0055)
            t.published(p.camera, cv::Point2f(p.x, p.y));
        t.ended(true, 0);
        say(!isARereadAfterTakeout(t, DartBoardState::CLEAN, t13.camera, cv::Point2f(t13.x, t13.y), 7276, kAnywhere),
            "horizon: merged -- 00:56:04's T13, 7,276 ms after the END, a throw the thrower typed as S7, stands");
    }

    // ---- arming: only a reversion END arms ---------------------------------------------
    {
        const std::vector<bool> h = replay(v0127, false, {{s1group, 5044}});
        say(!h[0], "arming: grouping -- 01:27:40's S1, 1.4 px from the visit before but after an END under the "
                   "CLEAN ceiling, stands");
    }

    // ---- disarm: a published first dart ends the hold -----------------------------------
    {
        const std::vector<bool> h = replay(v0057, true, {{s1next, 1000}, {s7again, 2000}});
        say(!h[0] && !h[1], "disarm: sequence -- once a real dart has published, a later dart on an old dart's "
                            "pixel is a throw into the group, and stands");
    }

    // ---- camera: a pixel is a pixel in one camera ---------------------------------------
    {
        const std::vector<bool> h = replay(v0057, true, {{{"S7", 1, 568, 266}, 2410}});
        say(!h[0], "camera: another camera -- the same pixel through camera 2 is a different place, and stands");
    }

    // ---- sequence: the live visits, replayed with the tree's figures --------------------
    {
        const std::vector<bool> h = replay(v0057, true, {{s7again, 2410}, {s13, 4494}, {s1next, 41020}});
        say(h[0], "sequence: 00:57 S7 held -- 0.0 px, 2,410 ms after the reversion END");
        say(h[1], "sequence: 00:57 S13 held -- the second pull, 9.2 px, 4,494 ms: the S7's hold left the hold armed");
        say(!h[2], "sequence: 00:58 S1 stands -- the next visit's real first dart");
        const std::vector<bool> g = replay(v0100, true, {{s16again, 2174}, {t4again, 4292}});
        say(g[0], "sequence: 01:00 S16 held -- 0.0 px, 2,174 ms");
        say(g[1], "sequence: 01:00 T4 held -- 0.0 px, 4,292 ms");
        const std::vector<bool> t = replay(v0055, true, {{t13, 7276}});
        say(!t[0], "sequence: 00:56 T13 stands -- a throw with a late pull in its window, 7,276 ms");
        const std::vector<bool> d = replay(v0058, true, {{d20, 7840}});
        say(!d[0], "sequence: 00:59 D20 stands -- a throw (a miss) with a late pull in its window, 7,840 ms");
        const std::vector<bool> s = replay(v0122, true, {{s5, 4041}});
        say(!s[0], "sequence: 01:23 S5 stands -- 50.6 px, 4,041 ms after a reversion END");
    }

    std::cout << "FAILURES: " << failures << std::endl;
    return failures == 0 ? 0 : 1;
}
