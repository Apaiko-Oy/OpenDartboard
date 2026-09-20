// #1317: the three guards, made to fire.
//
// The issue's subject is that all three could be deleted without failing anything: each
// asked `std::array<Point2f, 20>::size()`, which is the template argument, so `== 20` was
// always true and `< 16` and `< 20` were always false. A guard that cannot be made to
// fail is not a guard, so each one is fired here, beside a positive control on the same
// input with one wire more -- #708's shape: the needle is proved to exist before its
// absence means anything.
//
// The first guard is in wire_processing and needs a camera, a frame and a fitted doubles
// ring, so it is fired by the real detector on real footage in
// testers/phases1317/1317-partial.sh. What is asked HERE is the part of it that is pure:
// that WireEndpoints cannot be overrun from either side, which is what the copy at
// wire_processing.cpp:656 did by construction before this issue.
//
// The second and third are ordinary functions of a DartboardCalibration, so they are
// called with one. That is deliberate rather than convenient: since #1317 the calibration
// refuses a camera on the wire count at step 8, so nothing in the pipeline reaches these
// two with too few wires -- they guard a calibration that arrived another way (the cache,
// a blank slot kept for a camera that produced no frame, a future caller), and a test
// that could only reach them through the pipeline would be asserting the pipeline.
//
// #1447: section 3's positive control used to score a board whose orientation was never
// anchored, so `scorePoint` answered `dartboard_numbers[0]` for every tip on any ring and
// a working scorer and an absent one agreed perfectly. The fixture is anchored below and
// the control is now a census -- twenty tips, twenty different numbers -- which is the
// shape #1442 reached on its own fixture. The three refusals are untouched and are
// argued, one by one, at the site: each returns above the orientation gate.
//
//   i1317_guards        # prints one line per check, exits non-zero on any failure
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

/**
 * A calibration of a board whose bull is at (640, 360), with `wires` wire endpoints spaced
 * evenly around it. Everything else is what a camera that really saw a board would carry.
 */
static DartboardCalibration boardWith(int wires)
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

    // Wires every 18 degrees, starting at the top, as many as asked for.
    for (int i = 0; i < wires; i++)
    {
        const double angle = (-CV_PI / 2.0) + (i * 2.0 * CV_PI / wire_processing::kWiresRequired);
        calib.wires.wireEndpoints.add(cv::Point2f(
            640.0f + 200.0f * (float)cos(angle),
            360.0f + 200.0f * (float)sin(angle)));
    }
    calib.wires.wiresDetected = wires;
    calib.wires.camera_index = 0;
    calib.wires.isValid = (calib.wires.wireEndpoints.size() == (size_t)wire_processing::kWiresRequired);

    calib.orientation.camera_index = 0;
    calib.orientation.wedge20WireIndex = 0;
    calib.orientation.southWireIndex = 10;
    calib.orientation.isStarCamera = true;
    // #1447: ANCHORED, and this one line is what section 3 was missing. `scorePoint` sets
    // `wedge_measured = anchored && wedge20WireIndex >= 0`, and with `anchored` false --
    // its default, which nothing here used to set -- it takes #1346's path that ASSERTS
    // the 20: `sequence_slot = 0`, `dartboard_numbers[0]`, the same answer for every tip
    // on the board and on every ring behind it. So section 3's positive control scored a
    // board nobody was reading, and it would have passed just as well with the entire
    // wedge stage deleted. #1346's path is correct and is not what moves here; the
    // FIXTURE reaching it is the defect. The census below -- twenty tips, twenty
    // DIFFERENT numbers -- is what makes that state impossible to pass in, and it is the
    // control #1442 arrived at on its own fixture, for this same reason, by accident.
    calib.orientation.anchored = true;

    return calib;
}

/**
 * A tip in the middle of wedge `i` of the ring `boardWith` lays down: mid-wedge, and well
 * inside the doubles ring, which is a point the scorer answers with a segment whenever it
 * is allowed to answer at all. Wedge 0 is the one `wedge20WireIndex` names, so on a whole
 * ANCHORED board `midWedgeTip(i)` is slot `i` and `dartboard_numbers[i]` -- and on an
 * unanchored one every `i` is `dartboard_numbers[0]`, which is the distinction section 3
 * exists to be able to draw.
 */
static cv::Point2f midWedgeTip(int i)
{
    const double wedge = 2.0 * CV_PI / wire_processing::kWiresRequired;
    const double mid = (-CV_PI / 2.0) + i * wedge + wedge / 2.0;
    return cv::Point2f(640.0f + 120.0f * (float)cos(mid),
                       360.0f + 120.0f * (float)sin(mid));
}

