// #1501: board_recognition::recognise, held without a detector run. Every verdict it can
// reach, from OrientationData a real calibration can leave behind, and the two things the
// maintainer's decision requires of the words: an unrecognised board is a WARNING that names
// OD_CAMERA_WEDGES, and every sentence names the one supported board.
#include "board_recognition.hpp"

#include <cstdio>
#include <string>
#include <vector>

using orientation_processing::CameraPosition;
using orientation_processing::OrientationData;

static int failures = 0;
static void check(bool ok, const std::string &what)
{
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok)
    {
        failures++;
    }
}

static bool has(const std::string &s, const std::string &needle) { return s.find(needle) != std::string::npos; }

static OrientationData camera(bool read, float separation, bool star = false,
                              CameraPosition position = CameraPosition::UNKNOWN, bool anchored = false)
{
    OrientationData o;
    o.southWireIndex = 3;
    o.numbersRead = read;
    o.numberSeparation = separation;
    o.isStarCamera = star;
    o.cameraPosition = position;
    o.anchored = anchored;
    o.wedge20WireIndex = anchored ? 7 : -1;
    return o;
}

int main()
{
    using board_recognition::Verdict;

    // The Blade 6's shape: numbers read, no clips.
    {
        const auto r = board_recognition::recognise(
            {camera(true, 4.28f, false, CameraPosition::READ, true), camera(false, 2.10f),
             camera(true, 3.50f, false, CameraPosition::READ, true)}, true, 2.75);
        std::printf("     %s\n", r.sentence.c_str());
        check(r.verdict == Verdict::SupportedShape && !r.warn, "numbers read and no wire ring: the supported board's shape, at INFO");
        check(has(r.sentence, "Winmau Blade 6") && has(r.sentence, "2 of 3 cameras"), "it names the board and how many cameras read it");
        check(has(r.sentence, "camera 2 2.10") && has(r.sentence, "cut 2.75"), "it says what each ring measured and the cut");
    }

    // The Unicorn mocks' shape: numbers read AND a star camera.
    {
        const auto r = board_recognition::recognise(
            {camera(true, 3.75f, false, CameraPosition::READ, true), camera(true, 4.12f, true, CameraPosition::MIDDLE, true),
             camera(true, 3.52f, false, CameraPosition::READ, true)}, true, 2.75);
        std::printf("     %s\n", r.sentence.c_str());
        check(r.verdict == Verdict::AnotherBoard && !r.warn, "numbers read beside a wire number ring: another board, best-effort, at INFO");
        check(has(r.sentence, "NOT the Winmau Blade 6") && has(r.sentence, "camera 2 also found"), "it says it is not the supported board, and which camera found the clips");
    }

    // Unrecognised: no camera read the numbers, nothing configured.
    {
        const auto r = board_recognition::recognise({camera(false, 1.94f), camera(false, 1.62f), camera(false, 0.0f)}, true, 2.75);
        std::printf("     %s\n", r.sentence.c_str());
        check(r.verdict == Verdict::NotRecognised && r.warn, "no camera read the numbers: NOT recognised, as a WARNING");
        check(has(r.sentence, "NOT recognised") && has(r.sentence, "REMEDY: set OD_CAMERA_WEDGES"), "the remedy is named in the same sentence");
        check(has(r.sentence, "camera 3 -"), "a camera whose ring was never sampled reads '-', not 0.00");
    }

    // Unrecognised, but a star camera anchors: still a warning, and says why the board is anchored at all.
    {
        const auto r = board_recognition::recognise({camera(false, 2.40f, true, CameraPosition::MIDDLE, true), camera(false, 1.9f), camera(false, 1.8f)}, true, 2.75);
        check(r.verdict == Verdict::NotRecognised && r.warn && has(r.sentence, "camera 1 found the four clip wires"),
              "unread numbers beside a wire ring: NOT recognised, a WARNING that says the clips anchor it");
    }

    // Unrecognised with the remedy already in effect: said, at INFO.
    {
        const auto r = board_recognition::recognise(
            {camera(false, 1.94f, false, CameraPosition::CONFIGURED, true), camera(false, 1.62f), camera(false, 1.78f)}, true, 2.75);
        std::printf("     %s\n", r.sentence.c_str());
        check(r.verdict == Verdict::NotRecognised && !r.warn, "OD_CAMERA_WEDGES already states an anchor: still NOT recognised, said at INFO");
        check(has(r.sentence, "OD_CAMERA_WEDGES states the anchor on 1 of 3 cameras"), "and it says the remedy is in effect, on how many cameras");
    }

    // The reader switched off: nothing was measured and that is what is said.
    {
        const auto r = board_recognition::recognise({camera(false, 0.0f), camera(false, 0.0f), camera(false, 0.0f)}, false, 2.75);
        check(r.verdict == Verdict::NotAsked && !r.warn && has(r.sentence, "OD_NUMBER_ANCHOR=off"),
              "OD_NUMBER_ANCHOR=off: not attempted, never 'not recognised'");
    }

    // #1676: forced to the Blade 6 (the default). The verdict is Forced, whatever the shape.
    {
        OrientationData aside = camera(true, 4.19f, false, CameraPosition::READ, true);
        aside.starSetAside = true;
        const auto r = board_recognition::recognise(
            {camera(true, 4.28f, false, CameraPosition::READ, true), aside, camera(true, 3.70f, false, CameraPosition::READ, true)},
            true, 2.75, true);
        std::printf("  %s\n", r.sentence.c_str());
        check(r.verdict == Verdict::Forced && r.setAside == 1 && r.wireRing == 0,
              "forced: a set-aside star is counted as set aside, not as a wire ring");
        check(r.warn && has(r.sentence, "taken as the Winmau Blade 6") && has(r.sentence, "(forced") &&
                  has(r.sentence, "camera 2 reported the four clip wires") && has(r.sentence, "OD_BOARD=auto"),
              "forced with a set-aside star: a WARNING about the finder that names the camera and the pin");
    }
    {
        const auto r = board_recognition::recognise(
            {camera(true, 4.28f, false, CameraPosition::READ, true), camera(true, 4.53f, false, CameraPosition::READ, true), camera(false, 1.2f)},
            true, 2.75, true);
        check(r.verdict == Verdict::Forced && !r.warn && !has(r.sentence, "clip-wire finder"),
              "forced, no star anywhere: said at INFO, with no finder warning");
    }
    {
        const auto r = board_recognition::recognise({camera(false, 1.9f), camera(false, 1.8f), camera(false, 0.0f)}, true, 2.75, true);
        check(r.verdict == Verdict::Forced && r.warn && has(r.sentence, "REMEDY: set OD_CAMERA_WEDGES"),
              "forced, nothing read and nothing configured: a WARNING naming the remedy");
    }
    {
        OrientationData aside = camera(false, 0.0f);
        aside.starSetAside = true;
        const auto r = board_recognition::recognise({aside, camera(false, 0.0f), camera(false, 0.0f)}, false, 2.75, true);
        check(r.verdict == Verdict::NotAsked && r.warn && has(r.sentence, "OD_NUMBER_ANCHOR=off") &&
                  has(r.sentence, "camera 1 reported the four clip wires"),
              "forced, reader off: not attempted, and the set-aside star is still a WARNING");
    }

    // Every sentence carries one greppable prefix.
    std::printf("I1501CHECK failures=%d\n", failures);
    return failures == 0 ? 0 : 1;
}
