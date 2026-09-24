// #1336's decision, held to its measurement: a CAP_PROP_FOURCC read-back that cannot
// name a wire format does not reject a camera from --autocams.
//
// Measured on the rig, 2026-09-24, three board cameras on one Windows machine, the
// 5bc3b0a build. Every camera answered the probe identically:
//
//    - [0] USB Camera: 1280x720 @ 10 fps 0x00000016
//      rejected: negotiated 0x00000016, not the MJPG that was requested
//
// 0x00000016 is 22 -- MFVideoFormat_RGB32's Data1, OpenCV's OWN conversion target with
// CAP_PROP_CONVERT_RGB on (capture.hpp reads it out of cap_msmf.cpp) -- and the same
// three cameras stream MJPG 1280x720@30, three at once on one bus: mocks/rig-20260918
// was recorded off them. So on MSMF the read-back answers a conversion target or 0
// whatever the camera transmits, and a rejection built on it rejected every camera
// --autocams exists to find. The decision: an uninformative read KEEPS the camera and
// warns; only a format the backend POSITIVELY NAMED, and which is not MJPG, rejects --
// that answer is real where it happens (V4L2, or MSMF with conversion off) and three
// uncompressed 720p streams do not fit on one USB 2.0 bus. Whether a kept camera
// belongs to this board stays board_look's question (#1318): it judges what a camera
// can see, not what it reports.
//
//   testers/unit_check.sh 1336
//
// probe() itself is #ifdef _WIN32; the decision is the platform-neutral
// autocam::judgeProbedFormat(), so every branch runs here with no camera and no MSMF.
// testers/i1477_probe_format_check.cpp holds the WORDING of these sentences to the open
// site's; this check holds the ADMISSION, including against the two cheap mutants -- a
// judge that keeps everything cannot pass the YUY2 rows, and one that rejects anything
// non-MJPG cannot pass the rig's own value.

#include "autocam.hpp"
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
    const int mjpg = ('M') | ('J' << 8) | ('P' << 16) | ('G' << 24);
    const int yuy2 = ('Y') | ('U' << 8) | ('Y' << 16) | ('2' << 24);
    const int nv12 = ('N') | ('V' << 8) | ('1' << 16) | ('2' << 24);

    std::printf("=== 1. the rig's own value: 0x00000016 keeps the camera ===\n");
    {
        // The literal read-back every camera on the rig answered. This row is the
        // measurement; if it rejects again, --autocams is back to rejecting a whole
        // working rig.
        const autocam::FormatVerdict rig = autocam::judgeProbedFormat(0x00000016);
        std::printf("       kept, unverified: %s\n", rig.warning.c_str());

        say(rig.admitted, "0x00000016 -- the value all three rig cameras read -- is kept");
        say(!rig.is_mjpg, "kept is not the same claim as verified MJPG");
        say(rig.reason.empty(), "there is no rejection to print");
        say(!rig.warning.empty(), "the not-knowing is said rather than swallowed");
        say(contains(rig.warning, "0x00000016"), "the warning spells the code it could not read");
        say(contains(rig.warning, "conversion target"),
            "and says what that code is on MSMF -- OpenCV's conversion target, not the camera");
        say(contains(rig.warning, "cannot be told from here"),
            "the claim is bounded: whether it is MJPG cannot be told from HERE");
    }

    std::printf("=== 2. a FOURCC of 0 keeps the camera the same way ===\n");
    {
        const autocam::FormatVerdict none = autocam::judgeProbedFormat(0);
        say(none.admitted, "a backend that reported no format at all does not reject");
        say(!none.is_mjpg, "and is not counted as verified MJPG either");
        say(none.reason.empty(), "no rejection");
        say(contains(none.warning, "reported no format at all"),
            "the warning names the silence");
        say(none.warning != autocam::judgeProbedFormat(0x00000016).warning,
            "0 and a conversion-target code are two different sentences");
    }

    std::printf("=== 3. a NAMED format that is not MJPG still rejects, by name ===\n");
    {
        // The bus-bandwidth reason stands: this is the one positive answer a rejection
        // can stand on, and three uncompressed 720p streams do not fit on one bus.
        const autocam::FormatVerdict fell_back = autocam::judgeProbedFormat(yuy2);
        say(!fell_back.admitted, "YUY2 is rejected");
        say(contains(fell_back.reason, "negotiated YUY2, not the MJPG that was requested"),
            "and the reason names it");
        say(fell_back.warning.empty(), "a rejected camera is not also warned about");

        const autocam::FormatVerdict third = autocam::judgeProbedFormat(nv12);
        say(!third.admitted, "NV12 is rejected too -- the rule is about being NAMED, not about YUY2");
    }

    std::printf("=== 4. the positive control: MJPG is kept with nothing to say ===\n");
    {
        const autocam::FormatVerdict kept = autocam::judgeProbedFormat(mjpg);
        say(kept.admitted && kept.is_mjpg, "MJPG is kept, verified");
        say(kept.warning.empty() && kept.reason.empty(),
            "a verified camera prints neither a warning nor a reason");
    }

    std::printf("\n%s (%d failed)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
