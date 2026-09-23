// #1511: the shaft-axis fit, held with figures whose truth is KNOWN.
//
// Every case below is a synthetic pixel figure built here, by rasterising rectangles
// and discs -- never by running the detector -- so the expected axis is arithmetic and
// not a fixture's opinion. The issue's own list is the coverage: rotation, scale,
// fragmentation, shadows, flight-dominated shapes, two competing objects, and a dart
// seen nearly end-on.
//
// THE ISSUE'S REQUIRED MUTATION IS MEASURED ON EVERY RUN, not once in a report:
// "removing the quality gate must make negative controls fail". Each negative control
// is asserted TWICE -- gated it must be REFUSED, and with the gate off (the same
// binary, AxisParams::gated = false, which is what OD_AXIS_GATE=off sets in the real
// pipeline) it must come back VALID. The second half is the load-bearing one: a
// negative control the ungated fit also refuses is structurally degenerate and proves
// nothing about the gate. The prediction, stated before any run of this file: all five
// negative controls (near-end-on blob, two crossing rods, a shadow-dominated figure, a
// bent silhouette, an under-floor speck) are refused gated and accepted ungated.
//
// It also PRINTS the measured gate figures per case, because the constants in
// AxisParams cite this census in their docblocks and a cited census must be
// reproducible by running one file.
//
//   compiled by unit_check.sh (row 1511), no extra translation units.

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
    char buf[220];
    snprintf(buf, sizeof(buf),
             "[px=%d cols=%d extent=%.1f width=%.1f elong=%.2f rms=%.3f trim=%.3f sigma=%.3f angle=%.2f]",
             o.supportPixels, o.columns, o.extentPx, o.medianWidthPx,
             o.medianWidthPx > 0 ? o.extentPx / o.medianWidthPx : 0.0,
             o.centrelineRmsPx, o.trimmedFraction, o.sigmaDeg, o.angleDeg);
    return buf;
}

// ---- rasterisers: the truth is built, never detected --------------------------------

/** Every integer pixel inside a rotated rectangle: centre, axis angle, length, width. */
static void addRod(std::vector<cv::Point> &pixels, double cx, double cy, double angleDeg,
                   double length, double width)
{
    const double a = angleDeg * CV_PI / 180.0;
    const double c = std::cos(a), s = std::sin(a);
    const double reach = (length + width) / 2.0 + 2.0;
    for (int y = (int)std::floor(cy - reach); y <= (int)std::ceil(cy + reach); y++)
    {
        for (int x = (int)std::floor(cx - reach); x <= (int)std::ceil(cx + reach); x++)
        {
            const double t = (x - cx) * c + (y - cy) * s;
            const double u = -(x - cx) * s + (y - cy) * c;
            if (std::fabs(t) <= length / 2.0 && std::fabs(u) <= width / 2.0)
            {
                pixels.push_back(cv::Point(x, y));
            }
        }
    }
}

/** Every integer pixel inside a disc. */
static void addDisc(std::vector<cv::Point> &pixels, double cx, double cy, double r)
{
    for (int y = (int)std::floor(cy - r); y <= (int)std::ceil(cy + r); y++)
    {
        for (int x = (int)std::floor(cx - r); x <= (int)std::ceil(cx + r); x++)
        {
            const double dx = x - cx, dy = y - cy;
            if (dx * dx + dy * dy <= r * r)
            {
                pixels.push_back(cv::Point(x, y));
            }
        }
    }
}

/** Duplicate pixels break nothing, but the figures print honestly deduplicated. */
static void dedupe(std::vector<cv::Point> &pixels)
{
    std::sort(pixels.begin(), pixels.end(), [](const cv::Point &a, const cv::Point &b)
              { return a.y != b.y ? a.y < b.y : a.x < b.x; });
    pixels.erase(std::unique(pixels.begin(), pixels.end()), pixels.end());
}

/** Angle difference between two LINE angles, degrees, in [0, 90]. */
static double lineAngleError(double gotDeg, double wantDeg)
{
    double d = std::fmod(std::fabs(gotDeg - wantDeg), 180.0);
    return d > 90.0 ? 180.0 - d : d;
}

