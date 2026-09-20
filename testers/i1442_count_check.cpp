// #1442: a camera proposing more than twenty wire boundaries is not a clean twenty.
//
// The issue is that `isValid` compared the SIZE OF A STORE bounded at twenty against
// twenty, and both guards downstream asked the same store `< kWiresRequired`. A store
// that cannot hold twenty-one can read short and can never read long, so all three were
// one-sided by construction: a camera proposing twenty-two filled it to twenty, had the
// rest refused by `WireEndpoints::add`, and passed every one of them.
//
// What is asked here is the part of that which is PURE -- a count, three guards and a
// dart -- so it needs no footage, no detector and no clock. The measured half, over both
// fixtures and all six clips, is phases1442/1442-count.sh beside this file.
//
//   i1442_count_check two-sided     what this binary does by default
//   i1442_count_check at-least      what it does under OD_WIRE_COUNT=atleast
//
// The argument it is told to expect is not a convenience. `anyCountFromTwentyUpIsAWholeRing`
// reads its environment once into a function-local static, the way every OD_ switch in
// this tree does, so one process is one mode and the two conditions are two runs. The
// phase script runs it four ways -- each mode said truly, a typo'd value that must read
// two-sided, and one deliberate MISMATCH that must FAIL -- because a check that has never
// been shown to fail proves nothing, and a switch that only ever refuses passes any test
// that asks it to refuse (#1340).
//
// Prints one line per check; exits non-zero on any failure.
#include <opencv2/opencv.hpp>
#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

#include "geometry_calibration.hpp"
#include "perspective_processing.hpp"
#include "score_processing.hpp"

static int failures = 0;

static void say(bool ok, const std::string &what)
{
    std::cout << (ok ? "OK   " : "FAIL ") << what << std::endl;
    if (!ok)
    {
        failures++;
    }
}

static std::string yesno(bool v) { return v ? "yes" : "no"; }

/** The twenty boundaries a board really has, sorted by angle the way the ensemble returns them. */
static std::vector<double> trueRing()
{
    std::vector<double> angles;
    for (int i = 0; i < wire_processing::kWiresRequired; i++)
    {
        double a = -CV_PI / 2.0 + i * 2.0 * CV_PI / wire_processing::kWiresRequired;
        while (a >= CV_PI)
        {
            a -= 2.0 * CV_PI;
        }
        while (a < -CV_PI)
        {
            a += 2.0 * CV_PI;
        }
        angles.push_back(a);
    }
    std::sort(angles.begin(), angles.end());
    return angles;
}

/**
 * The board's twenty plus `extra` boundaries no board has, each halfway across a wedge at
 * the START of the angular sweep -- which is what the number ring, a wire's far end or a
 * bright wall looks like to a stage that groups candidates by angle and then sorts them
 * (findWiresByEnsemble, "Sort by angle").
 *
 * Placing them low is the whole point rather than an arbitrary choice: `add()` keeps the
 * first twenty of what it is handed, so with the extras low the kept twenty are the two
 * spurious ones PLUS the eighteen lowest real ones, and the two real boundaries at the top
 * of the sweep are the ones dropped. That is the ring with a double-width gap in it that
 * `kWiresRequired` says must not be scored through, arrived at from above instead of below.
 */
static std::vector<double> ringWithExtras(int extra)
{
    const std::vector<double> real = trueRing();
    std::vector<double> angles = real;
    const double wedge = 2.0 * CV_PI / wire_processing::kWiresRequired;
    for (int i = 0; i < extra; i++)
    {
        angles.push_back(real[i] + wedge / 2.0);
    }
    std::sort(angles.begin(), angles.end());
    return angles;
}

/**
 * A calibration of a board whose bull is at (640, 360), filled from `proposed` exactly the
 * way `processWires` fills one: every endpoint pushed in turn, `add()` refusing whatever
 * will not fit, and `wiresDetected` recording what the detector really returned. Everything
 * else is what a camera that really saw a board would carry. (The board itself is
 * #1317's boardWith, which is the shape this repository's testers already state a board in.)
 */
