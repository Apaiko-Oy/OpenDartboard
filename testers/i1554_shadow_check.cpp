// #1554: the cast-shadow subtraction, held with figures whose truth AND lighting are
// KNOWN.
//
// Every case below builds the two intensity images the discriminator reads -- a
// reference board and a current frame with objects PAINTED onto it -- so which pixel
// is dart, which is flight and which is shadow is construction, never a fixture's
// opinion. The support handed to the fit is exactly the pipeline's rule: every pixel
// whose |current - reference| clears the fresh-diff threshold (20, the
// DartParams::background_diff_threshold default).
//
// THE PREDICTIONS, STATED BEFORE ANY RUN OF THIS FILE (the issue's required mutation
// discipline):
//
//  1. The known-case figure (modelled on rig-20260922 w9: a thin dart with a FUSED
//     one-sided shadow whose separation grows smoothly along the shaft, a symmetric
//     flight, and a detached flight-shadow ridge) is ACCEPTED both ways -- the
//     contaminated spine is straight, which is why no geometric trim can see it --
//     but WITHOUT the subtraction the fit is tilted off the built axis and displaced
//     at mid-line, and WITH it both errors shrink, by at least 1.0 degree and to
//     under 1.2 degrees / 3.0 px.
//  2. Disabling the subtraction (params.subtract_shadow = false, the same switch
//     OD_AXIS_SHADOW=off throws) RESTORES the displaced fit byte-for-byte: the
//     mutation is load-bearing, not decorative.
//  3. A symmetric flight with no shadow is untouched: nothing classified, the axis
//     unchanged against the no-image fit.
//  4. A figure that is NOTHING but shadow refuses by name ("all shadow") instead of
//     answering a confident line about nobody.
//  5. A dark barrel crossing a DARK wedge (drop -40 on a 60-grey reference) KEEPS its
//     vote -- 40 is 67% of the light, which no shadow produces -- so the dimming
//     bound, not the depth-of-drop bound, is what decides; shadowPixels reads 0.
//  6. Two parallel equal dart-dark rods stay REFUSED with the subtraction live: the
//     discriminator removes shadow, never a competitor object.
//  7. Shuffled pixel order answers byte-identical bytes, images or none; and the
//     no-image path answers byte-for-byte what #1511's two-argument call answers.
//
//   compiled by unit_check.sh (row 1554), no extra translation units.

#include <algorithm>
#include <cmath>
#include <iostream>
#include <random>
#include <string>
#include <vector>

#include "detector/geometry/detection/shaft_axis.hpp"

using namespace shaft_axis;

static int failures = 0;

static void say(bool ok, const std::string &what)
{
    std::cout << (ok ? "OK   " : "FAIL ") << what << std::endl;
    if (!ok)
    {
        failures++;
    }
}

static std::string figures(const AxisObservation &o)
{
    char buf[260];
    snprintf(buf, sizeof(buf),
             "[px=%d shadowPx=%d shadowCols=%d cols=%d extent=%.1f width=%.1f rms=%.3f "
             "sigma=%.3f angle=%.2f]",
             o.supportPixels, o.shadowPixels, o.shadowColumns, o.columns, o.extentPx,
             o.medianWidthPx, o.centrelineRmsPx, o.sigmaDeg, o.angleDeg);
    return buf;
}

// ---- painters: the truth AND the lighting are built, never detected -----------------

/** Paint every pixel inside a rotated rectangle to an absolute grey value. */
static void paintRod(cv::Mat &img, double cx, double cy, double angleDeg, double length,
                     double width, unsigned char value)
{
    const double a = angleDeg * CV_PI / 180.0;
    const double c = std::cos(a), s = std::sin(a);
    const double reach = (length + width) / 2.0 + 2.0;
    for (int y = (int)std::floor(cy - reach); y <= (int)std::ceil(cy + reach); y++)
    {
        for (int x = (int)std::floor(cx - reach); x <= (int)std::ceil(cx + reach); x++)
        {
            if (x < 0 || y < 0 || x >= img.cols || y >= img.rows)
            {
                continue;
            }
            const double t = (x - cx) * c + (y - cy) * s;
            const double u = -(x - cx) * s + (y - cy) * c;
            if (std::fabs(t) <= length / 2.0 && std::fabs(u) <= width / 2.0)
            {
                img.at<unsigned char>(y, x) = value;
            }
        }
    }
}

/** The pipeline's support rule: every pixel whose |current - reference| clears the
 *  fresh-diff threshold. */
static std::vector<cv::Point> supportOf(const cv::Mat &current, const cv::Mat &reference,
                                        int threshold = 20)
{
    std::vector<cv::Point> pixels;
    for (int y = 0; y < current.rows; y++)
    {
        for (int x = 0; x < current.cols; x++)
        {
            if (std::abs((int)current.at<unsigned char>(y, x) -
                         (int)reference.at<unsigned char>(y, x)) >= threshold)
            {
                pixels.push_back(cv::Point(x, y));
            }
        }
    }
    return pixels;
}