int main()
{
    const AxisParams gated;
    AxisParams ungated;
    ungated.gated = false;

    // ---- rotation: a plain rod at many angles reads its own angle ---------------------
    for (const double angle : {0.0, 17.0, 45.0, 77.0, 90.0, -30.0, -88.0})
    {
        std::vector<cv::Point> pixels;
        addRod(pixels, 300, 300, angle, 120, 7);
        dedupe(pixels);
        const AxisObservation o = observeShaftAxis(pixels, gated);
        say(o.valid && lineAngleError(o.angleDeg, angle) < 1.0,
            "rotation " + detail::fmt("%+.0f", angle) + " deg: valid axis within 1 deg " + figures(o));
        say(o.valid && o.direction.x >= 0.0f,
            "         ... and the direction is sign-normalised (dx >= 0)");
    }

    // ---- scale: the same rule reads a dart at half and at double the size -------------
    {
        std::vector<cv::Point> small, large;
        addRod(small, 200, 200, 33, 45, 4);
        addRod(large, 400, 400, 33, 260, 16);
        dedupe(small);
        dedupe(large);
        const AxisObservation a = observeShaftAxis(small, gated);
        const AxisObservation b = observeShaftAxis(large, gated);
        say(a.valid && lineAngleError(a.angleDeg, 33) < 1.5, "scale: a 45x4 rod is a valid axis " + figures(a));
        say(b.valid && lineAngleError(b.angleDeg, 33) < 1.0, "scale: a 260x16 rod is a valid axis " + figures(b));
        say(a.valid && b.valid && b.sigmaDeg < a.sigmaDeg,
            "scale: the longer support is the more certain direction (sigma " +
                detail::fmt("%.3f", b.sigmaDeg) + " < " + detail::fmt("%.3f", a.sigmaDeg) + ")");
    }

    // ---- fragmentation: a shaft in four pieces is still one line ----------------------
    {
        std::vector<cv::Point> pixels;
        // Four collinear fragments with gaps -- the shape #1492 measured the 400 px
        // floor dropping (9 to 328 px fragments on the rig).
        addRod(pixels, 260, 300, 20, 30, 6);
        addRod(pixels, 300, 314, 20, 26, 6);
        addRod(pixels, 336, 327, 20, 22, 6);
        addRod(pixels, 372, 341, 20, 34, 6);
        dedupe(pixels);
        const AxisObservation o = observeShaftAxis(pixels, gated);
        say(o.valid && lineAngleError(o.angleDeg, 20) < 1.5,
            "fragmentation: four collinear fragments fit one axis within 1.5 deg " + figures(o));
    }

    // ---- flight-dominated: the wide end must not outvote the shaft --------------------
    {
        std::vector<cv::Point> pixels;
        // A thin shaft with a flight THREE TIMES its width on one end, collinear --
        // the real dart's own shape. Column means sit on the shared axis, so the fit
        // must read the axis, not the flight's blob-first principal direction.
        addRod(pixels, 300, 300, -25, 90, 5);   // shaft
        addRod(pixels, 300 + 62 * std::cos(-25 * CV_PI / 180.0),
               300 + 62 * std::sin(-25 * CV_PI / 180.0), -25, 34, 26); // flight
        dedupe(pixels);
        const AxisObservation o = observeShaftAxis(pixels, gated);
        say(o.valid && lineAngleError(o.angleDeg, -25) < 1.5,
            "flight-dominated: shaft+flight reads the shared axis within 1.5 deg " + figures(o));
    }

    // ---- a small lateral shadow: trimmed, and the axis stays right --------------------
    {
        std::vector<cv::Point> pixels;
        addRod(pixels, 300, 300, 10, 130, 7);
        addDisc(pixels, 310, 322, 11); // a lobe to one side, mid-shaft
        dedupe(pixels);
        const AxisObservation o = observeShaftAxis(pixels, gated);
        say(o.valid && lineAngleError(o.angleDeg, 10) < 2.0,
            "small shadow lobe: the axis survives within 2 deg " + figures(o));
        say(o.valid && o.trimmedColumns > 0,
            "         ... and the lobe's columns are in the trimmed census (" +
                std::to_string(o.trimmedColumns) + " trimmed)");
    }

    // ---- NEGATIVE CONTROLS: refused gated, accepted ungated ---------------------------
    // The pair is the mutation proof. Prediction, stated before the run: all five are
    // refused gated AND valid ungated.
    struct Negative
    {
        const char *name;
        std::vector<cv::Point> pixels;
        double wrongIfNear = 1e9; // ungated angle that would prove the gate pointless
        double trueAngle = 0.0;
    };
    std::vector<Negative> negatives;
    {
        Negative n{"near-end-on blob", {}, 1e9, 0.0};
        addDisc(n.pixels, 300, 300, 16); // a dart at the camera: a compact blob
        dedupe(n.pixels);
        negatives.push_back(n);
    }
    {
        Negative n{"two crossing rods", {}, 1e9, 0.0};
        addRod(n.pixels, 300, 300, 15, 120, 7);
        addRod(n.pixels, 310, 306, 60, 110, 7); // the older dart, linked into the figure
        dedupe(n.pixels);
        negatives.push_back(n);
    }
    {
        Negative n{"shadow-dominated figure", {}, 1e9, 20.0};
        addRod(n.pixels, 300, 300, 20, 90, 6);      // the dart
        addRod(n.pixels, 330, 345, -35, 130, 34);   // its shadow, larger and off-axis
        dedupe(n.pixels);
        negatives.push_back(n);
    }
    {
        Negative n{"bent silhouette", {}, 1e9, 0.0};
        // The straight-axis assumption violated: an elbow at 35 degrees. The issue
        // asks that violations be detected and reported, and the residual is where
        // they land.
        addRod(n.pixels, 260, 300, 5, 90, 6);
        addRod(n.pixels, 335, 330, 40, 90, 6);
        dedupe(n.pixels);
        negatives.push_back(n);
    }
    {
        Negative n{"under-floor speck", {}, 1e9, 0.0};
        addRod(n.pixels, 300, 300, 30, 9, 4); // ~36 px: over the 3-px structural floor,
        dedupe(n.pixels);                     // under the 60-px evidential one
        negatives.push_back(n);
    }
    {
        Negative n{"parallel older dart", {}, 1e9, 12.0};
        // The previous-dart overlap the issue names: the new dart, and an older one
        // PARALLEL to it, laterally offset, linked into one figure and reaching
        // beyond the new dart's end. The columns they share average to a line
        // between the two objects; the columns past the end are the older dart
        // alone. Whatever the robust fit converges on, this figure is not one
        // object's evidence.
        addRod(n.pixels, 300, 300, 12, 110, 7);
        addRod(n.pixels, 300 + 80 * std::cos(12 * CV_PI / 180.0),
               300 + 80 * std::sin(12 * CV_PI / 180.0) + 26, 12, 110, 7);
        dedupe(n.pixels);
        negatives.push_back(n);
    }
    {
        Negative n{"off-axis end blob", {}, 1e9, 8.0};
        // A shadow blob hanging past the shaft's end, well off the axis. MEASURED:
        // the contaminated first fit tilts toward the blob, the residual MAD
        // inflates, nothing is trimmed (trim 0.000) and the RMS gate is what refuses
        // it at 7.04 px -- the numbers that refused a trim-fraction gate, recorded in
        // AxisParams' docblock.
        addRod(n.pixels, 300, 300, 8, 110, 7);
        addDisc(n.pixels, 300 + 72 * std::cos(8 * CV_PI / 180.0),
                300 + 72 * std::sin(8 * CV_PI / 180.0) + 26, 17);
        dedupe(n.pixels);
        negatives.push_back(n);
    }
    {
        Negative n{"short jittery fragment", {}, 1e9, 0.0};
        // Too short and too scattered to be evidence: measured, its jitter widens its
        // own columns, so the elongation gate is what refuses it (sigma measures 5.2
        // degrees beside the 6.0 gate -- the sigma gate is the backstop here, not the
        // trigger, and the AxisParams docblock records that).
        const int offsets[16] = {0, 3, -2, 2, -3, 3, 0, -2, 2, 3, -3, -2, 0, 2, -3, 3};
        for (int t = 0; t < 16; t++)
        {
            for (int u = -1; u <= 2; u++)
            {
                n.pixels.push_back(cv::Point(300 + t, 300 + offsets[t] + u));
            }
        }
        dedupe(n.pixels);
        negatives.push_back(n);
    }
    for (const Negative &n : negatives)
    {
        const AxisObservation g = observeShaftAxis(n.pixels, gated);
        const AxisObservation u = observeShaftAxis(n.pixels, ungated);
        say(!g.valid, std::string("negative gated: ") + n.name + " is REFUSED: " +
                          (g.valid ? "(was not)" : g.refusal) + " " + figures(g));
        say(u.valid, std::string("negative ungated: ") + n.name +
                         " comes back VALID with the gate off -- the gate, not degeneracy, "
                         "is what refuses it " +
                         figures(u));
    }
    // The shadow-dominated control is #1505's hazard by name: ungated it answers a
    // CONFIDENT WRONG axis, pulled off the dart by the shadow. Measured, not assumed.
    {
        const AxisObservation u = observeShaftAxis(negatives[2].pixels, ungated);
        say(u.valid && lineAngleError(u.angleDeg, negatives[2].trueAngle) > 5.0,
            "ungated shadow figure answers " + detail::fmt("%.1f", u.angleDeg) +
                " deg where the dart lies at " + detail::fmt("%.1f", negatives[2].trueAngle) +
                " deg -- the confident wrong output the gate exists to refuse");
    }

    // ---- determinism: one figure, one answer, whatever order the pixels arrive in ----
    {
        std::vector<cv::Point> pixels;
        addRod(pixels, 300, 300, 52, 100, 6);
        dedupe(pixels);
        std::vector<cv::Point> shuffled = pixels;
        std::mt19937 rng(1511);
        std::shuffle(shuffled.begin(), shuffled.end(), rng);
        const AxisObservation a = observeShaftAxis(pixels, gated);
        const AxisObservation b = observeShaftAxis(shuffled, gated);
        say(a.valid && b.valid && a.direction == b.direction && a.point == b.point,
            "determinism: shuffled pixel order answers byte-identical direction and point");
    }

    // ---- refusals carry their numbers (#1321's rule on the sentence) ------------------
    {
        std::vector<cv::Point> pixels;
        addDisc(pixels, 300, 300, 16);
        dedupe(pixels);
        const AxisObservation o = observeShaftAxis(pixels, gated);
        say(!o.valid && o.refusal.find("elongation") != std::string::npos &&
                o.refusal.find("3.0") != std::string::npos,
            "a blob's refusal names the elongation gate and its threshold: " + o.refusal);
    }
    {
        const AxisObservation o = observeShaftAxis(std::vector<cv::Point>(), gated);
        say(!o.valid, "an empty figure refuses rather than answering: " + o.refusal);
    }

    std::cout << (failures == 0 ? "ALL OK" : "FAILURES: " + std::to_string(failures)) << std::endl;
    return failures == 0 ? 0 : 1;
}
