// #1586: the composite rescue, held with figures whose truth is KNOWN.
//
// Every figure is rasterised here (i1511_axis_check's rasterisers, same shapes), so the
// expected axis is arithmetic. Three things are asserted:
//
//   1. A DART WITH SOMETHING LINKED INTO IT is recovered: the plain fit refuses it as
//      "not straight", and the rescue returns the dart's own line -- angle within 1.5
//      deg, and the line within 2 px of the dart's centre -- having passed every
//      ordinary gate on the corridor.
//   2. TWO OBJECTS stay refused: every one of #1511's negative controls is refused with
//      the rescue ON as well, or -- where a figure really is one dart plus a smaller
//      distractor (the off-axis end blob, the shadow-dominated figure) -- rescued onto
//      the DART, never onto the distractor. The confident wrong axis is the thing a
//      rescue could newly create, so this is the half that matters.
//   3. The pin: with rescue_composite off (what OD_AXIS_RESCUE=off sets in the
//      pipeline) the answer is byte-for-byte the plain fit's.
//
// THE PREDICTION, stated before the first run, and what the first run said instead:
// predicted that the crossing rods, bent silhouette and parallel older dart would all be
// refused by the RIVAL rule. They are refused, but by other halves of the rule --
// measured: the bent silhouette (0.53) and the parallel older dart (0.44) hold no
// dominant component at all, and the crossing rods' dominant component (0.77) fails the
// ordinary straightness gate on its own corridor (4.66 px). The rival rule's own case is
// the rival-length older fragment, and it binds there at 0.51 against 0.50 -- close, and
// said so. Also measured while writing it: a fragment CROSSING the shaft never reaches
// the rescue (the plain fit's robust trim removes it), so the composites below are
// contamination running alongside the shaft.
//
// THE MUTATION (the issue's proof): drop the rival rule (rescue_rival_ratio's test in
// shaft_axis.hpp made unreachable) and this file must go red naming exactly "dart beside
// a rival-length older fragment" as RESCUED -- the one figure the rival rule refuses.
//
//   compiled by unit_check.sh (row 1586), no extra translation units.

#include <algorithm>
#include <cmath>
#include <iostream>
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

static void dedupe(std::vector<cv::Point> &pixels)
{
    std::sort(pixels.begin(), pixels.end(), [](const cv::Point &a, const cv::Point &b)
              { return a.y != b.y ? a.y < b.y : a.x < b.x; });
    pixels.erase(std::unique(pixels.begin(), pixels.end()), pixels.end());
}

static double lineAngleError(double gotDeg, double wantDeg)
{
    double d = std::fmod(std::fabs(gotDeg - wantDeg), 180.0);
    return d > 90.0 ? 180.0 - d : d;
}

/** Perpendicular distance of (cx,cy) from the observed line. */
static double offLine(const AxisObservation &o, double cx, double cy)
{
    const double dx = cx - o.point.x, dy = cy - o.point.y;
    return std::fabs(dx * o.direction.y - dy * o.direction.x);
}

static std::string figures(const AxisObservation &o)
{
    char buf[260];
    snprintf(buf, sizeof(buf),
             "[valid=%d tried=%d rescued=%d plainRms=%.2f rms=%.2f angle=%.2f fraction=%.2f "
             "rival=%.2f px=%d]",
             o.valid ? 1 : 0, o.rescueTried ? 1 : 0, o.rescued ? 1 : 0, o.rescuePlainRmsPx,
             o.centrelineRmsPx, o.angleDeg, o.rescueFraction, o.rescueRivalRatio,
             o.supportPixels);
    return buf + (o.valid ? std::string() : " refusal=" + o.refusal) +
           (o.rescueNote.empty() ? std::string() : " note=" + o.rescueNote);
}

struct Figure
{
    const char *name;
    std::vector<cv::Point> pixels;
    double dartAngle;     // the dart's own line; NAN where the figure holds no one dart
    double dartX, dartY;  // a point on the dart's centreline
};

