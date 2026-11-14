// turnaus#1991: window_keep.hpp -- every published dart's window on disk, bounded, oldest
// first -- held without a detector.
//
// The pictures are a real settled-size frame off mocks/rig-20260918 (cam_1's first frame,
// grey) with a line drawn per dart, so every picture differs from every other and a
// swapped or stale one is caught byte for byte. A "visit" is what dart_processing really
// hands over: three darts on one clean reference, dart N's working background being dart
// N-1's settled picture.
//
// PREDICTIONS, stated before the run:
//   1. the switch off: the same deposits and commits write no file at all.
//   2. one visit, three cameras: 12 pictures and 3 accounts on disk, not 27 -- the clean
//      reference once per camera, each settled picture once though the next dart names it
//      again as its working background. Every picture decodes to the exact Mat that went
//      in, under the role (settled / clean / fresh) it went in as; the scalars are in the
//      account as given.
//   3. a deposit whose window the published dart does not name writes nothing.
//   4. bounded at two and a half visits and fed six: the directory never ends a dart over
//      the bound, the darts on disk are always the NEWEST ones in commit order, and EVERY
//      ACCOUNT ON DISK HAS EVERY PICTURE IT NAMES -- asked after every single dart.
//   5. a restart on the same directory adopts what is there (the ledger's bytes are the
//      directory's) and goes on evicting oldest first with the same guarantee.
//   6. an OD_ switch whose name says it holds a credential is written `(withheld)`.
//
// The two mutations are made by i1991_check.sh on a copy of the header and each must turn
// check 4 red BY NAME: the account evicted after its pictures (a `settled` picture goes
// missing), and a re-used picture not re-dated (a `clean` or `fresh` one does).
//
//   g++ -std=c++17 -I src -I src/utils testers/i1991_keep_check.cpp -o keep_check
//       $(pkg-config --cflags --libs opencv4) -lpthread     (one command line)

#include <cstdlib>
#include <unistd.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <opencv2/opencv.hpp>

#include "detector/geometry/detection/window_keep.hpp"
#include "logging.hpp"

namespace fs = std::filesystem;

static int checks = 0;
static std::vector<std::string> failures;

static bool say(bool ok, const std::string &what)
{
    checks++;
    std::cout << (ok ? "PASS " : "FAIL ") << what << std::endl;
    if (!ok)
    {
        failures.push_back(what);
    }
    return ok;
}

static cv::Mat baseFrame(std::string &source)
{
    cv::Mat frame;
    cv::VideoCapture cap("mocks/rig-20260918/cam_1.mp4");
    if (cap.isOpened())
    {
        cap >> frame;
    }
    if (frame.empty())
    {
        source = "drawn (the clip could not be opened)";
        frame = cv::Mat(720, 1280, CV_8UC3, cv::Scalar(40, 40, 40));
        cv::circle(frame, cv::Point(640, 360), 300, cv::Scalar(200, 200, 200), 8);
    }
    else
    {
        source = "mocks/rig-20260918/cam_1.mp4 frame 0";
    }
    cv::Mat grey;
    cv::cvtColor(frame, grey, cv::COLOR_BGR2GRAY);
    return grey;
}

static std::string slurp(const fs::path &p)
{
    std::ifstream f(p, std::ios::binary);
    std::stringstream s;
    s << f.rdbuf();
    return s.str();
}

static uint64_t diskBytes(const std::string &dir)
{
    uint64_t total = 0;
    for (const char *sub : {"pics", "darts"})
    {
        std::error_code ec;
        for (const auto &e : fs::directory_iterator(fs::path(dir) / sub, ec))
        {
            total += (uint64_t)e.file_size(ec);
        }
    }
    return total;
}

static size_t countFiles(const std::string &dir, const char *sub)
{
    size_t n = 0;
    std::error_code ec;
    for (const auto &e : fs::directory_iterator(fs::path(dir) / sub, ec))
    {
        (void)e;
        n++;
    }
    return n;
}

