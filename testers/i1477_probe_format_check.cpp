// #1477's proof that a camera --autocams REJECTS says which of two things happened to it,
// and that the sentence it says is #1319's sentence rather than a second spelling of it.
//
// No camera is opened and no Windows is needed. autocam::probe() is #ifdef _WIN32 -- which
// is why #1319 repaired the open site and deliberately left this one alone -- but the
// decision and the words it prints are now a pure function of one int, sitting outside that
// #ifdef, so every branch is exercised on the Linux box this was written on.
//
//   testers/unit_check.sh 1477
//
// The strings this asserts on are the ones that reach the log. autocam.hpp prints
//   " %s [%d] %s: ... fps <verdict.name>"        the enumeration line, one per device
//   "   rejected: <verdict.reason>"              the line this issue is about
//   "   kept, unverified: <verdict.warning>"     #1336: the read could not verify MJPG
// so a non-empty name and a sentence that names the case are the whole of the repair.
//
// The mutations that make it fail are in the pull request, beside the checks they falsify:
// a check nothing can fail is not evidence.

#include "autocam.hpp"
#include <cstdio>
#include <string>
#include <vector>

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
    const int mjpg = ('M') | ('J' << 8) | ('P' << 16) | ('G' << 24);
    const int yuy2 = ('Y') | ('U' << 8) | ('Y' << 16) | ('2' << 24);
    const int nv12 = ('N') | ('V' << 8) | ('1' << 16) | ('2' << 24);
    const int lower = ('m') | ('j' << 8) | ('p' << 16) | ('g' << 24);

    std::printf("=== 1. a FOURCC of 0 is its own outcome, and the line says so ===\n");
    {
        // #1336 (measured on the rig, 2026-09-24): a read-back that cannot name a
        // format no longer REJECTS -- it keeps the camera and warns -- so the sentence
        // this section holds is now the warning rather than a rejection reason.
        const autocam::FormatVerdict none = autocam::judgeProbedFormat(0);
        std::printf("       [1] Some Camera: 1280x720 @ 30 fps %s\n", none.name.c_str());
        std::printf("       kept, unverified: %s\n", none.warning.c_str());

        say(!none.reported, "0 is \"the backend did not say\", not a format");
        say(!none.name.empty(), "it has a non-empty name (\"" + none.name + "\")");
        say(contains(none.warning, "reported no format at all"),
            "the warning says the backend reported no format");
        say(contains(none.warning, "CAP_PROP_FOURCC read back as 0"),
            "and names the property that answered 0");

        // The defect, byte for byte. This is the sentence #1336 quotes and it must not be
        // constructible any more -- from the warning, or from the whole line around it.
        const std::string line = "   kept, unverified: " + none.warning;
        say(!contains(line, "negotiated , not MJPG"), "the empty-name sentence is gone");
        say(!contains(line, "negotiated ,"), "no empty format name inside the sentence");
        say(!contains(line, "negotiated  "), "no doubled space where a name should be");
        say(!none.warning.empty(), "a camera kept unverified is given a warning at all");
    }

    std::printf("=== 2. a format that is not MJPG is a DIFFERENT outcome ===\n");
    {
        const autocam::FormatVerdict other = autocam::judgeProbedFormat(yuy2);
        std::printf("       [2] Some Camera: 1280x720 @ 10 fps %s\n", other.name.c_str());
        std::printf("       rejected: %s\n", other.reason.c_str());

        say(other.reported, "YUY2 is a format the backend really reported");
        say(other.name == "YUY2", "it is named YUY2 (\"" + other.name + "\")");
        say(contains(other.reason, "negotiated YUY2"), "the rejection names YUY2");
        say(!contains(other.reason, "reported no format at all"),
            "it is not the no-format sentence");

        const autocam::FormatVerdict none = autocam::judgeProbedFormat(0);
        say(other.reason != none.warning, "the rejection and the warning are not the same sentence");
        say(other.name != none.name, "and the two cameras are not given the same format name");
        say(!contains(none.warning, "negotiated YUY2"),
            "the no-format case does not borrow the named case's words");

        const autocam::FormatVerdict third = autocam::judgeProbedFormat(nv12);
        say(contains(third.reason, "negotiated NV12"),
            "a third format is named as itself and not as the second");
    }

    std::printf("=== 3. the positive control: MJPG is kept, and says nothing ===\n");
    {
        // A check that only proves the zero case proves half of it. This is the half that
        // says the function can still answer yes -- a mutation that rejects everything, or
        // that accepts everything, has to fail one of these two sections.
        const autocam::FormatVerdict kept = autocam::judgeProbedFormat(mjpg);
        say(kept.is_mjpg, "MJPG is accepted");
        say(kept.reported, "and it was really reported");
        say(kept.name == "MJPG", "it is named MJPG (\"" + kept.name + "\")");
        say(kept.reason.empty(), "a camera that is kept is given no rejection to print");
        say(kept.warning.empty(), "and a VERIFIED MJPG carries no warning either");
    }

    std::printf("=== 4. an unprintable code is spelled, not pasted into the sentence ===\n");
    {
        // #1336: an unprintable code is what MSMF answers about every camera -- the
        // rig's three read 0x00000016, OpenCV's conversion target, while streaming
        // MJPG -- so it keeps the camera and the hex reaches the warning instead.
        const autocam::FormatVerdict odd = autocam::judgeProbedFormat(0x00000014);
        std::printf("       kept, unverified: %s\n", odd.warning.c_str());
        say(odd.reported, "a non-zero code IS something the backend said");
        say(odd.name == "0x00000014", "it is spelled in hex (\"" + odd.name + "\")");
        say(contains(odd.warning, "0x00000014"), "the hex reaches the warning");
        say(!contains(odd.warning, "reported no format at all"),
            "and it is not confused with the backend saying nothing");
    }

    std::printf("=== 5. one repair, one spelling: the words are #1319's ===\n");
    {
        // A repository with two spellings of one repair drifts. These hold the probe's
        // sentences to the ones capture.hpp prints where a camera is OPENED, so moving
        // either wording alone is a red build.
        const std::string opened_none = camera::formatFinding(1, 0, 1280, 720, 10).text;
        const std::string opened_yuy2 = camera::formatFinding(2, yuy2, 1280, 720, 10).text;
        std::printf("       open site: %s\n", opened_none.c_str());
        std::printf("       open site: %s\n", opened_yuy2.c_str());

        const autocam::FormatVerdict none = autocam::judgeProbedFormat(0);
        const autocam::FormatVerdict other = autocam::judgeProbedFormat(yuy2);

        say(contains(opened_none, "MJPG was requested and the backend reported no format at all") &&
                contains(none.warning, "MJPG was requested and the backend reported no format at all"),
            "both sites say \"MJPG was requested and the backend reported no format at all\"");
        say(contains(opened_none, "(CAP_PROP_FOURCC read back as 0)") &&
                contains(none.warning, "(CAP_PROP_FOURCC read back as 0)"),
            "both name the property, the same way");
        say(contains(opened_yuy2, "not the MJPG that was requested") &&
                contains(other.reason, "not the MJPG that was requested"),
            "both say \"not the MJPG that was requested\"");

        // And both get the NAME from the same function, so a format capture.hpp learns to
        // spell is spelled the same here.
        say(camera::readFourCC(0).name == none.name, "the no-format name is readFourCC's");
        say(camera::readFourCC(yuy2).name == other.name, "and so is YUY2's");
    }

    std::printf("=== 6. what --autocams ACCEPTS: #1336's decision, made on the rig ===\n");
    {
        // #1477 left this census unchanged because whether an unverifiable read-back
        // should reject at all was #1336's question and needed the rig. Measured there
        // on 2026-09-24: all three board cameras -- which stream MJPG 1280x720@30, the
        // rig fixtures are recorded off them -- read back 0x00000016, OpenCV's own RGB
        // conversion target, and --autocams rejected every one. So the read is
        // uninformative on MSMF and must not reject: a camera is kept unless the
        // backend POSITIVELY NAMED a non-MJPG format, the one answer a rejection can
        // stand on. testers/i1336_probe_admission_check.cpp carries the decision's own
        // proof; this row keeps the whole table in one place.
        struct Case
        {
            int code;
            bool admitted;
            const char *what;
        };
        const std::vector<Case> cases = {
            {mjpg, true, "MJPG"},
            {0, true, "a FOURCC of 0"},
            {yuy2, false, "YUY2"},
            {nv12, false, "NV12"},
            {lower, false, "lower-case mjpg"},
            {0x00000014, true, "an unprintable code"},
        };
        for (const Case &c : cases)
        {
            const autocam::FormatVerdict verdict = autocam::judgeProbedFormat(c.code);
            say(verdict.admitted == c.admitted,
                std::string(c.what) + (c.admitted ? " is kept" : " is rejected"));
            say(verdict.reason.empty() == c.admitted,
                std::string(c.what) + (c.admitted ? " carries no reason" : " carries a reason"));
            say(!verdict.name.empty(),
                std::string(c.what) + " has a name to print on the enumeration line");
        }
    }

    std::printf("\n%s (%d failed)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
