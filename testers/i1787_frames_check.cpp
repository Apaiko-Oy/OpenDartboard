// #1787: the ring of kept frames, and the bytes one kept dart costs.
//
// frame_keep.hpp keeps the last N published darts' settled frames (one grey picture per
// camera, deposited by dart_processing at the vote and committed under the dart's
// reference when the Turnaus client mints it) and answers a request by reference with
// PNGs and the window's census lines, or "gone". This holds all of it without a detector:
// the pictures are a real settled-size frame off mocks/rig-20260918 (the first frame of
// cam_1, grey, as the window averages them) and two transforms of it, so the three
// "cameras" differ and a swapped PNG would be caught.
//
// PREDICTION, stated before the run: with N=10 and fifteen darts committed, dart 5 is
// GONE and dart 15 is served -- three PNGs that decode back to the exact pictures that
// went in, the window's two census lines and not the stray one about another window. A
// deposit whose window the published result does not name is never committed (gone by
// construction). With the pin off (OD_KEEP_FRAMES unset: configure(0)) every one of the
// same calls keeps nothing and every answer is gone, which is the zero-cost control.
//
// THE FIGURE. One kept dart at 1280x720 and three cameras is 3 x 921,600 = 2,764,800 raw
// bytes by arithmetic; this prints what the buffer really holds for it (raw_bytes) and
// what its three PNGs come to, so docs/rig.md's number is measured and not derived.
//
//   g++ -std=c++17 -I src -I src/utils -I build/_deps/nlohmann_json-src/include \
//       -I build/_deps/httplib-src testers/i1787_frames_check.cpp \
//       src/communication/turnaus_client.cpp -o frames_check \
//       $(pkg-config --cflags --libs opencv4) -lpthread

#include <iostream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>
#include <opencv2/opencv.hpp>

#include "communication/turnaus_client.hpp"
#include "detector/geometry/detection/frame_keep.hpp"
#include "logging.hpp"

using json = nlohmann::json;

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

/** A settled frame: the fixture's first frame, grey, at the window's size; a drawn board if the clip is not there. */
static cv::Mat settledFrame(std::string &source)
{
    cv::Mat frame;
    const char *clip = "mocks/rig-20260918/cam_1.mp4";
    cv::VideoCapture cap(clip);
    if (cap.isOpened())
    {
        cv::Mat bgr;
        if (cap.read(bgr) && !bgr.empty())
        {
            cv::cvtColor(bgr, frame, cv::COLOR_BGR2GRAY);
            source = std::string(clip) + " frame 0, " + std::to_string(frame.cols) + "x" + std::to_string(frame.rows);
        }
    }
    if (frame.empty())
    {
        frame = cv::Mat(720, 1280, CV_8UC1);
        for (int y = 0; y < frame.rows; y++)
        {
            for (int x = 0; x < frame.cols; x++)
            {
                frame.at<unsigned char>(y, x) = (unsigned char)((x * 7 + y * 3) % 251);
            }
        }
        cv::circle(frame, cv::Point(640, 360), 300, cv::Scalar(255), 3);
        source = "a drawn 1280x720 board (the fixture clip could not be opened)";
    }
    if (frame.cols != 1280 || frame.rows != 720)
    {
        cv::resize(frame, frame, cv::Size(1280, 720));
        source += ", resized to 1280x720";
    }
    return frame;
}

static std::vector<unsigned char> unbase64(const std::string &s)
{
    static int table[256];
    static bool built = false;
    if (!built)
    {
        const char *alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        for (int i = 0; i < 256; i++)
        {
            table[i] = -1;
        }
        for (int i = 0; i < 64; i++)
        {
            table[(unsigned char)alphabet[i]] = i;
        }
        built = true;
    }
    std::vector<unsigned char> out;
    unsigned acc = 0;
    int bits = 0;
    for (char c : s)
    {
        if (c == '=')
        {
            break;
        }
        const int v = table[(unsigned char)c];
        if (v < 0)
        {
            continue;
        }
        acc = (acc << 6) | (unsigned)v;
        bits += 6;
        if (bits >= 8)
        {
            bits -= 8;
            out.push_back((unsigned char)((acc >> bits) & 0xFF));
        }
    }
    return out;
}

