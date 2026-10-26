// turnaus#1904: OD_EXPOSURE / OD_GAIN, held pure -- the pins' parse, the set() calls each
// backend is given at open and their order, and the per-start line.
//
// Each assertion's label is what i1904_check.sh's mutations predict red:
//
//   parse:  unset and empty are no pin; "156", "-6" and "0.5" are values; "auto" is the
//           exposure's auto and is refused as a gain; "156abc", "abc", "inf" and "nan"
//           are refused, never half a pin
//   unset:  with OD_EXPOSURE and OD_GAIN unset the process reads no pin, and no pin (or a
//           refused one) plans no set() on either backend -- today's auto, untouched
//   v4l2:   a value plans auto_exposure=1 (V4L2_EXPOSURE_MANUAL) then exposure; auto
//           plans auto_exposure=3 (aperture priority)
//   msmf:   a value plans auto_exposure=0 (manual flag) then exposure; auto plans
//           auto_exposure=1
//   order:  on both backends the mode is set before the exposure (MSMF's mode set
//           writes its value as the exposure) and the gain comes last
//   file:   a video file plans nothing whatever the pins say
//   line:   the start line names the mode, asked and got, and says MSMF's mode is
//           not readable instead of reporting OpenCV's misread
//
//   compiled by i1904_check.sh against the tree and against mutated scratch copies.

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "exposure_pin.hpp"

using namespace exposure_pin;

static int failures = 0;

static void say(bool ok, const std::string &what)
{
    std::cout << (ok ? "OK   " : "FAIL ") << what << std::endl;
    if (!ok)
    {
        failures++;
    }
}

static bool contains(const std::string &s, const std::string &needle)
{
    return s.find(needle) != std::string::npos;
}

static bool isStep(const Set &s, int prop, double value)
{
    return s.prop == prop && s.value == value;
}

int main()
{
    const Pin none = pinFrom(nullptr, true);
    const Pin v156 = pinFrom("156", true);
    const Pin vMinus6 = pinFrom("-6", true);
    const Pin automatic = pinFrom("auto", true);
    const Pin g4 = pinFrom("4", false);
    const Pin refused = pinFrom("156abc", true);

    // ---- parse -------------------------------------------------------------------------
    say(none.kind == Pin::Unset && pinFrom("", true).kind == Pin::Unset, "parse: unset and empty are no pin");
    say(v156.kind == Pin::Value && v156.value == 156.0, "parse: \"156\" is 156");
    say(vMinus6.kind == Pin::Value && vMinus6.value == -6.0, "parse: \"-6\" is -6 (MSMF's log2 seconds)");
    say(pinFrom("0.5", false).kind == Pin::Value && pinFrom("0.5", false).value == 0.5, "parse: \"0.5\" is 0.5");
    say(automatic.kind == Pin::Auto, "parse: \"auto\" is the exposure's auto");
    say(pinFrom("auto", false).kind == Pin::Refused, "parse: \"auto\" is refused as a gain");
    say(refused.kind == Pin::Refused, "parse: \"156abc\" is refused, not 156");
    say(pinFrom("abc", true).kind == Pin::Refused, "parse: \"abc\" is refused");
    say(pinFrom("inf", true).kind == Pin::Refused && pinFrom("nan", true).kind == Pin::Refused,
        "parse: inf and nan are refused");

    // ---- unset -------------------------------------------------------------------------
    say(exposurePin().kind == Pin::Unset && gainPin().kind == Pin::Unset,
        "unset: with OD_EXPOSURE and OD_GAIN unset the process reads no pin");
    say(plan(Backend::V4L2, none, none).empty() && plan(Backend::MSMF, none, none).empty(),
        "unset: no pin plans no set() on V4L2 or MSMF");
    say(plan(Backend::V4L2, refused, pinFrom("x", false)).empty(), "unset: a refused pin plans no set()");
    say(!asksAnything(none, none) && !asksAnything(refused, refused), "unset: no pin and a refused pin ask nothing");

    // ---- v4l2 --------------------------------------------------------------------------
    {
        const std::vector<Set> p = plan(Backend::V4L2, v156, none);
        say(p.size() == 2 && isStep(p[0], cv::CAP_PROP_AUTO_EXPOSURE, 1.0) && isStep(p[1], cv::CAP_PROP_EXPOSURE, 156.0),
            "v4l2: 156 plans auto_exposure=1 (manual), exposure=156");
        const std::vector<Set> a = plan(Backend::V4L2, automatic, none);
        say(a.size() == 1 && isStep(a[0], cv::CAP_PROP_AUTO_EXPOSURE, 3.0), "v4l2: auto plans auto_exposure=3");
    }

    // ---- msmf --------------------------------------------------------------------------
    {
        const std::vector<Set> p = plan(Backend::MSMF, vMinus6, none);
        say(p.size() == 2 && isStep(p[0], cv::CAP_PROP_AUTO_EXPOSURE, 0.0) && isStep(p[1], cv::CAP_PROP_EXPOSURE, -6.0),
            "msmf: -6 plans auto_exposure=0 (manual), exposure=-6");
        const std::vector<Set> a = plan(Backend::MSMF, automatic, none);
        say(a.size() == 1 && isStep(a[0], cv::CAP_PROP_AUTO_EXPOSURE, 1.0), "msmf: auto plans auto_exposure=1");
    }

    // ---- order -------------------------------------------------------------------------
    for (Backend b : {Backend::V4L2, Backend::MSMF})
    {
        const std::vector<Set> p = plan(b, v156, g4);
        say(p.size() == 3 && p[0].prop == cv::CAP_PROP_AUTO_EXPOSURE && p[1].prop == cv::CAP_PROP_EXPOSURE &&
                isStep(p[2], cv::CAP_PROP_GAIN, 4.0),
            std::string("order: ") + backendName(b) + " sets mode, then exposure, then gain");
    }
    {
        const std::vector<Set> p = plan(Backend::V4L2, none, g4);
        say(p.size() == 1 && isStep(p[0], cv::CAP_PROP_GAIN, 4.0), "order: a gain alone is one set(), the gain");
    }

    // ---- file --------------------------------------------------------------------------
    say(plan(Backend::File, v156, g4).empty() && plan(Backend::File, automatic, none).empty(),
        "file: a video file plans nothing, pinned or not");

    // ---- line --------------------------------------------------------------------------
    {
        const std::string pinned = startLine(2, Backend::V4L2, v156, g4, 150, 4, 1);
        say(contains(pinned, "Camera 2 exposure: manual (OD_EXPOSURE), asked 156, got 150") &&
                contains(pinned, "mode reads manual") && contains(pinned, "gain asked 4, got 4") &&
                contains(pinned, "(V4L2)"),
            "line: pinned V4L2 -> " + pinned);
        const std::string own = startLine(1, Backend::V4L2, none, none, 312, -1, 3);
        say(contains(own, "Camera 1 exposure: camera's own (OD_EXPOSURE unset), reads 312") &&
                contains(own, "mode reads auto (V4L2 3)") && contains(own, "gain reads unreadable"),
            "line: unset V4L2 -> " + own);
        const std::string msmf = startLine(3, Backend::MSMF, vMinus6, none, -6, 0, 1);
        say(contains(msmf, "asked -6, got -6") && contains(msmf, "mode not readable on MSMF") &&
                !contains(msmf, "mode reads"),
            "line: MSMF -> " + msmf);
    }

    std::cout << "FAILURES: " << failures << std::endl;
    return failures == 0 ? 0 : 1;
}