/** The names after one role's key, in camera order. */
static std::vector<std::string> named(const std::string &json, const std::string &role)
{
    std::vector<std::string> out;
    const std::string key = "\"" + role + "\":\"";
    size_t at = 0;
    while ((at = json.find(key, at)) != std::string::npos)
    {
        at += key.size();
        out.push_back(json.substr(at, 16));
    }
    return out;
}

/** Empty when every account on disk has every picture it names; otherwise the first that does not, by role. */
static std::string brokenAccount(const std::string &dir)
{
    std::error_code ec;
    for (const auto &e : fs::directory_iterator(fs::path(dir) / "darts", ec))
    {
        const std::string json = slurp(e.path());
        for (const char *role : {"settled", "clean", "fresh"})
        {
            for (const std::string &name : named(json, role))
            {
                if (!fs::exists(fs::path(dir) / "pics" / (name + ".png")))
                {
                    return e.path().filename().string() + " names the " + role + " picture " + name + " and it is gone";
                }
            }
        }
    }
    return "";
}

struct Visit
{
    std::vector<cv::Mat> clean;                // per camera
    std::vector<std::vector<cv::Mat>> settled; // [dart][camera]
};

static Visit makeVisit(const cv::Mat &base, int v, int cameras)
{
    Visit out;
    for (int c = 0; c < cameras; c++)
    {
        cv::Mat clean = base.clone();
        cv::putText(clean, "v" + std::to_string(v) + "c" + std::to_string(c), cv::Point(40 + 7 * v, 60 + 30 * c),
                    cv::FONT_HERSHEY_SIMPLEX, 1.0, cv::Scalar(255), 2);
        out.clean.push_back(clean);
    }
    for (int d = 0; d < 3; d++)
    {
        std::vector<cv::Mat> row;
        for (int c = 0; c < cameras; c++)
        {
            cv::Mat s = d == 0 ? out.clean[c].clone() : out.settled[d - 1][c].clone();
            cv::line(s, cv::Point(300 + 60 * d + 11 * v, 200 + 40 * c), cv::Point(520 + 60 * d, 420 + 13 * v), cv::Scalar(230), 5);
            row.push_back(s);
        }
        out.settled.push_back(row);
    }
    return out;
}

static window_keep::Window windowOf(const Visit &visit, int d, long ordinal)
{
    window_keep::Window w;
    w.window = ordinal;
    w.board_state_before = d;
    for (size_t c = 0; c < visit.clean.size(); c++)
    {
        window_keep::Camera k;
        k.settled = visit.settled[d][c];
        k.clean = visit.clean[c];
        k.fresh = d == 0 ? cv::Mat() : visit.settled[d - 1][c];
        k.previous_state = d;
        k.previous_board_change = 1000 * d + (int)c;
        k.frames = 9;
        k.board_known = true;
        k.board = cv::RotatedRect(cv::Point2f(640.5f, 360.25f), cv::Size2f(500, 420), 12.5f);
        for (int t = 0; t < d; t++)
        {
            k.reported_tips.push_back(cv::Point2f(100.5f + t, 200.0f + (float)c));
        }
        w.cameras.push_back(k);
    }
    return w;
}

static bool samePicture(const std::string &dir, const std::string &name, const cv::Mat &want)
{
    const cv::Mat got = cv::imread((fs::path(dir) / "pics" / (name + ".png")).string(), cv::IMREAD_UNCHANGED);
    return !got.empty() && got.size() == want.size() && got.type() == want.type() && cv::countNonZero(got != want) == 0;
}

