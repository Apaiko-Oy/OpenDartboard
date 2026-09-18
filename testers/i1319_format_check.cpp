// #1319's proof that a FOURCC of 0 can never again render as an empty name inside a
// sentence, and that the frame-rate and bandwidth findings are things a run can fail.
//
// No camera is opened. Everything asserted here is a pure function in capture.hpp, so
// this runs on a box with no video devices at all -- which is where it was written.
//
//   g++ -std=c++17 -I src/utils -o i1319_format_check testers/i1319_format_check.cpp \
//       $(pkg-config --cflags --libs opencv4)
//
// The mutations that make it fail are in testers/i1319_run.sh, beside the checks they
// falsify: a check nothing can fail is not evidence.

#include "capture.hpp"
#include <cstdio>
#include <string>

static int failures = 0;

static void say(bool ok, const std::string &what)
{
    std::printf("%s %s\n", ok ? "OK  " : "FAIL", what.c_str());
    if (!ok)
        failures++;
}

static bool contains(const std::string &haystack, const std::string &needle)
{
    return haystack.find(needle) != std::string::npos;
}

int main()
{
    using namespace camera;

    const int mjpg = ('M') | ('J' << 8) | ('P' << 16) | ('G' << 24);
    const int yuy2 = ('Y') | ('U' << 8) | ('Y' << 16) | ('2' << 24);

    std::printf("=== 1. a FOURCC of 0 is its own outcome, and names itself ===\n");
    {
        const NegotiatedFormat none = readFourCC(0);
        say(none.report == FormatReport::NotReported, "0 reads as FormatReport::NotReported");
        say(!none.name.empty(), "0 has a non-empty name (\"" + none.name + "\")");
        say(!none.is_mjpg, "0 is not mistaken for MJPG");

        const Finding finding = formatFinding(1, 0, 1280, 720, 10);
        std::printf("     %s\n", finding.text.c_str());
        say(finding.said && finding.severity == Severity::Warning, "it is said, as a warning");
        say(contains(finding.text, "reported no format at all"),
            "the line says the backend reported no format");
        // The empty-name sentence, byte for byte, must not be constructible any more.
        say(!contains(finding.text, "negotiated , "), "no empty format name inside the sentence");
        say(!contains(finding.text, "negotiated  "), "no doubled space where a name should be");
    }

    std::printf("=== 2. a format that is not MJPG is a DIFFERENT outcome ===\n");
    {
        const Finding finding = formatFinding(2, yuy2, 1280, 720, 10);
        std::printf("     %s\n", finding.text.c_str());
        say(finding.said && finding.severity == Severity::Warning, "it is said, as a warning");
        say(contains(finding.text, "negotiated YUY2"), "the line names YUY2");
        say(!contains(finding.text, "reported no format at all"),
            "it is not the no-format sentence");
        say(finding.text != formatFinding(2, 0, 1280, 720, 10).text,
            "the two sentences are not the same sentence");
    }

    std::printf("=== 3. MJPG is the quiet one ===\n");
    {
        const Finding finding = formatFinding(3, mjpg, 1280, 720, 30);
        std::printf("     %s\n", finding.text.c_str());
        say(finding.said && finding.severity == Severity::Info, "it is said, at INFO");
        say(contains(finding.text, "negotiated MJPG at 1280x720 @ 30 fps"), "it names the mode");
    }

    std::printf("=== 4. an unprintable code is spelled, not pasted ===\n");
    {
        const NegotiatedFormat odd = readFourCC(0x00000014);
        say(odd.report == FormatReport::Unprintable, "0x14 reads as Unprintable");
        say(odd.name == "0x00000014", "it is spelled in hex (\"" + odd.name + "\")");
        const Finding finding = formatFinding(1, 0x00000014, 1280, 720, 10);
        std::printf("     %s\n", finding.text.c_str());
        say(contains(finding.text, "0x00000014"), "the hex reaches the line");
    }

    std::printf("=== 5. the rate is compared against the rate that was ASKED FOR ===\n");
    {
        // The comparison must be between two different quantities. A check that
        // compares a value against itself can never fire; the mutation in i1319_run.sh
        // does exactly that and this is what goes red.
        say(!rateWasGranted(10, 15), "10 granted where 15 asked is a mismatch");
        say(rateWasGranted(15, 15), "15 granted where 15 asked is not");
        say(rateWasGranted(29.97, 30), "29.97 is 30, not a refusal");
        say(!rateWasGranted(30, 15), "30 where 15 asked is a mismatch too");
        say(rateWasGranted(0, 15), "a backend that did not say is not accused");

        const Finding device = rateFinding(2, 10, 15, true);
        std::printf("     %s\n", device.text.c_str());
        say(device.said && device.severity == Severity::Warning, "a device mismatch is a warning");
        say(contains(device.text, "10 fps") && contains(device.text, "15"),
            "both figures are in the line");

        const Finding file = rateFinding(1, 30, 15, false);
        std::printf("     %s\n", file.text.c_str());
        say(file.said && file.severity == Severity::Info, "a file mismatch is INFO, not a warning");

        say(!rateFinding(1, 30, 30, false).said, "a file at the rate that was asked says nothing");
    }

    std::printf("=== 6. bandwidth is one claim about every camera, not one per camera ===\n");
    {
        std::vector<OpenedCamera> three;
        for (int i = 0; i < 3; i++)
            three.push_back(OpenedCamera{yuy2, 1280, 720, 10});
        const Finding over = busFinding(three);
        std::printf("     %s\n", over.text.c_str());
        say(over.said && over.severity == Severity::Warning, "three uncompressed 720p cameras is a warning");
        say(contains(over.text, "3 cameras are running uncompressed"), "it counts them");
        say(contains(over.text, "55.3 MB/s"), "1280*720*2*10*3 = 55.3 MB/s is the figure");
        say(contains(over.text, "35.0 MB/s"), "the threshold it is judged against is in the sentence");

        std::vector<OpenedCamera> one{OpenedCamera{yuy2, 1280, 720, 10}};
        const Finding under = busFinding(one);
        std::printf("     %s\n", under.text.c_str());
        say(under.said && under.severity == Severity::Info, "one such camera is within the bus and is INFO");

        std::vector<OpenedCamera> compressed;
        for (int i = 0; i < 3; i++)
            compressed.push_back(OpenedCamera{mjpg, 1280, 720, 30});
        say(!busFinding(compressed).said, "three MJPG cameras: no claim, because none can be computed");

        std::vector<OpenedCamera> silent;
        for (int i = 0; i < 3; i++)
            silent.push_back(OpenedCamera{0, 1280, 720, 10});
        const Finding unmeasurable = busFinding(silent);
        std::printf("     %s\n", unmeasurable.text.c_str());
        say(unmeasurable.said && unmeasurable.severity == Severity::Info,
            "three cameras that reported nothing: the arithmetic is stated, at INFO");
        say(contains(unmeasurable.text, "no bandwidth figure can be measured"),
            "it says outright that nothing was measured");
        say(contains(unmeasurable.text, "18.4 MB/s per camera") && contains(unmeasurable.text, "55.3 MB/s for 3"),
            "the scale it gives is the arithmetic of the mode they are really running");
        say(!contains(unmeasurable.text, "running uncompressed"),
            "it does not claim they are uncompressed, because nothing said so");

        std::vector<OpenedCamera> mixed{OpenedCamera{yuy2, 1280, 720, 10}, OpenedCamera{0, 1280, 720, 10}};
        const Finding partial = busFinding(mixed);
        std::printf("     %s\n", partial.text.c_str());
        say(contains(partial.text, "1 more reported no format"),
            "a camera outside the figure is named as being outside it");
    }

    std::printf("=== 7. the rate is the lever, so the DEVICE is asked for a different rate ===\n");
    {
        // MSMF picks a camera's mode by nearest resolution, then largest, then NEAREST
        // FRAME RATE, and never looks at the subtype at all (cap_msmf.cpp
        // VideoIsBetterThan, 4.14.0 line 318). Both of the rig's modes are 1280x720, so
        // the rate alone decides which one the camera is put in. This is that ranking,
        // and it is the whole of the arithmetic this issue turned on.
        const double modes[] = {30.0, 10.0}; // mjpeg @30, yuyv422 @10
        auto rigPicks = [&](double want) -> double
        {
            double best = modes[0];
            for (double mode : modes)
            {
                const double a = mode > want ? mode - want : want - mode;
                const double b = best > want ? best - want : want - best;
                if (a < b)
                    best = mode;
            }
            return best;
        };

        say(rigPicks(15) == 10.0, "the defect, as arithmetic: asking for 15 picks the 10 fps uncompressed mode");
        say(rigPicks(30) == 30.0, "and asking for 30 picks the 30 fps compressed one");

        // The fix is entirely in what the device is ASKED for, before it is asked.
        // There is no read-back, no second ask and no state: the rig hung the board on
        // camera 3 when this was a re-ask, and a function of two numbers cannot hang.
        const double msmf = 30.0; // captureRateFloorDefault() on _WIN32
        const double v4l2 = 0.0;  // ...and everywhere else

        say(captureRateRequest(15, msmf) == 30.0, "--fps 15 on MSMF asks the camera for 30");
        say(rigPicks(captureRateRequest(15, msmf)) == 30.0, "which is the mode that carries three cameras");
        say(captureRateRequest(30, msmf) == 30.0, "--fps 30 asks for 30: the path the rig already proved works");
        say(captureRateRequest(60, msmf) == 60.0, "a request ABOVE the floor is not lowered to it");
        say(captureRateRequest(10, msmf) == 30.0,
            "and one below it is raised, because 10 is the uncompressed mode and a board cannot run on it");

        // The floor is backend knowledge, not a platform preference. V4L2 has no floor
        // because CAP_PROP_FOURCC really is the negotiation there and has always worked;
        // raising the request would move a platform that never had this bug.
        say(captureRateRequest(15, v4l2) == 15.0, "with no floor the operator's number goes through untouched");
        say(captureRateRequest(10, v4l2) == 10.0, "at any value");

        const Finding raised = rateFloorFinding(1, 15, captureRateRequest(15, msmf), "MSMF");
        std::printf("     %s\n", raised.text.c_str());
        say(raised.said && raised.severity == Severity::Info, "a raised request is said, at INFO");
        say(contains(raised.text, "30 fps") && contains(raised.text, "15"),
            "both numbers are in the line: what the camera is asked, and what --fps says");
        say(contains(raised.text, "MSMF"), "the backend whose chooser makes this necessary is named");
        say(!rateFloorFinding(1, 15, 15, "V4L2").said,
            "a request that was not raised says nothing, because there is nothing to explain");
        say(!rateFloorFinding(1, 60, captureRateRequest(60, msmf), "MSMF").said,
            "and neither does one above the floor");

        // #1319: a device is now held to the rate it was really ASKED for. Asked 30 and
        // given 30 is silence; asked 30 and given 10 is the failure this issue is about
        // and is a warning naming both figures.
        say(!rateFinding(1, 30, captureRateRequest(15, msmf), true).said,
            "a camera that grants the raised request says nothing");
        const Finding refusedRate = rateFinding(3, 10, captureRateRequest(15, msmf), true);
        std::printf("     %s\n", refusedRate.text.c_str());
        say(refusedRate.said && refusedRate.severity == Severity::Warning,
            "a camera still stuck on 10 after being asked for 30 is a warning");
        say(contains(refusedRate.text, "slower mode"), "and it is named as the slower mode it is");

        // The same sentence must not be printed about a camera doing BETTER than asked.
        const Finding faster = rateFinding(2, 30, 15, true);
        std::printf("     %s\n", faster.text.c_str());
        say(faster.said && faster.severity == Severity::Info, "faster than asked is INFO, not a warning");
        say(contains(faster.text, "faster mode") && !contains(faster.text, "slower mode"),
            "and the line says faster rather than slower");
    }

    std::printf("=== 8. a refused MJPG request is said by name ===\n");
    {
        // MSMF answers false to set(CAP_PROP_FOURCC, MJPG) -- configureVideoOutput's
        // conversion switch accepts BGR3/RGB3/GREY/YUYV and returns false to everything
        // else (4.14.0 line 1153). The refusal is the first acceptance criterion of this
        // issue: the reason MJPG could not be asked for is reported by name.
        const Finding refused = fourccRequestFinding(1, false, "MSMF");
        std::printf("     %s\n", refused.text.c_str());
        say(refused.said && refused.severity == Severity::Warning, "a refusal is a warning");
        say(contains(refused.text, "MSMF"), "the backend that refused is named");
        say(contains(refused.text, "refused CAP_PROP_FOURCC"), "the property that was refused is named");
        say(!fourccRequestFinding(1, true, "V4L2").said,
            "a backend that took the request says nothing, because there it IS the negotiation");
    }

    std::printf("\n%s (%d failed)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