int main()
{
    AxisParams plain;
    AxisParams rescue;
    rescue.rescue_composite = true;

    // ---- 1. a dart with something linked into it is recovered -------------------------
    std::vector<Figure> composites;
    {
        // An older dart's fragment lying against the new dart near its flight end,
        // linked into one figure: the fresh diff picks up where the new dart passes over
        // (or shakes) one already standing. A fragment CROSSING the shaft is not this
        // case -- the plain fit's robust trim already removes crossing columns (measured
        // while writing this file: a 56 px fragment at 75 deg to the shaft leaves the
        // plain fit valid at rms 0.28) -- so the figure that reaches the rescue is one
        // whose contamination runs ALONGSIDE the shaft for a stretch.
        Figure f{"dart touched by an older dart's fragment", {}, 25.0, 300, 300};
        addRod(f.pixels, 300, 300, 25, 170, 7);
        addRod(f.pixels, 300 + 55 * std::cos(25 * CV_PI / 180.0) - 22 * std::sin(25 * CV_PI / 180.0),
               300 + 55 * std::sin(25 * CV_PI / 180.0) + 22 * std::cos(25 * CV_PI / 180.0), 40, 34, 11);
        dedupe(f.pixels);
        composites.push_back(f);
    }
    {
        // A lobe hanging off one side of the shaft for a stretch: a cast shadow the
        // polarity classifier could not see (no images here), or a flight edge.
        Figure f{"dart with a one-sided lobe", {}, -40.0, 400, 300};
        addRod(f.pixels, 400, 300, -40, 160, 8);
        addDisc(f.pixels, 400 - 55 * std::cos(-40 * CV_PI / 180.0) + 16 * std::sin(-40 * CV_PI / 180.0),
                300 - 55 * std::sin(-40 * CV_PI / 180.0) - 16 * std::cos(-40 * CV_PI / 180.0), 14);
        dedupe(f.pixels);
        composites.push_back(f);
    }
    for (const Figure &f : composites)
    {
        const AxisObservation p = observeShaftAxis(f.pixels, plain);
        const AxisObservation r = observeShaftAxis(f.pixels, rescue);
        say(!p.valid && p.refusal.rfind("not straight:", 0) == 0,
            std::string("composite, plain: ") + f.name + " is refused as not straight " +
                figures(p));
        say(r.valid && r.rescued && lineAngleError(r.angleDeg, f.dartAngle) < 1.5 &&
                offLine(r, f.dartX, f.dartY) < 2.0,
            std::string("composite, rescue: ") + f.name + " comes back as the DART's line (" +
                detail::fmt("%.1f", f.dartAngle) + " deg) " + figures(r) + " off=" +
                detail::fmt("%.2f", offLine(r, f.dartX, f.dartY)));
        say(r.centrelineRmsPx <= rescue.max_centreline_rms_px &&
                (r.medianWidthPx > 0 ? r.extentPx / r.medianWidthPx : 0.0) >= rescue.min_elongation,
            std::string("composite, rescue: ") + f.name +
                " passed the ordinary straightness and elongation gates on its corridor");
    }

    // ---- 2. two objects stay refused, or the rescue lands on the dart ----------------
    std::vector<Figure> negatives;
    {
        Figure f{"two crossing rods", {}, NAN, 0, 0};
        addRod(f.pixels, 300, 300, 15, 120, 7);
        addRod(f.pixels, 310, 306, 60, 110, 7);
        dedupe(f.pixels);
        negatives.push_back(f);
    }
    {
        Figure f{"bent silhouette", {}, NAN, 0, 0};
        addRod(f.pixels, 260, 300, 5, 90, 6);
        addRod(f.pixels, 335, 330, 40, 90, 6);
        dedupe(f.pixels);
        negatives.push_back(f);
    }
    {
        Figure f{"parallel older dart", {}, NAN, 0, 0};
        addRod(f.pixels, 300, 300, 12, 110, 7);
        addRod(f.pixels, 300 + 80 * std::cos(12 * CV_PI / 180.0),
               300 + 80 * std::sin(12 * CV_PI / 180.0) + 26, 12, 110, 7);
        dedupe(f.pixels);
        negatives.push_back(f);
    }
    {
        Figure f{"shadow-dominated figure", {}, 20.0, 300, 300};
        addRod(f.pixels, 300, 300, 20, 90, 6);
        addRod(f.pixels, 330, 345, -35, 130, 34);
        dedupe(f.pixels);
        negatives.push_back(f);
    }
    {
        Figure f{"off-axis end blob", {}, 8.0, 300, 300};
        addRod(f.pixels, 300, 300, 8, 110, 7);
        addDisc(f.pixels, 300 + 72 * std::cos(8 * CV_PI / 180.0),
                300 + 72 * std::sin(8 * CV_PI / 180.0) + 26, 17);
        dedupe(f.pixels);
        negatives.push_back(f);
    }
    {
        Figure f{"near-end-on blob", {}, NAN, 0, 0};
        addDisc(f.pixels, 300, 300, 16);
        dedupe(f.pixels);
        negatives.push_back(f);
    }
    {
        // The same older dart's fragment as composite 1, but long enough (60 px against
        // the dart's 170) to be a straight object in its own right: the rival rule's
        // own case, refused whichever line the column vote favours.
        Figure f{"dart beside a rival-length older fragment", {}, NAN, 0, 0};
        addRod(f.pixels, 300, 300, 25, 170, 7);
        addRod(f.pixels, 300 + 55 * std::cos(25 * CV_PI / 180.0) - 22 * std::sin(25 * CV_PI / 180.0),
               300 + 55 * std::sin(25 * CV_PI / 180.0) + 22 * std::cos(25 * CV_PI / 180.0), 40, 60, 11);
        dedupe(f.pixels);
        negatives.push_back(f);
    }
    for (const Figure &f : negatives)
    {
        const AxisObservation r = observeShaftAxis(f.pixels, rescue);
        if (std::isnan(f.dartAngle))
        {
            say(!r.valid, std::string("two objects, rescue: ") + f.name +
                              " stays REFUSED " + figures(r));
        }
        else
        {
            const bool onDart = r.valid && lineAngleError(r.angleDeg, f.dartAngle) < 1.5 &&
                                offLine(r, f.dartX, f.dartY) < 2.0;
            say(!r.valid || onDart,
                std::string("dart plus distractor, rescue: ") + f.name +
                    (r.valid ? " is rescued onto the DART, not the distractor "
                             : " stays REFUSED ") +
                    figures(r));
        }
    }

    // ---- 3. the pin: rescue off is the plain fit, byte for byte ------------------------
    {
        const Figure &f = composites[0];
        const AxisObservation p = observeShaftAxis(f.pixels, plain);
        const AxisObservation q = observeShaftAxisOnce(f.pixels, cv::Mat(), cv::Mat(), plain);
        say(p.valid == q.valid && p.refusal == q.refusal && p.angleDeg == q.angleDeg &&
                p.centrelineRmsPx == q.centrelineRmsPx && !p.rescueTried,
            "pin: rescue_composite=false answers exactly the plain fit and never tries");
        std::vector<cv::Point> rod;
        addRod(rod, 300, 300, 33, 140, 7);
        dedupe(rod);
        const AxisObservation c = observeShaftAxis(rod, rescue);
        say(c.valid && !c.rescueTried,
            "a clean rod is valid on the plain fit, and the rescue never looks at it " +
                figures(c));
    }

    std::cout << (failures ? "I1586 RESCUE-CHECK FAILED " : "I1586 RESCUE-CHECK PASSED ")
              << failures << " failure(s)" << std::endl;
    return failures ? 1 : 0;
}
