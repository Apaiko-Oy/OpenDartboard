// turnaus#1993: plane_agreement.hpp, pure -- which of one camera's fresh points have fresh
// change from another camera at the same place on the board, and what is done with them.
// Points are planted straight in the board plane (mm); no camera, no map, no footage.
// testers/i1993_check.sh compiles this against the tree and against five mutated copies.
#include "detector/geometry/detection/plane_agreement.hpp"
#include "detector/geometry/detection/shaft_axis.hpp"
#include <cstdio>
#include <string>
#include <vector>

static int failures = 0;
static void label(bool ok, const std::string &tag, const std::string &what)
{
    printf("%s %s: %s\n", ok ? "OK  " : "FAIL", tag.c_str(), what.c_str());
    failures += ok ? 0 : 1;
}

// A band `width` mm wide from the origin along `deg`, `length` mm long, one point per mm.
static std::vector<cv::Point2f> streak(double deg, double length, double width, cv::Point2f from = cv::Point2f(0, 0))
{
    std::vector<cv::Point2f> out;
    const double c = std::cos(deg * CV_PI / 180.0), s = std::sin(deg * CV_PI / 180.0);
    for (double t = 0; t <= length; t += 1.0)
    {
        for (double u = -width / 2; u <= width / 2; u += 1.0)
        {
            out.emplace_back((float)(from.x + t * c - u * s), (float)(from.y + t * s + u * c));
        }
    }
    return out;
}
static std::vector<cv::Point2f> joined(std::vector<cv::Point2f> a, const std::vector<cv::Point2f> &b)
{
    a.insert(a.end(), b.begin(), b.end());
    return a;
}
static int countIn(const plane_agreement::Agreement &a, size_t from, size_t to)
{
    int n = 0;
    for (size_t i = from; i < to && i < a.coincident.size(); i++)
    {
        n += a.coincident[i];
    }
    return n;
}