/** Angle difference between two LINE angles, degrees, in [0, 90]. */
static double lineAngleError(double gotDeg, double wantDeg)
{
    double d = std::fmod(std::fabs(gotDeg - wantDeg), 180.0);
    return d > 90.0 ? 180.0 - d : d;
}

/** Perpendicular distance from a point to the observation's line, px. */
static double perpTo(const AxisObservation &o, double px, double py)
{
    const double dx = px - o.point.x, dy = py - o.point.y;
    return std::fabs(dx * o.direction.y - dy * o.direction.x);
}

static bool sameAnswer(const AxisObservation &a, const AxisObservation &b)
{
    return a.valid == b.valid && a.direction == b.direction && a.point == b.point &&
           a.angleDeg == b.angleDeg && a.centrelineRmsPx == b.centrelineRmsPx &&
           a.extentPx == b.extentPx && a.sigmaDeg == b.sigmaDeg;
}

int main()
{
    const AxisParams params; // gated, subtraction on: the deployment defaults

    // ---- the known case: w9's shape, built (the header's polarity numbers) ------------
    // A dart along 80 degrees through (300, 300): thin rod, FUSED one-sided shadow
    // whose lateral separation and width grow smoothly toward the flight end (the
    // standoff physics: the shadow meets the dart at the board contact and walks away
    // with height), a symmetric flight with a bright and a dark face, and a detached
    // flight-shadow ridge. Board 180 grey; dart 25 (drop -155: dart); shadow 126
    // (drop -54 = 30% of 180: shadow); flight faces 235 (+55) and 25 (-155).
    const double A = 80.0, AR = A * CV_PI / 180.0;
    const double ax = std::cos(AR), ay = std::sin(AR);   // along the axis
    const double nx = -ay, ny = ax;                      // shadow side: +n
    cv::Mat ref(640, 640, CV_8UC1, cv::Scalar(180));
    cv::Mat cur = ref.clone();
    // shadow first, dart over it, so no shadow pixel ever overwrites the object
    for (int s = -130; s <= 130; s += 2)
    {
        const double f = (s + 130) / 260.0;              // 0 at tip end, 1 at flight end
        const double off = 6.0 + 14.0 * f;               // fused: 6 -> 20 px
        const double wid = 6.0 + 10.0 * f;               // 6 -> 16 px
        paintRod(cur, 300 + s * ax + off * nx, 300 + s * ay + off * ny, A, 3.0, wid, 126);
    }
    paintRod(cur, 300 + 105 * ax + 30 * nx, 300 + 105 * ay + 30 * ny, A, 52, 26, 126);
    paintRod(cur, 300, 300, A, 260, 6, 25);                                   // the dart
    paintRod(cur, 300 + 105 * ax - 8 * nx, 300 + 105 * ay - 8 * ny, A, 52, 12, 235);
    paintRod(cur, 300 + 105 * ax + 5 * nx, 300 + 105 * ay + 5 * ny, A, 52, 12, 25);
    const std::vector<cv::Point> known = supportOf(cur, ref);

    AxisParams off = params;
    off.subtract_shadow = false;
    const AxisObservation without = observeShaftAxis(known, cur, ref, off);
    const AxisObservation with = observeShaftAxis(known, cur, ref, params);
    const AxisObservation noImages = observeShaftAxis(known, params);
    const double errWithout = lineAngleError(without.angleDeg, A);
    const double errWith = lineAngleError(with.angleDeg, A);
    const double perpWithout = perpTo(without, 300, 300);
    const double perpWith = perpTo(with, 300, 300);
    say(without.valid,
        "known case, subtraction OFF: the contaminated fit is ACCEPTED -- the fused "
        "shadow bends nothing the rms gate can see " + figures(without));
    say(without.valid && errWithout >= 1.0 && perpWithout >= 3.0,
        "known case, subtraction OFF: and it is displaced -- angle " +
            detail::fmt("%.2f", errWithout) + " deg off the built axis, " +
            detail::fmt("%.1f", perpWithout) + " px off at the axis midpoint");
    say(with.valid && errWith <= 1.2 && perpWith <= 3.0,
        "known case, subtraction ON: the axis moves onto the built line -- angle err " +
            detail::fmt("%.2f", errWith) + " deg, midpoint perp " +
            detail::fmt("%.1f", perpWith) + " px " + figures(with));
    say(with.valid && without.valid && (errWithout - errWith) >= 1.0,
        "known case: the subtraction is what moved it (angle err " +
            detail::fmt("%.2f", errWithout) + " -> " + detail::fmt("%.2f", errWith) + " deg)");
    say(with.shadowSubtracted && with.shadowPixels > 500,
        "known case: the classifier was LIVE and found the shadow (" +
            std::to_string(with.shadowPixels) + " px classified, " +
            std::to_string(with.shadowColumns) + " all-shadow columns dropped)");

    // ---- the mutation, prediction 2: OFF restores the displaced fit exactly -----------
    say(sameAnswer(without, noImages),
        "mutation: subtract_shadow=false answers byte-for-byte the no-image fit -- "
        "disabling the subtraction RESTORES the displaced fit");
    say(!noImages.shadowSubtracted && noImages.shadowPixels == 0,
        "and the no-image observation SAYS the classifier never ran "
        "(shadowSubtracted=false), so its zero cannot be read as 'no shadow' (#708)");

    // ---- prediction 3: a symmetric flight with no shadow is untouched -----------------
    {
        cv::Mat r2(640, 640, CV_8UC1, cv::Scalar(180));
        cv::Mat c2 = r2.clone();
        paintRod(c2, 300, 300, -25, 90, 5, 25);
        const double bx = std::cos(-25 * CV_PI / 180.0), by = std::sin(-25 * CV_PI / 180.0);
        paintRod(c2, 300 + 62 * bx - 6.5 * (-by), 300 + 62 * by - 6.5 * bx, -25, 34, 13, 235);
        paintRod(c2, 300 + 62 * bx + 6.5 * (-by), 300 + 62 * by + 6.5 * bx, -25, 34, 13, 25);
        const std::vector<cv::Point> pts = supportOf(c2, r2);
        const AxisObservation plain = observeShaftAxis(pts, params);
        const AxisObservation seen = observeShaftAxis(pts, c2, r2, params);
        say(seen.valid && seen.shadowPixels == 0,
            "symmetric flight, no shadow: nothing classified " + figures(seen));
        say(seen.valid && plain.valid &&
                lineAngleError(seen.angleDeg, plain.angleDeg) < 0.01,
            "symmetric flight: the flight keeps its vote -- axis unchanged against the "
            "no-image fit");
    }

    // ---- prediction 4: an all-shadow figure refuses by name ---------------------------
    {
        cv::Mat r3(400, 400, CV_8UC1, cv::Scalar(180));
        cv::Mat c3 = r3.clone();
        paintRod(c3, 200, 200, 60, 130, 12, 126);
        const std::vector<cv::Point> pts = supportOf(c3, r3);
        const AxisObservation o = observeShaftAxis(pts, c3, r3, params);
        say(!o.valid && o.refusal.find("all shadow") != std::string::npos,
            "a pure shadow band REFUSES by name: " + o.refusal);
        const AxisObservation blind = observeShaftAxis(pts, params);
        say(blind.valid,
            "and the same band WITHOUT the images is a confident wrong axis -- the "
            "discriminator, not degeneracy, is what refuses it " + figures(blind));
    }

    // ---- prediction 5: a dark barrel on a dark wedge keeps its vote -------------------
    {
        cv::Mat r4(400, 400, CV_8UC1, cv::Scalar(60));  // a dark wedge
        cv::Mat c4 = r4.clone();
        paintRod(c4, 200, 200, 40, 150, 7, 20);         // drop -40: 67% of the light
        const std::vector<cv::Point> pts = supportOf(c4, r4);
        const AxisObservation o = observeShaftAxis(pts, c4, r4, params);
        say(o.valid && o.shadowPixels == 0 && lineAngleError(o.angleDeg, 40) < 1.0,
            "dark barrel on a dark wedge: no shadow produces a 67% drop, so the dimming "
            "bound keeps every pixel and the axis " + figures(o));
    }

    // ---- prediction 6: a parallel equal competitor is not 'shadow' --------------------
    {
        cv::Mat r5(640, 640, CV_8UC1, cv::Scalar(180));
        cv::Mat c5 = r5.clone();
        paintRod(c5, 300, 300, 12, 110, 7, 25);
        paintRod(c5, 300 + 80 * std::cos(12 * CV_PI / 180.0),
                 300 + 80 * std::sin(12 * CV_PI / 180.0) + 26, 12, 110, 7, 25);
        const std::vector<cv::Point> pts = supportOf(c5, r5);
        const AxisObservation o = observeShaftAxis(pts, c5, r5, params);
        say(!o.valid && o.shadowPixels == 0,
            "parallel older dart, both dart-dark: nothing classified, still REFUSED: " +
                o.refusal);
    }

    // ---- prediction 7: determinism, and the no-image path is #1511's ------------------
    {
        std::vector<cv::Point> shuffled = known;
        std::mt19937 rng(1554);
        std::shuffle(shuffled.begin(), shuffled.end(), rng);
        const AxisObservation b = observeShaftAxis(shuffled, cur, ref, params);
        say(sameAnswer(with, b),
            "determinism: shuffled pixel order answers byte-identical bytes with the "
            "classifier live");
        const AxisObservation two = observeShaftAxis(known, params);
        const AxisObservation four = observeShaftAxis(known, cv::Mat(), cv::Mat(), params);
        say(sameAnswer(two, four),
            "the two-argument #1511 call and empty images answer byte-for-byte the same");
    }

    std::cout << (failures == 0 ? "ALL OK" : "FAILURES: " + std::to_string(failures)) << std::endl;
    return failures == 0 ? 0 : 1;
}