int main()
{
    std::string source;
    const cv::Mat base = baseFrame(source);
    std::cout << "settled frame: " << source << ", " << base.cols << "x" << base.rows << std::endl;
    const std::string root = (fs::temp_directory_path() / ("i1991-keep-" + std::to_string((long)getpid()))).string();
    fs::remove_all(root);
    fs::create_directories(root);
    const int cameras = 3;

    // ---- 1. off ------------------------------------------------------------------------
    {
        window_keep::configure("", 0);
        const Visit v = makeVisit(base, 0, cameras);
        bool any = false;
        for (int d = 0; d < 3; d++)
        {
            window_keep::deposit(windowOf(v, d, d + 1));
            any = window_keep::commit("OFF" + std::to_string(d), d + 1) || any;
        }
        window_keep::flush();
        say(!window_keep::enabled() && !any, "off: enabled() is false and no commit hands anything over");
        say(fs::is_empty(root), "off: nothing was written anywhere under the directory");
    }

    // ---- 2. one visit ------------------------------------------------------------------
    uint64_t visit_bytes = 0;
    {
        const std::string dir = root + "/one";
        setenv("OD_I1991_TOKEN", "do-not-write-me", 1);
        setenv("OD_I1991_PLAIN", "seen", 1);
        window_keep::configure(dir, 1ULL << 40);
        const Visit v = makeVisit(base, 1, cameras);
        for (int d = 0; d < 3; d++)
        {
            window_keep::deposit(windowOf(v, d, 10 + d));
            say(window_keep::commit("REF" + std::to_string(d), 10 + d), "one visit: dart " + std::to_string(d + 1) + " is handed to the writer");
        }
        window_keep::flush();
        const window_keep::Figures f = window_keep::figures();
        visit_bytes = f.bytes;
        say(countFiles(dir, "pics") == 12 && countFiles(dir, "darts") == 3,
            "one visit: 12 pictures and 3 accounts on disk (got " + std::to_string(countFiles(dir, "pics")) + " and " +
                std::to_string(countFiles(dir, "darts")) + "); 27 would be every picture written every time");
        say(f.written == 3 && f.dropped == 0 && f.evicted == 0 && f.bytes == diskBytes(dir),
            "one visit: the ledger's bytes are the directory's (" + std::to_string(f.bytes) + " vs " + std::to_string(diskBytes(dir)) + ")");
        bool pictures = true, roles = true;
        for (int d = 0; d < 3; d++)
        {
            const std::string json = slurp(fs::path(dir) / "darts" / ("REF" + std::to_string(d) + ".json"));
            const auto settled = named(json, "settled"), clean = named(json, "clean"), fresh = named(json, "fresh");
            roles = roles && settled.size() == 3 && clean.size() == 3 && fresh.size() == (d == 0 ? 0u : 3u);
            for (size_t c = 0; roles && c < 3; c++)
            {
                pictures = pictures && samePicture(dir, settled[c], v.settled[d][c]) && samePicture(dir, clean[c], v.clean[c]);
                if (d > 0)
                {
                    pictures = pictures && samePicture(dir, fresh[c], v.settled[d - 1][c]) &&
                               fresh[c] == named(slurp(fs::path(dir) / "darts" / ("REF" + std::to_string(d - 1) + ".json")), "settled")[c];
                }
            }
        }
        say(roles, "one visit: each account names 3 settled, 3 clean and (after dart 1) 3 fresh pictures; dart 1's fresh is null");
        say(pictures, "one visit: every picture decodes to the exact Mat that went in, under its role, and dart N's fresh IS dart N-1's settled file");
        const std::string third = slurp(fs::path(dir) / "darts" / "REF2.json");
        say(third.find("\"reference\":\"REF2\",\"window\":12,\"board_state_before\":2") != std::string::npos &&
                third.find("\"camera\":2,") != std::string::npos &&
                third.find("\"previous_state\":2,\"previous_board_change\":2001,\"frames\":9") != std::string::npos &&
                third.find("\"board\":{\"cx\":640.5,\"cy\":360.25,\"w\":500,\"h\":420,\"angle\":12.5}") != std::string::npos &&
                third.find("\"reported_tips\":[[100.5,201],[101.5,201]]") != std::string::npos,
            "one visit: dart 3's account carries the window, the state before, camera 2's scalars, its board and its two reported tips");
        say(third.find("\"OD_I1991_TOKEN\":\"(withheld)\"") != std::string::npos && third.find("do-not-write-me") == std::string::npos &&
                third.find("\"OD_I1991_PLAIN\":\"seen\"") != std::string::npos,
            "one visit: the OD_ switches ride in the account, and one named like a credential is (withheld)");
        say(third.find("\"calibration\":null") != std::string::npos,
            "one visit: no cache/geometry_calibration.dat here, so the account says calibration null and not a guess");
        const uint64_t settled_png = fs::file_size(fs::path(dir) / "pics" / (named(third, "settled")[0] + ".png"));
        std::cout << "MEASURED one_visit_bytes=" << f.bytes << " per_dart=" << f.bytes / 3 << " one_settled_png=" << settled_png
                  << " undeduplicated_per_dart~=" << settled_png * 8 << " (8 pictures a dart on average: 6, 9, 9)" << std::endl;

        // ---- 3. the wrong window ---------------------------------------------------------
        window_keep::deposit(windowOf(v, 2, 77));
        const bool handed = window_keep::commit("WRONG", 78);
        window_keep::flush();
        say(!handed && !fs::exists(fs::path(dir) / "darts" / "WRONG.json") && countFiles(dir, "pics") == 12,
            "a deposit whose window the published dart does not name is never written");
    }

    // ---- 4. the bound ------------------------------------------------------------------
    const std::string dir = root + "/bounded";
    const uint64_t bound = visit_bytes * 5 / 2;
    std::vector<std::string> order;
    long ordinal = 100;
    std::string broken;
    bool within = true, newest = true;
    const auto feed = [&](int v)
    {
        const Visit visit = makeVisit(base, v, cameras);
        for (int d = 0; d < 3; d++)
        {
            const std::string ref = "V" + std::to_string(v) + "D" + std::to_string(d);
            window_keep::deposit(windowOf(visit, d, ++ordinal));
            window_keep::commit(ref, ordinal);
            window_keep::flush();
            order.push_back(ref);
            if (broken.empty())
            {
                broken = brokenAccount(dir);
                if (!broken.empty())
                {
                    broken = "after " + ref + ": " + broken;
                }
            }
            within = within && diskBytes(dir) <= bound;
            // the darts on disk are a suffix of the commit order
            bool gap = false, seen_kept = false;
            for (const std::string &r : order)
            {
                const bool kept = fs::exists(fs::path(dir) / "darts" / (r + ".json"));
                gap = gap || (seen_kept && !kept);
                seen_kept = seen_kept || kept;
            }
            newest = newest && !gap && seen_kept;
        }
    };
    window_keep::configure(dir, bound);
    for (int v = 2; v < 8; v++)
    {
        feed(v);
    }
    {
        const window_keep::Figures f = window_keep::figures();
        say(f.written == 18 && f.evicted > 0, "bounded: 18 darts written and something evicted (" + std::to_string(f.evicted) + " files)");
        say(broken.empty(), "bounded: every account on disk has every picture it names, after every dart" + (broken.empty() ? std::string() : " -- " + broken));
        say(within, "bounded: the directory never ends a dart over the bound of " + std::to_string(bound) + " bytes");
        say(newest && !fs::exists(fs::path(dir) / "darts" / "V2D0.json") && fs::exists(fs::path(dir) / "darts" / "V7D2.json"),
            "bounded: the darts on disk are always the newest in commit order; the first is gone and the last is there");
        std::cout << "MEASURED bounded_kept_darts=" << countFiles(dir, "darts") << " of 18 in " << diskBytes(dir) << " bytes" << std::endl;
    }

    // ---- 5. a restart ------------------------------------------------------------------
    {
        const size_t before = countFiles(dir, "darts");
        window_keep::configure(dir, bound);
        const window_keep::Figures adopted = window_keep::figures();
        say(adopted.bytes == diskBytes(dir) && before > 0,
            "restart: the ledger adopts the directory (" + std::to_string(adopted.bytes) + " bytes, " + std::to_string(before) + " darts)");
        feed(8);
        feed(9);
        say(broken.empty(), "restart: every account on disk still has every picture it names" + (broken.empty() ? std::string() : " -- " + broken));
        say(within && newest && fs::exists(fs::path(dir) / "darts" / "V9D2.json") && !fs::exists(fs::path(dir) / "darts" / "V6D0.json"),
            "restart: still under the bound, still the newest darts, and what the run before wrote went first");
    }

    fs::remove_all(root);
    std::cout << checks << " checks, " << failures.size() << " failed" << std::endl;
    for (const std::string &f : failures)
    {
        std::cout << "  FAILED: " << f << std::endl;
    }
    return failures.empty() ? 0 : 1;
}
