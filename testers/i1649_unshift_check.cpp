// #1649: the fresh-diff morphology translates the figure, and the axis can undo it.
//
// Every figure here is rasterised, so the true line is arithmetic. Four things are
// asserted:
//
//   1. THE CHAIN TRANSLATES. dart_processing.cpp's four passes -- CLOSE and OPEN with a
//      k x k rectangle, then CLOSE and OPEN with a (k/2) x (k/2) one -- are run on a
//      built bar for k = 3..6, and the kept figure's centroid moves by exactly
//      supportChainShiftPx(k) in x and in y. At the rig's k = 4 that is (+4, +4) px.
//   2. THE COPY HOLDS. shaft_axis::kSupportMorphKernel equals
//      DartParams{}.morph_kernel_size, so the arithmetic in shaft_axis.hpp is about
//      the kernel dart_processing really uses.
//   3. THE AXIS INHERITS IT, AND THE CORRECTION REMOVES IT. A rod at the rigs' shaft
//      angles (+80 and -75 deg), put through the chain, fits a line that lies
//      4(|n_x| + n_y) px image-right of the rod's true centreline (within 0.35 px). With
//      support_shift_px = supportChainShiftPx(k) the same figure fits within 0.35 px of
//      the true line, at exactly the uncorrected fit's angle.
//   4. THE PIN. An AxisParams made with OD_AXIS_UNSHIFT unset carries no shift, so the
//      default binary fits exactly what it fitted before #1649.
//
// THE MUTATION: set kSupportMorphKernel to 3 in shaft_axis.hpp and rows 2 and 3's
// "corrected" lines go red (an odd kernel's shift is 0, so nothing is undone).
//
//   compiled by unit_check.sh (row 1649), no extra translation units.

#include <cmath>
#include <iostream>
#include <string>
#include <vector>

#include "detector/geometry/detection/dart_processing.hpp"
#include "detector/geometry/detection/shaft_axis.hpp"

static int failures = 0;

static void say(bool ok, const std::string &what)
{
    std::cout << (ok ? "OK   " : "FAIL ") << what << std::endl;
    if (!ok)
    {
        failures++;
    }
}

static std::string f2(double v)
{
    char b[32];
    snprintf(b, sizeof(b), "%.3f", v);
    return b;
}

// dart_processing.cpp's chain, as written there (lines after the fresh-diff threshold).
static void chain(cv::Mat &m, int k)
{
    const cv::Mat k1 = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(k, k));
    const cv::Mat k2 = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(k / 2, k / 2));
    cv::morphologyEx(m, m, cv::MORPH_CLOSE, k1);
    cv::morphologyEx(m, m, cv::MORPH_OPEN, k1);
    cv::morphologyEx(m, m, cv::MORPH_CLOSE, k2);
    cv::morphologyEx(m, m, cv::MORPH_OPEN, k2);
}

// A filled rod centred on (cx, cy), `length` along angleDeg and `width` across it.
static cv::Mat rod(double cx, double cy, double angleDeg, double length, double width)
{
    cv::Mat m = cv::Mat::zeros(400, 400, CV_8UC1);
    const double a = angleDeg * CV_PI / 180.0;
    const cv::Point2d d(std::cos(a), std::sin(a)), n(-std::sin(a), std::cos(a));
    std::vector<cv::Point> poly;
    for (const auto &c : {d * (length / 2) + n * (width / 2), d * (length / 2) - n * (width / 2),
                          -d * (length / 2) - n * (width / 2), -d * (length / 2) + n * (width / 2)})
    {
        poly.push_back(cv::Point((int)std::lround(cx + c.x), (int)std::lround(cy + c.y)));
    }
    cv::fillConvexPoly(m, poly, cv::Scalar(255));
    return m;
}

