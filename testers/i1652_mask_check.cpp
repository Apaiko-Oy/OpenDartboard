// #1652: the fresh-diff cleanup can be translation-free at its source.
//
// #1649 proved dart_processing's CLOSE/OPEN chain at k = 4 and k/2 = 2 moves every mask
// (+4, +4) px, and corrected the axis downstream (OD_AXIS_UNSHIFT). This check holds the
// source fix, dart_processing::cleanFreshMask(m, k, centred = true), which runs each
// pair's second operation at the reflected anchor. Five things are asserted:
//
//   1. THE SHIFT GOES. A built bar is moved by exactly supportChainShiftPx(k) by the old
//      chain and by exactly (0, 0) by the centred one, for k = 3..6.
//   2. THE DEFAULT IS THE OLD CHAIN. cleanFreshMask(.., false) is byte-identical to the
//      four morphologyEx calls dart_processing.cpp had, on a noisy random mask.
//   3. NOTHING ELSE CHANGES. On random masks (blobs, rods, gaps and specks), the centred
//      chain's output equals the old chain's output moved by (-4, -4), pixel for pixel
//      away from the border. The odd-kernel alternatives (3 then 1, 5 then 3) are
//      MEASURED against the same reference and printed, not asserted: they are
//      shift-free too but a different element, so they change the shape.
//   4. THE AXIS NEEDS NO CORRECTION. A rod at +80 and -75 deg put through the centred
//      chain fits within 0.35 px of its true line with support_shift_px = 0, at the old
//      chain's angle.
//   5. THE SWITCHES EXCLUDE EACH OTHER. With OD_MASK_UNSHIFT=on AND OD_AXIS_UNSHIFT=on,
//      maskUnshiftIsOn() is true, axisUnshiftIsOn() is false and AxisParams{} carries no
//      shift, so the axis is not moved back a second time.
//
// THE MUTATION: in cleanFreshMask, run the second operation at `a` instead of
// `reflected`, and row 1's centred lines read (+4, +4) and row 3 goes red.
//
//   compiled by unit_check.sh (row 1652), no extra translation units.

#include <cmath>
#include <cstdlib>
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

static std::string f3(double v)
{
    char b[32];
    snprintf(b, sizeof(b), "%.3f", v);
    return b;
}

// dart_processing.cpp's chain as it was written before #1652.
static void literalChain(cv::Mat &m, int k)
{
    const cv::Mat k1 = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(k, k));
    const cv::Mat k2 = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(k / 2, k / 2));
    cv::morphologyEx(m, m, cv::MORPH_CLOSE, k1);
    cv::morphologyEx(m, m, cv::MORPH_OPEN, k1);
    cv::morphologyEx(m, m, cv::MORPH_CLOSE, k2);
    cv::morphologyEx(m, m, cv::MORPH_OPEN, k2);
}

// An odd-kernel alternative: CLOSE/OPEN at a, then at b (both odd, default anchor).
static void oddChain(cv::Mat &m, int a, int b)
{
    const cv::Mat k1 = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(a, a));
    const cv::Mat k2 = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(b, b));
    cv::morphologyEx(m, m, cv::MORPH_CLOSE, k1);
    cv::morphologyEx(m, m, cv::MORPH_OPEN, k1);
    cv::morphologyEx(m, m, cv::MORPH_CLOSE, k2);
    cv::morphologyEx(m, m, cv::MORPH_OPEN, k2);
}