static DartboardCalibration boardFrom(const std::vector<double> &proposed)
{
    DartboardCalibration calib;
    calib.camera_index = 0;
    calib.capture_width = 1280;
    calib.capture_height = 720;
    calib.bullCenter = cv::Point(640, 360);
    calib.frameCenter = cv::Point(640, 360);
    calib.sees_board = true;

    calib.ellipses.hasValidDoubles = true;
    calib.ellipses.validOuterPoints = 108;
    calib.ellipses.validInnerPoints = 96;
    calib.ellipses.outerDoubleEllipse = cv::RotatedRect(cv::Point2f(640, 360), cv::Size2f(400, 400), 0.0f);
    calib.ellipses.innerDoubleEllipse = cv::RotatedRect(cv::Point2f(640, 360), cv::Size2f(376, 376), 0.0f);
    calib.ellipses.outerTripleEllipse = cv::RotatedRect(cv::Point2f(640, 360), cv::Size2f(248, 248), 0.0f);
    calib.ellipses.innerTripleEllipse = cv::RotatedRect(cv::Point2f(640, 360), cv::Size2f(224, 224), 0.0f);
    calib.ellipses.outerBullEllipse = cv::RotatedRect(cv::Point2f(640, 360), cv::Size2f(64, 64), 0.0f);
    calib.ellipses.innerBullEllipse = cv::RotatedRect(cv::Point2f(640, 360), cv::Size2f(26, 26), 0.0f);
    calib.ellipses.hasValidTriples = true;
    calib.ellipses.hasValidBulls = true;
    calib.ellipses.hasDetectedEllipses = true;

    for (double a : proposed)
    {
        calib.wires.wireEndpoints.add(cv::Point2f(
            640.0f + 200.0f * (float)cos(a),
            360.0f + 200.0f * (float)sin(a)));
    }
    calib.wires.wiresDetected = (int)proposed.size();
    calib.wires.camera_index = 0;
    calib.wires.isValid = calib.wires.wholeRing();

    calib.orientation.camera_index = 0;
    calib.orientation.wedge20WireIndex = 0;
    calib.orientation.southWireIndex = 10;
    calib.orientation.isStarCamera = true;
    // ANCHORED, and this line is load-bearing rather than decorative. `scorePoint` sets
    // `wedge_measured = anchored && wedge20WireIndex >= 0`, and with it false it takes the
    // #1346 path that ASSERTS the 20 -- `sequence_slot = 0`, `dartboard_numbers[0]`, the
    // same answer for every tip on the board and for every ring behind it. Section D was
    // written without this line first and reported "0 of 20 tips given a different
    // number": a clean board and a ring truncated from twenty-two agreed perfectly,
    // because neither was being read. An assertion that passes by not looking, met while
    // writing the thing that is supposed to catch them. The control in D -- twenty tips,
    // twenty DIFFERENT numbers -- is what now makes that state impossible to pass in.
    calib.orientation.anchored = true;

    return calib;
}

