// #1782: a flagged solve within its sigma of a ring wire AND a wedge wire offers the
// corner -- the three other cells by the solve's own covariance, an unused camera's clear
// reading of one of them first -- and the published score never moves.
//
// The rule is `score_processing::decideCornerCall`, pure, over #1556's BoundaryCall and the
// corner the solver measured (CornerCells, primitives). This file holds it on the live dart
// of 2026-10-10 16:38:53 (build 2b56b48) and on the cases it must leave alone.
//
//   g++ -std=c++17 -I src -I src/utils -o corner_check testers/i1782_corner_check.cpp
//       $(pkg-config --cflags --libs opencv4)
//
// THE LIVE DART, from turnaus#1782: a thrown S19 published T3, flagged "T3 or S3", from a
// two-line solve at 48.3 deg, at 188.03 deg and radius 0.6346, 5.3 mm sigma. On the fitted
// model 0.6 mm inside the treble's outer wire and 1.8 mm on the 3 side of the 3/19 wire
// (189 deg: 0.6346 x 170 x sin 0.97 deg = 1.83 mm). The four cells: T3 (published), S3
// across the ring wire, T19 across the wedge wire, S19 across both. The log gave one sigma
// (5.3 mm, across the ring wire it named); the wedge's is not in it, so the check holds the
// live shape at every tangential sigma from the 5 mm floor to 9.3 mm (#1766's largest
// fixture two-line figure) and at correlations -0.6..0.6, and S19 must be offered at all.
//
// FOUR PREFIXES, and the mutation harness (i1782_check.sh) counts them:
//   reach: the assertion depends on the corner threshold -- the 8 mm dart is NOT a corner;
//   clear: depends on an unused camera's clear reading going first (the live one is S19,
//          the diagonal, so a mutation that loses the diagonal turns these red too);
//   diag:  depends on the cell across BOTH wires being a cell at all;
//   pure:  none of those three (the probability arithmetic, the switch, what publishes).
// Each assertion carries exactly one prefix, so a mutation's prediction is a count.
//
// The fixture half is testers/i1782_census.py over the #1555 bakeoff's logs (I1782CORNER
// lines): how many flagged darts are corners, where the truth sits, and how often a camera
// the solve did not use read a corner cell. docs/rig.md records the count.

#include <cmath>
#include <iostream>
#include <string>
#include <vector>

#include "detector/geometry/detection/dart_candidates.hpp"
#include "detector/geometry/detection/score_processing.hpp"

using namespace score_processing;

static int failures = 0;
static int counts[4] = {0, 0, 0, 0};

static void say(bool ok, const std::string &what)
{
    std::cout << (ok ? "OK   " : "FAIL ") << what << std::endl;
    if (!ok)
    {
        failures++;
    }
    const char *p[4] = {"reach:", "clear:", "diag:", "pure:"};
    for (int i = 0; i < 4; i++)
    {
        if (what.rfind(p[i], 0) == 0)
        {
            counts[i]++;
        }
    }
}

static bool offers(const BoundaryCall &c, const std::string &s)
{
    if (c.alternative == s)
    {
        return true;
    }
    for (const std::string &o : c.others)
    {
        if (o == s)
        {
            return true;
        }
    }
    return false;
}

/** #1556's call on the live dart, as decideBoundaryCall made it. */
static BoundaryCall liveNearest()
{
    return decideBoundaryCall(true, true, "T3", "S3", "ring", 0.6, 5.3);
}

static CornerCells liveCells(double wedgeMm = 1.8, double sigmaWedge = 5.3, double rho = 0.0)
{
    CornerCells c;
    c.corner = true;
    c.ringAlt = "S3";
    c.wedgeAlt = "T19";
    c.diagonal = "S19";
    c.ringMm = 0.6;
    c.wedgeMm = wedgeMm;
    c.sigmaRingMm = 5.3;
    c.sigmaWedgeMm = sigmaWedge;
    c.rho = rho;
    return c;
}

static CameraReading reading(int cam, bool used, const std::string &score, float wedge, float ring)
{
    CameraReading r;
    r.camera = cam;
    r.used = used;
    r.score = score;
    r.wedgeMarginMm = wedge;
    r.ringMarginMm = ring;
    return r;
}

