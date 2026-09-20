// #1451: whether a point can be SCORED from a camera, on its own, and the census built on
// it -- including the one board the detector tester cannot build out of shipped footage.
//
// `score_processing::canScoreAPoint` is `scorePoint`'s own guard, extracted so the startup
// census and the scorer ask one expression. The startup census used to ask
// `sees_board && hasValidDoubles` and never the wire ring, so a camera the scorer refuses
// was counted a full voter and then contributed nothing at every dart, in silence -- the
// refusal is `log_debug`. This check holds the extracted expression to the guard it came
// from, and holds the count and the per-camera naming to it.
//
// WHY THE ZERO CASE IS HERE RATHER THAN IN testers/i1451_run.sh. The detector tester builds
// its board from a real cache, which is the only door into this state -- a freshly
// calibrated camera whose ring is not whole is refused by `calibrateSingleCamera` and never
// reaches the census. Measured on the shipped fixtures: `mocks/rig-20260918/cam_3.mp4` is
// the only footage in the repository whose wire stage finds more than twenty, and it finds
// twenty-one in the THIRD slot only -- three slots filled from that one file calibrate 20,
// 20, 21, because the three captures do not open on the same frame. So no cache this
// repository can write holds three rings that are all refused, and a board where the count
// is ZERO cannot be built from footage at all. It is built here instead, out of structs,
// where it is exact and deterministic; the detector tester proves the same expression on a
// board the program really becomes, at 2 of 3.
//
//   g++ -std=c++17 -I src -I src/utils -o scoring_check
//       testers/i1451_scoring_check.cpp $(pkg-config --cflags --libs opencv4)

#include <iostream>
#include <string>
#include <vector>

#include "detector/geometry/detection/score_processing.hpp"

using score_processing::aDartIsScoredFrom;
using score_processing::camerasThatCanScoreAPoint;
using score_processing::canScoreAPoint;
using score_processing::howItScores;
using score_processing::namingEachCamera;

static int failures = 0;

static void say(bool ok, const std::string &what)
{
    std::cout << (ok ? "OK   " : "FAIL ") << what << std::endl;
    if (!ok)
    {
        failures++;
    }
}

/**
 * A calibration carrying only the four fields this question is asked of. `wiresDetected`
 * is what decides (#1442) and `wireEndpoints` deliberately is not, so this helper fills
 * the deciding field and leaves the store empty -- which is also the shape a cache written
 * by an older binary arrives in.
 */
static DartboardCalibration cameraWith(bool sees_board, bool doubles, int wires_detected)
{
    DartboardCalibration calib;
    calib.sees_board = sees_board;
    calib.ellipses.hasValidDoubles = doubles;
    calib.wires.wiresDetected = wires_detected;
    return calib;
}

static bool mentions(const std::string &haystack, const std::string &needle)
{
    return haystack.find(needle) != std::string::npos;
}

