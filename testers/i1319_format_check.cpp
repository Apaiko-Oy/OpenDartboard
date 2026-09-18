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
        say(!busFinding(silent).said, "three cameras that reported nothing: no figure is invented");

        std::vector<OpenedCamera> mixed{OpenedCamera{yuy2, 1280, 720, 10}, OpenedCamera{0, 1280, 720, 10}};
        const Finding partial = busFinding(mixed);
        std::printf("     %s\n", partial.text.c_str());
        say(contains(partial.text, "1 more reported no format"),
            "a camera outside the figure is named as being outside it");
    }

    std::printf("\n%s (%d failed)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