/** One tip per wedge, mid-wedge and well inside the doubles ring. */
static std::vector<cv::Point2f> tipsAroundTheBoard()
{
    std::vector<cv::Point2f> tips;
    const std::vector<double> ring = trueRing();
    const double wedge = 2.0 * CV_PI / wire_processing::kWiresRequired;
    for (double a : ring)
    {
        const double mid = a + wedge / 2.0;
        tips.push_back(cv::Point2f(640.0f + 120.0f * (float)cos(mid),
                                   360.0f + 120.0f * (float)sin(mid)));
    }
    return tips;
}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        std::cerr << "usage: i1442_count_check <two-sided|at-least>" << std::endl;
        return 2;
    }
    const std::string mode = argv[1];
    if (mode != "two-sided" && mode != "at-least")
    {
        std::cerr << "i1442_count_check: '" << mode << "' is neither two-sided nor at-least" << std::endl;
        return 2;
    }
    // What this binary is being told it should be. `at-least` is the behaviour every
    // commit before #1442 had, restored by OD_WIRE_COUNT=atleast; `two-sided` is what it
    // is now. Over-counting is a whole ring in the first and is not in the second, and
    // NEITHER of them accepts nineteen or admits a blank calibration.
    const bool overCountingPasses = (mode == "at-least");
    const int required = wire_processing::kWiresRequired;

    std::cout << "=== the mode under test: " << mode << " (OD_WIRE_COUNT="
              << (getenv("OD_WIRE_COUNT") ? getenv("OD_WIRE_COUNT") : "<unset>") << ") ===" << std::endl;

    std::cout << "=== A. the one question, asked of a count ===" << std::endl;
    {
        // Under twenty is refused in both modes and always was; that half is #1317's and
        // is here as the control that says this check is looking at the right thing.
        const int tooFew[] = {0, 1, 9, 17, 19};
        bool allRefused = true;
        for (int n : tooFew)
        {
            allRefused = allRefused && !wire_processing::isAWholeRing(n);
        }
        say(allRefused, "0, 1, 9, 17 and 19 are not a whole ring, in either mode -- #1317's half, unmoved");

        say(wire_processing::isAWholeRing(required),
            "exactly " + std::to_string(required) + " IS a whole ring, in either mode -- the regression this could "
            "most easily cause, and it has not happened");

        const int tooMany[] = {21, 22, 27};
        bool allAgree = true;
        std::string seen;
        for (int n : tooMany)
        {
            const bool whole = wire_processing::isAWholeRing(n);
            allAgree = allAgree && (whole == overCountingPasses);
            seen += " " + std::to_string(n) + "=" + yesno(whole);
        }
        say(allAgree, "21, 22 and 27 read '" + yesno(overCountingPasses) + "' as this mode says they must (" +
                          seen + " )");
    }

    std::cout << "=== B. the shape the pipeline really produces ===" << std::endl;
    const DartboardCalibration clean = boardFrom(trueRing());
    const DartboardCalibration truncated = boardFrom(ringWithExtras(2));
    {
        // The needle, proved to exist before its absence means anything (#708): the two
        // calibrations are INDISTINGUISHABLE by the thing all three guards used to ask.
        // If this ever stops being true, everything below it is measuring two different
        // stores rather than one question asked two ways.
        say(truncated.wires.wireEndpoints.size() == clean.wires.wireEndpoints.size() &&
                truncated.wires.wireEndpoints.size() == (size_t)required,
            "a camera proposing " + std::to_string(truncated.wires.wiresDetected) + " and one proposing " +
                std::to_string(clean.wires.wiresDetected) + " both store " +
                std::to_string(truncated.wires.wireEndpoints.size()) +
                " endpoints -- which is why `size() < kWiresRequired` could never catch one of them");

        say(truncated.wires.wiresDetected == required + 2 && clean.wires.wiresDetected == required,
            "and the count the ensemble returned survives on the calibration: " +
                std::to_string(truncated.wires.wiresDetected) + " against " +
                std::to_string(clean.wires.wiresDetected));

        say(clean.wires.wholeRing() && clean.wires.isValid,
            "the clean board is a whole ring and calibrates, in either mode");

        say(truncated.wires.wholeRing() == overCountingPasses && truncated.wires.isValid == overCountingPasses,
            "the truncated one reads whole=" + yesno(truncated.wires.wholeRing()) + ", which is what " + mode +
                " says it must");
    }

    std::cout << "=== C. the perspective guard (perspective_processing.cpp) ===" << std::endl;
    {
        const perspective_processing::DartboardSpec spec;

        const perspective_processing::RingIntersections whole =
            perspective_processing::findAllRingWireIntersections(clean, spec);
        say(whole.isValid && whole.intersections.size() == (size_t)required,
            "the control: a clean board gets its " + std::to_string(whole.intersections.size()) +
                " ring-wire intersections in either mode");

        const perspective_processing::RingIntersections over =
            perspective_processing::findAllRingWireIntersections(truncated, spec);
        say(over.isValid == overCountingPasses,
            "a board proposing " + std::to_string(truncated.wires.wiresDetected) + " gets intersections=" +
                yesno(over.isValid) + ", which is what " + mode + " says it must");
    }

    std::cout << "=== D. the scoring guard, and the number the silence was giving out ===" << std::endl;
    {
        const std::vector<cv::Point2f> tips = tipsAroundTheBoard();

        int cleanScored = 0;
        std::vector<std::string> cleanAnswers;
        for (const cv::Point2f &tip : tips)
        {
            const score_processing::PointScore p = score_processing::scorePoint(tip, clean);
            cleanAnswers.push_back(p.score);
            if (p.score != "MISS" && p.segment > 0)
            {
                cleanScored++;
            }
        }
        say(cleanScored == (int)tips.size(),
            "the control: a clean board answers all " + std::to_string(cleanScored) + " of " +
                std::to_string(tips.size()) + " mid-wedge tips with a segment, in either mode");

        // The control's own control, and it is the half that cannot be faked. If the ring
        // is really being READ, twenty mid-wedge tips get twenty DIFFERENT numbers; if the
        // orientation is unanchored, `scorePoint` asserts the 20 for every one of them and
        // the answers are twenty copies of one string. Both states satisfy "answered with
        // a segment", which is how the first draft of this section agreed with itself.
        std::vector<std::string> distinct = cleanAnswers;
        std::sort(distinct.begin(), distinct.end());
        distinct.erase(std::unique(distinct.begin(), distinct.end()), distinct.end());
        say(distinct.size() == tips.size(),
            "and it gives them " + std::to_string(distinct.size()) + " different numbers, not " +
                std::to_string(tips.size()) + " copies of one -- so the wedge really is read off the ring");

        int refused = 0;
        int disagreed = 0;
        std::string firstDisagreement;
        for (size_t i = 0; i < tips.size(); i++)
        {
            const score_processing::PointScore p = score_processing::scorePoint(tips[i], truncated);
            if (p.score == "MISS" && p.segment == -1)
            {
                refused++;
                continue;
            }
            if (p.score != cleanAnswers[i])
            {
                disagreed++;
                if (firstDisagreement.empty())
                {
                    firstDisagreement = cleanAnswers[i] + " read as " + p.score;
                }
            }
        }

        if (!overCountingPasses)
        {
            say(refused == (int)tips.size(),
                "every one of the " + std::to_string(refused) + " tips is refused rather than scored through a ring "
                "of " + std::to_string(required) + " kept from " + std::to_string(truncated.wires.wiresDetected));
        }
        else
        {
            // This is why the silence was worth a slice. It is not that the board was
            // "less accurate" through a truncated ring; it is that the same dart in the
            // same place is given a DIFFERENT NUMBER, with nothing in the answer marking
            // it as less trustworthy than the clean board's.
            say(refused == 0 && disagreed > 0,
                "with the pre-#1442 test restored the truncated ring scores " +
                    std::to_string((int)tips.size() - refused) + " of " + std::to_string(tips.size()) +
                    " tips and gives " + std::to_string(disagreed) +
                    " of them a different number than the clean board does (" + firstDisagreement + ")");
        }
    }

    std::cout << (failures == 0 ? "I1442_RC=0" : "I1442_RC=1") << std::endl;
    return failures == 0 ? 0 : 1;
}