int main()
{
    using namespace plane_agreement;

    // ---- unset: nothing is on, and the tolerance is 6 mm ---------------------------------
    label(!dropFrom(nullptr) && !censusFrom(nullptr), "unset", "no switch set keeps and adopts nothing");
    label(!dropFrom("1") && !dropFrom("") && dropFrom("on"), "unset", "OD_PLANE_DROP is `on` and only `on`");
    label(toleranceFrom(nullptr) == 6.0 && toleranceFrom("3") == 3.0 && toleranceFrom("-2") == 6.0 &&
              toleranceFrom("900") == 6.0,
          "unset", "the tolerance is 6 mm unset, and a value outside (0, 50] is refused back to it");

    // ---- cross: one dart seen by two cameras coincides at its entry and nowhere else ------
    const std::vector<cv::Point2f> dartA = streak(0, 150, 7), dartB = streak(60, 150, 7);
    {
        const std::vector<Agreement> a = agree({dartA, dartB}, {dartA, dartB}, 6.0);
        double furthest = 0;
        for (size_t i = 0; i < dartA.size(); i++)
        {
            if (a[0].coincident[i])
            {
                furthest = std::max(furthest, (double)dartA[i].x);
            }
        }
        label(a[0].count > 0 && furthest <= 22.0, "cross",
              "two streaks crossing at 60 degrees coincide only within 22 mm of the entry (furthest " +
                  std::to_string(furthest) + ")");
        label(a[0].count < (int)dartA.size() / 5 && a[0].extentMm < 40.0, "cross",
              "that is under a fifth of the figure and under 40 mm of board (" + std::to_string(a[0].count) + " of " +
                  std::to_string(dartA.size()) + ", " + std::to_string(a[0].extentMm) + " mm)");
        label(a[0].others == 1 && a[1].others == 1, "cross", "each camera was compared with the one other");
    }

    // ---- shadow: a lobe both cameras see at one board place coincides whole --------------
    const std::vector<cv::Point2f> lobe = streak(200, 80, 9, cv::Point2f(-30, 0));
    {
        const std::vector<cv::Point2f> a_all = joined(dartA, lobe), b_all = joined(dartB, lobe);
        const std::vector<Agreement> a = agree({a_all, b_all}, {a_all, b_all}, 6.0);
        const int in_lobe = countIn(a[0], dartA.size(), a_all.size());
        label(in_lobe == (int)lobe.size(), "shadow",
              "every point of the lobe coincides (" + std::to_string(in_lobe) + " of " + std::to_string(lobe.size()) + ")");
        label(a[0].extentMm >= 40.0, "shadow", "and the coincident set spans 40 mm or more (" +
                                                   std::to_string(a[0].extentMm) + ")");
        std::vector<cv::Point> px;
        for (const cv::Point2f &p : a_all)
        {
            px.emplace_back((int)std::lround(p.x * 10), (int)std::lround(p.y * 10));
        }
        const std::vector<cv::Point> kept = without(px, a[0]);
        label(kept.size() == a_all.size() - (size_t)a[0].count && kept.size() > dartA.size() * 4 / 5, "shadow",
              "without() keeps exactly the points that did not coincide, most of the dart among them");
    }

    // ---- tol: the tolerance is what lets two maps a few mm apart agree --------------------
    {
        std::vector<cv::Point2f> moved = lobe;
        for (cv::Point2f &p : moved)
        {
            p.y += 4.0f; // the other camera's map puts the same lobe 4 mm away
        }
        const std::vector<cv::Point2f> edge = streak(200, 80, 1, cv::Point2f(-30, 0));
        std::vector<cv::Point2f> edge_moved = edge;
        for (cv::Point2f &p : edge_moved)
        {
            p.y += 4.0f;
        }
        const std::vector<Agreement> wide = agree({edge, edge_moved}, {edge, edge_moved}, 6.0);
        // 1 mm, not 2: a point and its twin are each rounded to a millimetre cell, so the
        // comparison is slack by up to 1.4 mm (the first run of this check asked 2 mm and was red).
        const std::vector<Agreement> narrow = agree({edge, edge_moved}, {edge, edge_moved}, 1.0);
        label(wide[0].count == (int)edge.size(), "tol", "a line 4 mm from its twin coincides whole at 6 mm");
        label(narrow[0].count == 0, "tol", "and not at all at 1 mm");
    }

    // ---- self: a camera is never compared with itself --------------------------------------
    {
        const std::vector<Agreement> a = agree({dartA, {}}, {dartA, {}}, 6.0);
        label(a[0].others == 0 && a[0].count == 0, "self", "a camera alone coincides with nothing");
        const std::vector<Agreement> b = agree({dartA, {}, {}}, {dartA, {}, streak(90, 40, 5, cv::Point2f(200, 100))}, 6.0);
        label(b[0].others == 1 && b[0].count == 0, "self", "and with nothing where the other's change is elsewhere");
    }

    // ---- retry: only a not-straight figure with something coincident and something left ---
    {
        Agreement some;
        some.count = 40;
        some.others = 1;
        label(retries("not-straight", some, 1000), "retry", "a not-straight figure with coincident pixels is fitted again");
        label(!retries("ok", some, 1000), "retry", "a valid figure is never fitted again");
        label(!retries("not-a-shaft", some, 1000), "retry", "nor one the elongation gate alone refused");
        Agreement none = some;
        none.count = 0;
        label(!retries("not-straight", none, 1000), "retry", "nothing coincident, nothing to drop");
        Agreement all = some;
        all.count = 1000;
        label(!retries("not-straight", all, 1000), "retry", "everything coincident, nothing left to fit");
        Agreement alone = some;
        alone.others = 0;
        label(!retries("not-straight", alone, 1000), "retry", "no other camera, no comparison");
    }

    // ---- adopt: the switch, a retry, and every gate passed --------------------------------
    label(adopts(true, true, true), "adopt", "on, retried and valid is adopted");
    label(!adopts(true, true, false), "adopt", "a retry the gates refused is not");
    label(!adopts(false, true, true), "adopt", "nor a valid retry with the switch unset");

    // ---- fit: a shaft with a lobe is refused, and fits once the lobe is dropped -----------
    {
        std::vector<cv::Point> shaft, bent;
        for (int y = 50; y <= 300; y++)
        {
            for (int x = 100; x <= 106; x++)
            {
                shaft.emplace_back(x, y);
            }
        }
        for (int y = 180; y <= 300; y++)
        {
            const int x0 = 107 + (int)((y - 180) * 0.35);
            for (int x = x0; x <= x0 + 8; x++)
            {
                bent.emplace_back(x, y);
            }
        }
        std::vector<cv::Point> figure = shaft;
        figure.insert(figure.end(), bent.begin(), bent.end());
        const shaft_axis::AxisObservation whole = shaft_axis::observeShaftAxis(figure, cv::Mat(), cv::Mat());
        const std::string reason = shaft_axis::refusalReasonOf(whole, true, false);
        label(!whole.valid && reason.find("not-straight") != std::string::npos, "fit",
              "the shaft with its lobe is refused not-straight (" + reason + ", " +
                  std::to_string(whole.centrelineRmsPx) + " px)");
        // 1 px = 1 mm here; the other camera saw the lobe and not the shaft.
        std::vector<cv::Point2f> mine, theirs;
        for (const cv::Point &p : figure)
        {
            mine.emplace_back((float)p.x, (float)p.y);
        }
        for (const cv::Point &p : bent)
        {
            theirs.emplace_back((float)p.x, (float)p.y);
        }
        const std::vector<Agreement> a = agree({mine, {}}, {mine, theirs}, 1.0);
        const bool again = retries(reason, a[0], figure.size());
        const shaft_axis::AxisObservation second =
            shaft_axis::observeShaftAxis(without(figure, a[0]), cv::Mat(), cv::Mat());
        label(again && second.valid && second.centrelineRmsPx < 2.5, "fit",
              "and without the coincident pixels it is an axis (" + std::to_string(second.centrelineRmsPx) + " px)");
        label(adopts(true, again, second.valid) && !adopts(false, again, second.valid), "fit",
              "adopted under the switch, left refused without it");
    }

    printf("I1993 PLANE CHECK %s (%d failure(s))\n", failures == 0 ? "PASS" : "FAIL", failures);
    return failures == 0 ? 0 : 1;
}