static bool same(const cv::Mat &a, const cv::Mat &b)
{
    if (a.size() != b.size() || a.type() != b.type())
    {
        return false;
    }
    cv::Mat diff;
    cv::absdiff(a, b, diff);
    return cv::countNonZero(diff) == 0;
}

int main()
{
    std::string source;
    const cv::Mat frame = settledFrame(source);
    std::cout << "  settled frame: " << source << std::endl;
    std::vector<cv::Mat> cameras(3);
    cameras[0] = frame;
    cv::flip(frame, cameras[1], 1);
    cv::rotate(frame, cameras[2], cv::ROTATE_180);
    const size_t raw_per_camera = frame.total() * frame.elemSize();

    // ---- the pin off: nothing is kept, nothing costs --------------------------------------
    {
        (void)frame_keep::enabled(); // read the environment once, before this check configures itself
        frame_keep::configure(0);
        say(!frame_keep::enabled(), "OD_KEEP_FRAMES unset: the buffer is off");
        frame_keep::deposit(7, cameras);
        say(!frame_keep::commit("REF-OFF", 7) && frame_keep::kept() == 0, "a deposit and a commit keep nothing");
        say(!frame_keep::answer("REF-OFF").found, "and the answer is gone");
        say(logging::lineTap == nullptr, "and no log tap is installed: a log line costs one pointer read");
    }

    // ---- the pin on, N=10, fifteen darts --------------------------------------------------
    frame_keep::configure(10);
    say(frame_keep::enabled() && frame_keep::capacity() == 10, "OD_KEEP_FRAMES=on: the ring holds ten");
    say(logging::lineTap != nullptr, "the log tap is installed");

    for (int k = 1; k <= 15; k++)
    {
        const long window = k * 3;
        frame_keep::deposit(window, cameras);
        // The census lines the scoring prints about this window, through the real logger,
        // plus one about ANOTHER window that must not attach, plus an ordinary line.
        logging::log("I1512ENTRY window=" + std::to_string(window) + " outcome=SOLVED solved=1 x=1.0 y=2.0", logging::LogLevel::INFO, "CHECK");
        logging::log("I1512ENTRY window=999 outcome=SOLVED solved=1", logging::LogLevel::INFO, "CHECK");
        logging::log("Geometric score: T20 from 2 intersecting constraint(s)", logging::LogLevel::INFO, "CHECK");
        logging::log("I1681CONTROL window=" + std::to_string(window) + " solved=1 usable=2", logging::LogLevel::INFO, "CHECK");
        const bool kept = frame_keep::commit("REF-" + std::to_string(k), window);
        if (!kept)
        {
            say(false, "dart " + std::to_string(k) + " was not kept");
        }
    }
    say(frame_keep::kept() == 10, "fifteen darts in, ten are kept (" + std::to_string(frame_keep::kept()) + ")");
    say(!frame_keep::answer("REF-5").found, "dart 5 is gone, as predicted");
    say(!frame_keep::answer("REF-4").found && frame_keep::answer("REF-6").found, "dart 4 is gone and dart 6 is the oldest kept");

    const frame_keep::Answer a = frame_keep::answer("REF-15");
    say(a.found && a.window == 45, "dart 15 is served, from window 45");
    say(a.pngs.size() == 3, "three cameras' PNGs");
    size_t png_bytes = 0;
    for (size_t i = 0; i < a.pngs.size() && i < 3; i++)
    {
        png_bytes += a.pngs[i].size();
        cv::Mat back = cv::imdecode(a.pngs[i], cv::IMREAD_UNCHANGED);
        say(!back.empty() && same(back, cameras[i]),
            "camera " + std::to_string(i + 1) + "'s PNG decodes to exactly the picture that went in (" +
                std::to_string(a.pngs[i].size()) + " bytes)");
    }
    say(a.census.size() == 2 && a.census[0].rfind("I1512ENTRY window=45", 0) == 0 &&
            a.census[1].rfind("I1681CONTROL window=45", 0) == 0,
        "the window's two census lines ride with it, the stray window=999 line and the prose do not (" +
            std::to_string(a.census.size()) + " lines)");

    // ---- THE FIGURE ---------------------------------------------------------------------------
    std::cout << "  MEASURED raw_bytes_per_dart=" << a.raw_bytes << " (3 x " << raw_per_camera
              << " at " << frame.cols << "x" << frame.rows << " grey) png_bytes_per_dart=" << png_bytes
              << " ring_of_10_raw_bytes=" << a.raw_bytes * 10 << " ring_of_10_MB=" << (a.raw_bytes * 10) / 1048576.0
              << std::endl;
    say(a.raw_bytes == 3 * raw_per_camera && a.raw_bytes == 2764800,
        "a kept dart is 2,764,800 raw bytes: three 1280x720 grey pictures, as docs/rig.md says");
    say(a.raw_bytes * 10 < 100u * 1024u * 1024u, "and the ring of ten is well under 100 MB on the Pi");

    // ---- a deposit the published result does not name is never committed ------------------
    {
        frame_keep::deposit(1000, cameras);
        say(!frame_keep::commit("REF-X", 1001) && !frame_keep::answer("REF-X").found,
            "a window the published dart does not name is dropped at the commit: gone by construction");
        say(frame_keep::kept() == 10, "and nothing else moved");
        // A commit with nothing pending keeps nothing either (an END, a dart before any deposit).
        say(!frame_keep::commit("REF-Y", 1000), "a commit with no deposit pending keeps nothing");
    }

    // ---- the answer body, as it is posted ---------------------------------------------------
    {
        json j = json::parse(TurnausClient::frameAnswerBody("REF-15", a));
        say(j["reference"] == "REF-15" && j["found"] == true && j["window"] == 45, "the body names the dart, found, and its window");
        say(j["frames"].size() == 3 && j["frames"][0]["camera"] == 1 && j["frames"][2]["camera"] == 3,
            "three frames numbered 1..3 as the log numbers cameras");
        const std::vector<unsigned char> png = unbase64(j["frames"][1]["png_base64"].get<std::string>());
        say(png == a.pngs[1], "the base64 round-trips to the PNG's own bytes");
        say(j["census"].size() == 2, "the census lines are in it");
        json gone = json::parse(TurnausClient::frameAnswerBody("REF-5", frame_keep::answer("REF-5")));
        say(gone["found"] == false && !gone.contains("frames") && !gone.contains("census"),
            "a gone dart's body says found=false and carries no frames (" + gone.dump() + ")");
        // A camera that brought no frame is left out of the array, numbered past it.
        std::vector<cv::Mat> two = {cameras[0], cv::Mat(), cameras[2]};
        frame_keep::deposit(2000, two);
        frame_keep::commit("REF-TWO", 2000);
        json t = json::parse(TurnausClient::frameAnswerBody("REF-TWO", frame_keep::answer("REF-TWO")));
        say(t["frames"].size() == 2 && t["frames"][0]["camera"] == 1 && t["frames"][1]["camera"] == 3,
            "a camera that brought no frame is left out, and the others keep their numbers");
    }

    // ---- OD_KEEP_FRAMES=<N> -----------------------------------------------------------------
    {
        say(frame_keep::detail::capacityFromEnv("on") == 10 && frame_keep::detail::capacityFromEnv("25") == 25 &&
                frame_keep::detail::capacityFromEnv(nullptr) == 0 && frame_keep::detail::capacityFromEnv("off") == 0 &&
                frame_keep::detail::capacityFromEnv("") == 0,
            "OD_KEEP_FRAMES: on is ten, a number is that many, unset or anything else is off");
        frame_keep::configure(0);
        say(!frame_keep::enabled() && frame_keep::kept() == 0 && logging::lineTap == nullptr,
            "turning it off empties the ring and removes the tap");
    }

    std::cout << std::endl
              << "checks " << checks << "   failed " << failures.size() << std::endl;
    for (const std::string &f : failures)
    {
        std::cout << "  - " << f << std::endl;
    }
    return failures.empty() ? 0 : 1;
}