static cv::Mat rod(double cx, double cy, double angleDeg, double length, double width, int size = 400)
{
    cv::Mat m = cv::Mat::zeros(size, size, CV_8UC1);
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

// A thresholded-diff-like mask: rods with gaps, blobs, and salt-and-pepper specks.
static cv::Mat randomMask(cv::RNG &rng, int size)
{
    cv::Mat m = cv::Mat::zeros(size, size, CV_8UC1);
    for (int i = 0; i < 6; i++)
    {
        cv::Mat r = rod(rng.uniform(60.0, size - 60.0), rng.uniform(60.0, size - 60.0),
                        rng.uniform(-90.0, 90.0), rng.uniform(40.0, 200.0), rng.uniform(2.0, 16.0), size);
        m |= r;
    }
    for (int i = 0; i < 10; i++)
    {
        cv::circle(m, cv::Point(rng.uniform(30, size - 30), rng.uniform(30, size - 30)),
                   rng.uniform(1, 12), cv::Scalar(255), cv::FILLED);
    }
    for (int i = 0; i < 12; i++) // gaps across the figures
    {
        cv::line(m, cv::Point(rng.uniform(0, size), rng.uniform(0, size)),
                 cv::Point(rng.uniform(0, size), rng.uniform(0, size)), cv::Scalar(0), rng.uniform(1, 4));
    }
    cv::Mat noise(size, size, CV_8UC1);
    rng.fill(noise, cv::RNG::UNIFORM, 0, 1000);
    m.setTo(255, noise < 15);  // 1.5 % salt
    m.setTo(0, noise > 985);   // 1.5 % pepper
    return m;
}

// `m` moved by (dx, dy), zero-filled.
static cv::Mat moved(const cv::Mat &m, int dx, int dy)
{
    cv::Mat out = cv::Mat::zeros(m.size(), m.type());
    const cv::Matx23d t(1, 0, dx, 0, 1, dy);
    cv::warpAffine(m, out, t, m.size(), cv::INTER_NEAREST, cv::BORDER_CONSTANT, cv::Scalar(0));
    return out;
}

static int differingInterior(const cv::Mat &a, const cv::Mat &b, int border)
{
    const cv::Rect in(border, border, a.cols - 2 * border, a.rows - 2 * border);
    cv::Mat d;
    cv::compare(a(in), b(in), d, cv::CMP_NE);
    return cv::countNonZero(d);
}

int main()
{
    // Row 5's pins must be set before the first read, because both switches are cached.
    setenv("OD_MASK_UNSHIFT", "on", 1);
    setenv("OD_AXIS_UNSHIFT", "on", 1);

    // 1. The shift goes.
    for (int k = 3; k <= 6; k++)
    {
        for (const bool centred : {false, true})
        {
            cv::Mat bar = cv::Mat::zeros(200, 200, CV_8UC1);
            cv::rectangle(bar, cv::Rect(90, 40, 11, 121), cv::Scalar(255), cv::FILLED);
            const cv::Moments before = cv::moments(bar, true);
            dart_processing::cleanFreshMask(bar, k, centred);
            const cv::Moments after = cv::moments(bar, true);
            const double sx = after.m10 / after.m00 - before.m10 / before.m00;
            const double sy = after.m01 / after.m00 - before.m01 / before.m00;
            const int want = centred ? 0 : shaft_axis::supportChainShiftPx(k);
            say(std::fabs(sx - want) < 1e-9 && std::fabs(sy - want) < 1e-9 &&
                    std::fabs(after.m00 - before.m00) < 1e-9,
                "k=" + std::to_string(k) + (centred ? " centred" : " old    ") +
                    ": the chain moves the bar by (" + f3(sx) + ", " + f3(sy) + ") px, want (" +
                    std::to_string(want) + ", " + std::to_string(want) + "), area kept");
        }
    }

    const int k = dart_processing::DartParams{}.morph_kernel_size;
    cv::RNG rng(1652);

    // 2. The default is the old chain, byte for byte.
    {
        const cv::Mat src = randomMask(rng, 480);
        cv::Mat a = src.clone(), b = src.clone();
        literalChain(a, k);
        dart_processing::cleanFreshMask(b, k, false);
        say(differingInterior(a, b, 0) == 0,
            "cleanFreshMask(.., false) is the literal morphologyEx chain on every pixel of a "
            "480x480 random mask");
    }

    // 3. Nothing else changes; the odd-kernel alternatives measured beside it.
    {
        const int shift = shaft_axis::supportChainShiftPx(k);
        const int border = 3 * k;
        long diffCentred = 0, diff31 = 0, diff53 = 0, refPx = 0;
        for (int t = 0; t < 20; t++)
        {
            const cv::Mat src = randomMask(rng, 400);
            cv::Mat old = src.clone(), cen = src.clone(), o31 = src.clone(), o53 = src.clone();
            literalChain(old, k);
            dart_processing::cleanFreshMask(cen, k, true);
            oddChain(o31, 3, 1);
            oddChain(o53, 5, 3);
            const cv::Mat ref = moved(old, -shift, -shift);
            diffCentred += differingInterior(cen, ref, border);
            diff31 += differingInterior(o31, ref, border);
            diff53 += differingInterior(o53, ref, border);
            refPx += cv::countNonZero(ref);
        }
        say(diffCentred == 0,
            "20 random masks: the centred chain differs from the old chain moved by (-" +
                std::to_string(shift) + ", -" + std::to_string(shift) + ") on " +
                std::to_string(diffCentred) + " interior px (want 0)");
        std::cout << "MEASURE odd kernels against the same reference over " << refPx
                  << " kept px: 3x3 then 1x1 differs on " << diff31 << " px ("
                  << f3(100.0 * diff31 / std::max(1L, refPx)) << "%), 5x5 then 3x3 on " << diff53
                  << " px (" << f3(100.0 * diff53 / std::max(1L, refPx)) << "%)" << std::endl;
    }

    // 4. The axis needs no correction.
    for (const double angle : {80.0, -75.0})
    {
        const double cx = 200.0, cy = 200.0;
        cv::Mat m = rod(cx, cy, angle, 220.0, 11.0);
        cv::Mat mOld = m.clone();
        dart_processing::cleanFreshMask(m, k, true);
        literalChain(mOld, k);
        std::vector<cv::Point> px, pxOld;
        cv::findNonZero(m, px);
        cv::findNonZero(mOld, pxOld);

        const double a = angle * CV_PI / 180.0;
        cv::Point2d n(std::sin(a), -std::cos(a));
        if (n.x < 0)
        {
            n = -n;
        }
        shaft_axis::AxisParams plain;
        plain.support_shift_px = 0;
        const shaft_axis::AxisObservation q = shaft_axis::observeShaftAxis(px, plain);
        const shaft_axis::AxisObservation p = shaft_axis::observeShaftAxis(pxOld, plain);
        const double off = n.x * (q.point.x - cx) + n.y * (q.point.y - cy);
        say(q.valid && p.valid && std::fabs(off) < 0.35 && std::fabs(q.angleDeg - p.angleDeg) < 1e-3,
            "rod at " + f3(angle) + " deg, centred chain, no axis correction: the axis lies " + f3(off) +
                " px from the true line, at the old chain's angle (" + f3(q.angleDeg) + " vs " +
                f3(p.angleDeg) + ")");
    }

    // 5. The switches exclude each other.
    say(shaft_axis::maskUnshiftIsOn(), "OD_MASK_UNSHIFT=on reads as on");
    say(!shaft_axis::axisUnshiftIsOn() && shaft_axis::AxisParams{}.support_shift_px == 0,
        "with OD_MASK_UNSHIFT=on, OD_AXIS_UNSHIFT=on is ignored: AxisParams{}.support_shift_px is " +
            std::to_string(shaft_axis::AxisParams{}.support_shift_px));

    std::cout << (failures == 0 ? "PASS" : "FAIL") << " i1652_mask_check (" << failures
              << " failure(s))" << std::endl;
    return failures == 0 ? 0 : 1;
}