int main()
{
    // ---- the probability arithmetic -------------------------------------------------
    {
        const double pi = 3.14159265358979323846;
        bool ok = true;
        for (double rho : {-0.9, -0.5, 0.0, 0.3, 0.8})
        {
            const double want = 0.25 + std::asin(rho) / (2.0 * pi);
            ok = ok && std::fabs(bivariateNormalCdf(0.0, 0.0, rho) - want) < 1e-5;
        }
        say(ok, "pure: P(X<0,Y<0) = 1/4 + asin(rho)/2pi at five correlations");
        say(std::fabs(bivariateNormalCdf(1.0, -0.5, 0.0) - normalCdf(1.0) * normalCdf(-0.5)) < 1e-9,
            "pure: rho 0 is the product of the marginals");
        say(std::fabs(bivariateNormalCdf(9.0, 0.7, 0.6) - normalCdf(0.7)) < 1e-5,
            "pure: a far-off first wire leaves the second's marginal");
        double pPub = 0.0;
        const std::vector<CornerCell> r = rankCornerCells(liveCells(), "T3", &pPub);
        double sum = pPub;
        for (const CornerCell &c : r)
        {
            sum += c.probability;
        }
        say(std::fabs(sum - 1.0) < 1e-6, "diag: the four cells of the live corner sum to one");
    }

    // ---- the live dart ---------------------------------------------------------------
    const BoundaryCall nearest = liveNearest();
    say(nearest.flagged && nearest.alternative == "S3" && nearest.kind == "ring",
        "pure: #1556 on the live shape flags T3 or S3 across the ring wire (what Turnaus received)");
    {
        // No camera outside the solve read anything clear: the covariance alone.
        const BoundaryCall c = decideCornerCall(nearest, liveCells(), {}, "", 1.0, true);
        std::cout << "  live, covariance only: " << c.account << std::endl;
        say(c.published == "T3" && c.flagged, "pure: the live corner still publishes T3, flagged");
        say(offers(c, "S19"), "diag: live (188.03 deg, 0.6346, 5.3 mm) offers S19 among its alternatives");
        say(c.alternative == "S3" && c.others.size() == 2 && c.others[0] == "T19" && c.others[1] == "S19",
            "diag: by probability alone the order is S3, T19, S19 (T3 34%, then 29/20/17 at rho 0)");
        say(c.account.rfind("UNCERTAINTY: T3 or S3, T19, S19 -- a corner", 0) == 0,
            "diag: the sentence names the corner and all three cells");
    }
    {
        // Every tangential sigma from the floor to #1766's largest fixture two-line figure,
        // and correlations either way: the diagonal is offered in every one.
        bool all = true;
        for (double sw : {5.0, 5.3, 6.5, 8.0, 9.3})
        {
            for (double rho : {-0.6, -0.3, 0.0, 0.3, 0.6})
            {
                const BoundaryCall c = decideCornerCall(nearest, liveCells(1.8, sw, rho), {}, "", 1.0, true);
                all = all && offers(c, "S19") && c.published == "T3";
            }
        }
        say(all, "diag: S19 is offered at every wedge sigma 5.0..9.3 mm and rho -0.6..0.6");
    }
    {
        // #1675's shape: the camera the solve did not use read S19 clear of every wire.
        // LONE-WIRE said 5.5 mm from a wedge wire; its ring margin is not in that line, so
        // it is set clear here and the case below holds the margin that is not.
        const BoundaryCall c = decideCornerCall(nearest, liveCells(), {reading(0, false, "S19", 5.5f, 6.0f),
                                                                      reading(1, true, "T3", 2.0f, 0.8f)},
                                                "", 1.0, true);
        std::cout << "  live, unused camera clear: " << c.account << std::endl;
        say(c.alternative == "S19", "clear: an unused camera's clear S19 is the first alternative (Turnaus's `alternative`)");
        say(c.others.size() == 2 && c.others[0] == "S3" && c.others[1] == "T19",
            "clear: the rest keep their probability order behind it");
        say(c.published == "T3", "pure: and the published score is still T3");
    }
    {
        // The same reading from a camera the solve DID use is not #1675's shape.
        const BoundaryCall c =
            decideCornerCall(nearest, liveCells(), {reading(1, true, "S19", 5.5f, 6.0f)}, "", 1.0, true);
        say(c.alternative == "S3", "pure: a USED camera's clear S19 does not reorder the corner");
    }
    {
        // turnaus#1782's counter-case: an unused camera's reading INSIDE its own sigma is
        // another coin (16:49:14/15: 1.5 and 2.9 mm, one right and one wrong).
        const BoundaryCall c =
            decideCornerCall(nearest, liveCells(), {reading(0, false, "S19", 2.9f, 6.0f)}, "", 1.0, true);
        say(c.alternative == "S3", "pure: an unused camera's S19 2.9 mm from its wedge wire is not promoted");
        const BoundaryCall d =
            decideCornerCall(nearest, liveCells(), {reading(0, false, "S19", 5.5f, 1.2f)}, "", 1.0, true);
        say(d.alternative == "S3", "pure: nor one 1.2 mm from its ring wire -- clear means every wire");
        const BoundaryCall e =
            decideCornerCall(nearest, liveCells(), {reading(0, false, "S17", 9.0f, 9.0f)}, "", 1.0, true);
        say(e.alternative == "S3", "pure: a clear reading of a cell outside the corner promotes nothing");
    }

    // ---- not a corner ---------------------------------------------------------------
    {
        // The same T3 with its wedge wire 8 mm away: outside the 5.3 mm sigma, so #1556's
        // nearest-wire call stands and S19 is not offered.
        const BoundaryCall c = decideCornerCall(nearest, liveCells(8.0), {}, "", 1.0, true);
        say(!offers(c, "S19") && !offers(c, "T19"), "reach: a dart 8 mm from the wedge wire does not offer S19 or T19");
        say(c.alternative == "S3" && c.others.empty() && c.account == nearest.account,
            "reach: and its flag is #1556's, byte for byte");
        // and with an unused camera reading S19 clear beside it: no corner, nothing promoted.
        const BoundaryCall d =
            decideCornerCall(nearest, liveCells(8.0), {reading(0, false, "S19", 6.0f, 6.0f)}, "", 1.0, true);
        say(d.alternative == "S3", "reach: an unused camera's S19 does not reach a call that is not a corner");
    }
    {
        // Not flagged at all: nothing changes, corner or not.
        const BoundaryCall clear = decideBoundaryCall(true, false, "T3", "S3", "ring", 6.0, 5.3);
        const BoundaryCall c = decideCornerCall(clear, liveCells(), {reading(0, false, "S19", 6.0f, 6.0f)}, "",
                                                1.0, true);
        say(!c.flagged && c.alternative.empty() && c.others.empty() && c.account == clear.account,
            "pure: an unflagged call is returned untouched");
        const BoundaryCall vote = decideBoundaryCall(false, false, "", "", "", -1, -1);
        say(decideCornerCall(vote, liveCells(), {}, "", 1.0, true).account.empty(),
            "pure: a vote publish stays silent (#1556 rule 1)");
    }
    {
        // The switch: OD_CORNER_FLAG=nearest offers #1556's one alternative and says what the
        // corner would have offered.
        const BoundaryCall c = decideCornerCall(nearest, liveCells(), {reading(0, false, "S19", 5.5f, 6.0f)}, "",
                                                1.0, false);
        say(c.alternative == "S3" && c.others.empty() && c.published == "T3",
            "pure: OD_CORNER_FLAG=nearest restores the one alternative");
        say(c.account.find("the corner would offer S19, S3, T19") != std::string::npos,
            "clear: and its sentence says what the corner would offer, the unused camera's S19 first");
    }
    {
        // A single beside the 25 ring: the diagonal reads OUTER again, and is merged.
        CornerCells c;
        c.corner = true;
        c.ringAlt = "OUTER";
        c.wedgeAlt = "S1";
        c.diagonal = "OUTER";
        c.ringMm = 1.0;
        c.wedgeMm = 1.0;
        c.sigmaRingMm = 5.0;
        c.sigmaWedgeMm = 5.0;
        const BoundaryCall n = decideBoundaryCall(true, true, "S20", "OUTER", "ring", 1.0, 5.0);
        const BoundaryCall d = decideCornerCall(n, c, {}, "", 1.0, true);
        say(d.alternative == "OUTER" && d.others.size() == 1 && d.others[0] == "S1",
            "pure: a diagonal that repeats a cell is merged into it, not offered twice");
    }
    {
        // A strongly correlated ellipse moves the diagonal up: the order is the covariance's.
        const BoundaryCall c = decideCornerCall(nearest, liveCells(1.8, 5.3, 0.9), {}, "", 1.0, true);
        std::vector<std::string> order{c.alternative};
        order.insert(order.end(), c.others.begin(), c.others.end());
        auto at = [&](const std::string &x)
        {
            for (size_t i = 0; i < order.size(); i++)
            {
                if (order[i] == x)
                {
                    return (int)i;
                }
            }
            return 99;
        };
        std::cout << "  rho 0.9: " << c.account << std::endl;
        say(order.size() == 3 && at("S19") < at("T19") && at("S19") < at("S3"),
            "diag: at rho 0.9 toward both wires the diagonal is the first alternative");
    }

    {
        // dart_candidates (#1721's ranking) on the corner, built by hand so no mutation of
        // score_processing.hpp reaches it: a corner cell another camera READ comes straight
        // after the alternative, then the corner's rest, then the other readings.
        dart_candidates::Evidence e;
        e.published = "T3";
        e.ring = "triple";
        e.segment = 3;
        e.wedge_read = true;
        e.radius_known = true;
        e.radius = 0.6346f;
        e.angle_known = true;
        e.angle = 188.03f;
        e.alternative = "S3";
        e.corner = {"T19", "S19"};
        e.others = {{"S19", false}, {"T3", false}};
        const std::vector<std::string> withRead = dart_candidates::rank(e);
        say(withRead.size() >= 3 && withRead[0] == "S3" && withRead[1] == "S19" && withRead[2] == "T19",
            "pure: candidates on the live corner with a camera reading S19: S3, S19, T19 (Turnaus's live order kept)");
        e.others = {{"S17", false}};
        const std::vector<std::string> unread = dart_candidates::rank(e);
        say(unread.size() >= 4 && unread[0] == "S3" && unread[1] == "T19" && unread[2] == "S19" && unread[3] == "S17",
            "pure: with no camera reading a corner cell: S3, T19, S19, then the other reading");
    }

    std::cout << "SENSITIVE reach=" << counts[0] << " clear=" << counts[1] << " diag=" << counts[2]
              << " pure=" << counts[3] << std::endl;
    std::cout << "FAILURES: " << failures << std::endl;
    return failures == 0 ? 0 : 1;
}