int main()
{
    // 1. The chain translates by supportChainShiftPx, for odd and even kernels alike.
    for (int k = 3; k <= 6; k++)
    {
        cv::Mat bar = cv::Mat::zeros(200, 200, CV_8UC1);
        cv::rectangle(bar, cv::Rect(90, 40, 11, 121), cv::Scalar(255), cv::FILLED);
        const cv::Moments before = cv::moments(bar, true);
        chain(bar, k);
        const cv::Moments after = cv::moments(bar, true);
        const double sx = after.m10 / after.m00 - before.m10 / before.m00;
        const double sy = after.m01 / after.m00 - before.m01 / before.m00;
        const int want = shaft_axis::supportChainShiftPx(k);
        say(std::fabs(sx - want) < 1e-9 && std::fabs(sy - want) < 1e-9,
            "k=" + std::to_string(k) + ": the chain moves the bar by (" + f2(sx) + ", " + f2(sy) +
                ") px, supportChainShiftPx says " + std::to_string(want));
    }

    // 2. The copy of the kernel size is the kernel dart_processing uses.
    const int k = dart_processing::DartParams{}.morph_kernel_size;
    say(shaft_axis::kSupportMorphKernel == k,
        "kSupportMorphKernel " + std::to_string(shaft_axis::kSupportMorphKernel) +
            " == DartParams::morph_kernel_size " + std::to_string(k));
    say(shaft_axis::supportChainShiftPx(k) == 4,
        "at the rig's kernel the support arrives translated by (+4, +4) px");

    // 3. The axis inherits the translation, and support_shift_px removes it.
    for (const double angle : {80.0, -75.0})
    {
        const double cx = 200.0, cy = 200.0;
        cv::Mat m = rod(cx, cy, angle, 220.0, 11.0);
        chain(m, k);
        std::vector<cv::Point> px;
        cv::findNonZero(m, px);

        const double a = angle * CV_PI / 180.0;
        cv::Point2d n(std::sin(a), -std::cos(a)); // image-right normal of the true line
        if (n.x < 0)
        {
            n = -n;
        }
        const double predicted = shaft_axis::supportChainShiftPx(k) * (n.x + n.y);

        auto offsetOf = [&](const shaft_axis::AxisObservation &o)
        { return n.x * (o.point.x - cx) + n.y * (o.point.y - cy); };
        auto angleErr = [&](const shaft_axis::AxisObservation &o)
        {
            double e = o.angleDeg - angle;
            while (e > 90.0)
                e -= 180.0;
            while (e < -90.0)
                e += 180.0;
            return std::fabs(e);
        };

        shaft_axis::AxisParams plain;
        plain.support_shift_px = 0;
        const shaft_axis::AxisObservation p = shaft_axis::observeShaftAxis(px, plain);
        say(p.valid && std::fabs(offsetOf(p) - predicted) < 0.35,
            "rod at " + f2(angle) + " deg, uncorrected: the axis lies " + f2(offsetOf(p)) +
                " px image-right of the true line, the chain predicts " + f2(predicted));

        shaft_axis::AxisParams fixed;
        fixed.support_shift_px = shaft_axis::supportChainShiftPx(shaft_axis::kSupportMorphKernel);
        const shaft_axis::AxisObservation q = shaft_axis::observeShaftAxis(px, fixed);
        // A translation cannot rotate the line: the corrected angle is the uncorrected one
        // (the rasterised rod's own quantisation, ~0.2 deg, is in both alike).
        say(q.valid && std::fabs(offsetOf(q)) < 0.35 && std::fabs(q.angleDeg - p.angleDeg) < 1e-3,
            "rod at " + f2(angle) + " deg, corrected: the axis lies " + f2(offsetOf(q)) +
                " px from the true line, at the uncorrected fit's angle (" + f2(q.angleDeg) +
                " vs " + f2(p.angleDeg) + "; " + f2(angleErr(q)) + " deg from the rod's)");
    }

    // 4. The pin: unset, the default params carry no shift.
    say(shaft_axis::AxisParams{}.support_shift_px ==
            (shaft_axis::axisUnshiftIsOn() ? 4 : 0),
        std::string("AxisParams{}.support_shift_px is ") +
            std::to_string(shaft_axis::AxisParams{}.support_shift_px) + " with OD_AXIS_UNSHIFT " +
            (shaft_axis::axisUnshiftIsOn() ? "on" : "unset"));

    std::cout << (failures == 0 ? "PASS" : "FAIL") << " i1649_unshift_check (" << failures
              << " failure(s))" << std::endl;
    return failures == 0 ? 0 : 1;
}