int main()
{
    const int required = wire_processing::kWiresRequired;

    std::cout << "=== 0. the result can say how many wires it found, and cannot be overrun ===" << std::endl;
    {
        wire_processing::WireEndpoints ends;
        say(ends.size() == 0 && ends.empty(), "a fresh result holds nothing and says so (size=" +
                                                  std::to_string(ends.size()) + ")");

        for (int i = 0; i < 9; i++)
        {
            ends.add(cv::Point2f((float)i, (float)i));
        }
        say(ends.size() == 9, "nine wires added read back as nine, not as " + std::to_string(required) +
                                  " (size=" + std::to_string(ends.size()) + ")");

        int walked = 0;
        for (const cv::Point2f &p : ends)
        {
            (void)p;
            walked++;
        }
        say(walked == 9, "a range-for walks the nine that are there, not the store behind them (walked=" +
                             std::to_string(walked) + ")");

        int accepted = 0;
        wire_processing::WireEndpoints full;
        for (int i = 0; i < required + 7; i++)
        {
            if (full.add(cv::Point2f((float)i, (float)i)))
            {
                accepted++;
            }
        }
        say(accepted == required && full.size() == (size_t)required,
            "handed " + std::to_string(required + 7) + " wires it keeps " + std::to_string(required) +
                " and refuses the rest rather than writing past its end (accepted=" + std::to_string(accepted) + ")");
    }

    std::cout << "=== 2. the perspective guard fires (perspective_processing.cpp) ===" << std::endl;
    {
        const perspective_processing::DartboardSpec spec;

        const DartboardCalibration partial = boardWith(required - 1);
        const perspective_processing::RingIntersections refused =
            perspective_processing::findAllRingWireIntersections(partial, spec);
        say(!refused.isValid && refused.intersections.empty(),
            "a board with " + std::to_string(required - 1) + " wires gets no ring-wire intersections");

        const DartboardCalibration whole = boardWith(required);
        const perspective_processing::RingIntersections allowed =
            perspective_processing::findAllRingWireIntersections(whole, spec);
        say(allowed.isValid && allowed.intersections.size() == (size_t)required,
            "and the same board with " + std::to_string(required) + " gets " +
                std::to_string(allowed.intersections.size()) + " -- so the refusal is the count and nothing else");
    }

    std::cout << "=== 3. the scoring guard fires (score_processing.cpp) ===" << std::endl;
    {
        // A pixel in the middle of a wedge, well inside the double ring: a point the
        // scorer answers with a segment when it is allowed to answer at all. It is
        // wedge 0's, which is the tip this section always used.
        const cv::Point2f tip = midWedgeTip(0);

        const score_processing::PointScore whole = score_processing::scorePoint(tip, boardWith(required));
        // #1447: `wedge_measured` is added to this line and is the half that moved. The
        // other two clauses are unchanged and still pass -- but they passed BEFORE the
        // fixture was anchored too, off `dartboard_numbers[0]`, which is why they could
        // not tell a working scorer from an absent one. The answer's VALUE did not even
        // change: wedge 0 is the 20 either way, so the printed string is what it was.
        // That is the point rather than a coincidence -- an assertion on one tip's number
        // cannot distinguish a measured 20 from an asserted one, and the census below is
        // what does.
        say(whole.score != "MISS" && whole.segment > 0 && whole.wedge_measured,
            "the control: with " + std::to_string(required) + " wires that tip scores " + whole.score +
                ", off a wedge that was MEASURED rather than asserted");

        // The control's own control, and the half that cannot be faked. If the ring is
        // really being read, twenty mid-wedge tips get twenty DIFFERENT numbers; if it is
        // not -- an unanchored orientation, a wedge stage removed, `sequence_slot` pinned
        // at 0 -- every one of them is `dartboard_numbers[0]` and the answers are twenty
        // copies of one string. Both states satisfy "answered with a segment", which is
        // the whole of what this section used to ask.
        const DartboardCalibration clean = boardWith(required);
        std::vector<std::string> answers;
        int scored = 0;
        for (int i = 0; i < required; i++)
        {
            const score_processing::PointScore p = score_processing::scorePoint(midWedgeTip(i), clean);
            answers.push_back(p.score);
            if (p.score != "MISS" && p.segment > 0)
            {
                scored++;
            }
        }
        say(scored == required, "a clean board answers all " + std::to_string(scored) + " of " +
                                    std::to_string(required) + " mid-wedge tips with a segment");

        std::vector<std::string> distinct = answers;
        std::sort(distinct.begin(), distinct.end());
        distinct.erase(std::unique(distinct.begin(), distinct.end()), distinct.end());
        say((int)distinct.size() == required,
            "and it gives them " + std::to_string(distinct.size()) + " different numbers, not " +
                std::to_string(required) + " copies of one -- so the wedge really is read off the ring, and "
                "this section can tell a working scorer from an absent one");

        // #1447: both refusals below are the assertions they always were, and the
        // anchoring cannot have reached them. `scorePoint` refuses on
        // `!hasValidDoubles || !wires.wholeRing()` and RETURNS THERE -- above the ring
        // stage, above the orientation gate, above `wedge_measured`, which is computed
        // some eighty lines further down and only on calibrations that got past it.
        // `wholeRing()` asks `wiresDetected`, a field `anchored` does not touch. So each
        // is reached by the identical branch for the identical reason, and neither can
        // have started passing because the numbers are now distinct: what they assert is
        // a default-constructed PointScore, which is MISS and segment -1 -- never a
        // number at all, distinct or otherwise. `wedge_asserted` is asked beside each as
        // the proof of WHERE the call came back from: false says the wedge stage was
        // never entered, where an asserted 20 would set it true at the site.
        const score_processing::PointScore partial = score_processing::scorePoint(tip, boardWith(required - 1));
        say(partial.score == "MISS" && partial.segment == -1 && !partial.wedge_asserted && !partial.wedge_measured,
            "with " + std::to_string(required - 1) + " wires the same tip is refused, not scored (" +
                partial.score + ") -- and refused by the count, above the wedge stage, which it never entered");

        // The blank slot #1318 keeps for a camera that produced no frame. Before this
        // issue its wire count read 20 like everything else and findWedgeSlot took
        // `(start + i) % wires` with wires == 20 over twenty default-constructed points;
        // with a real count it is `% 0`, and this guard is what stops the division.
        const score_processing::PointScore blank = score_processing::scorePoint(tip, boardWith(0));
        say(blank.score == "MISS" && blank.segment == -1 && !blank.wedge_asserted && !blank.wedge_measured,
            "and a blank calibration -- no frame, no wires -- is refused rather than divided by (" +
                blank.score + "), by that same count and in that same place");
    }

    std::cout << (failures == 0 ? "GUARDS_RC=0" : "GUARDS_RC=1") << std::endl;
    return failures == 0 ? 0 : 1;
}