int main()
{
    const int WHOLE = wire_processing::kWiresRequired;

    // ---- the guard, one expression, both halves and both sides ------------------------
    say(canScoreAPoint(cameraWith(true, true, WHOLE)),
        "a fitted doubles ring and a whole wire ring is a camera a dart is scored from");
    say(!canScoreAPoint(cameraWith(true, false, WHOLE)),
        "no fitted doubles ring: refused, however whole the wire ring is");
    say(!canScoreAPoint(cameraWith(true, true, WHOLE - 1)),
        "nineteen wires: refused -- every wedge past the gap would be the wrong segment");
    say(!canScoreAPoint(cameraWith(true, true, WHOLE + 2)),
        "twenty-two wires: refused too, which is #1442's half and is the same fault");

    // The one this issue is about. `sees_board` is not the question and must not become
    // it: a cached calibration carries `sees_board` true over a ring the guard refuses,
    // which is the entire state #1451 was filed about.
    say(!canScoreAPoint(cameraWith(true, true, WHOLE + 1)),
        "a camera that IS looking at the dartboard is still refused on a long ring (#1451)");
    // ---- the inner guard is not the census's question, and the difference is #1451 ----
    // `scorePoint` never asks `sees_board`, correctly: `processScore` asks it first and
    // abstains the camera by name. So the inner guard SAYS YES to a camera that is not
    // looking at the board, and a census calling it alone would count a camera the scorer
    // abstains -- this issue one field further on. #1372 makes that a real board rather
    // than a hypothetical one: a cached camera that produced no frame this start keeps its
    // cached ring and its doubles and loses only `sees_board`.
    const DartboardCalibration no_frame = cameraWith(false, true, WHOLE);
    say(canScoreAPoint(no_frame),
        "the INNER guard says yes to a no-frame camera -- it never asks sees_board, and should not");
    say(!aDartIsScoredFrom(no_frame),
        "but no dart is scored from it, because processScore abstains it first (#1372)");
    say(aDartIsScoredFrom(cameraWith(true, true, WHOLE)),
        "and a camera that is looking at the board with a whole ring is scored from");

    // ---- the count -------------------------------------------------------------------
    const std::vector<DartboardCalibration> whole = {
        cameraWith(true, true, WHOLE), cameraWith(true, true, WHOLE), cameraWith(true, true, WHOLE)};
    const std::vector<DartboardCalibration> stale = {
        cameraWith(true, true, WHOLE), cameraWith(true, true, WHOLE), cameraWith(true, true, WHOLE + 1)};
    const std::vector<DartboardCalibration> none = {
        cameraWith(true, true, WHOLE + 1), cameraWith(true, true, WHOLE + 1), cameraWith(true, true, WHOLE + 2)};

    // A board of three cached cameras that produced no frame this start: every one of
    // them holds a whole ring and none of them is scored from. This is the count the
    // earlier draft got wrong, and it is why the census asks the conjunction.
    const std::vector<DartboardCalibration> silent = {
        cameraWith(false, true, WHOLE), cameraWith(false, true, WHOLE), cameraWith(false, true, WHOLE)};
    say(camerasThatCanScoreAPoint(silent) == 0,
        "three cached cameras with whole rings and no frames count ZERO, not three (#1372)");

    say(camerasThatCanScoreAPoint(whole) == 3, "a healthy board counts three");
    say(camerasThatCanScoreAPoint(stale) == 2,
        "the board this issue is about counts TWO while calibrating on three (#1451)");
    say(camerasThatCanScoreAPoint(none) == 0,
        "a board no camera can be scored from counts zero -- the count the WARN fires on");
    say(camerasThatCanScoreAPoint({}) == 0, "no cameras is no scorable cameras, and not a crash");

    // ---- the per-camera naming, which is what an operator actually reads --------------
    // #1389 / ADR-0081 section 3: never a count alone. Every camera in its own slot, on
    // every board, including the ones where nothing is wrong.
    for (const auto &board : {whole, stale, none})
    {
        const std::string named = namingEachCamera(board);
        const bool all_three = mentions(named, "camera 1: ") && mentions(named, "camera 2: ") &&
                               mentions(named, "camera 3: ");
        say(all_three, "every camera is named in its own slot, whatever the count");
    }
    say(namingEachCamera({}).empty(), "and a board with no cameras names nobody");

    // The reason has to send the reader to the right place, which is the whole of #1389.
    // A camera that IS looking at the dartboard and still cannot be scored from did not
    // calibrate this start -- it came off the cache -- so the remedy is the cache and not
    // the rig, and that distinction is the only actionable thing in the sentence.
    const std::string long_ring = howItScores(cameraWith(true, true, WHOLE + 1));
    say(mentions(long_ring, "21 of the 20"),
        "a long ring states its count against the threshold that refused it (#1321)");
    say(mentions(long_ring, "delete cache/"),
        "and sends the reader to the cache, which is the only door into this state");

    const std::string no_doubles = howItScores(cameraWith(true, false, WHOLE));
    say(mentions(no_doubles, "no fitted doubles ring"),
        "an unfitted doubles ring is named as itself, not as a wire problem");
    say(!mentions(no_doubles, "delete cache/"),
        "and is NOT sent to the cache -- that camera's remedy is the lighting and the aim");

    const std::string blind = howItScores(cameraWith(false, true, WHOLE));
    say(mentions(blind, "not looking at the dartboard"),
        "a camera with no frame is named as that FIRST, whole ring or not -- processScore's own order");
    say(!mentions(blind, "wire boundaries a board has"),
        "and is not reported as a ring problem, which would send the reader to the wrong place");

    // #1321 applies to the healthy sentence too. An earlier draft said "all 20" as a
    // constant, so a camera really holding twenty-one -- which is what a board running
    // under OD_WIRE_COUNT=atleast holds -- was reported as holding all twenty. A census
    // whose healthy sentence cannot report the number that decided it hides the reading
    // this issue is about, and this is the assertion that caught it.
    say(mentions(howItScores(cameraWith(true, true, WHOLE)), "20 of the 20"),
        "a scorable camera states its real wire count, not the constant");
    say(mentions(howItScores(cameraWith(true, true, WHOLE + 1)), "21"),
        "and the count it states is the camera's own, so a long ring cannot read as clean");

    std::cout << "failures=" << failures << std::endl;
    return failures == 0 ? 0 : 1;
}
