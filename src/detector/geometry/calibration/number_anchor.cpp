#include "number_anchor.hpp"
#include "wire_model.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <numeric>

namespace number_anchor
{
    namespace
    {
        /**
         * The board's own millimetres. Stated here because this module is pure, and the
         * same three numbers #1497's census states for the same reason.
         */
        constexpr double kDoublesMm = 170.0; // the outer double wire
        constexpr double kRimMm = 225.5;     // the board's rim

        /** The number ring: the annulus between the doubles and the rim, in board units. */
        constexpr double kRingInner = 1.0;
        constexpr double kRingOuter = kRimMm / kDoublesMm;

        /**
         * The control ring, and it is NOT a second guess at where the numbers are: it is
         * the same depth of annulus placed where a board prints no numbers at all -- the
         * big singles, between the treble ring's outside (107 mm) and the doubles ring's
         * inside (162.5 mm). Same machinery, same cell count, same picture size, nothing
         * to read.
         */
        constexpr double kNullInner = 107.0 / kDoublesMm;
        constexpr double kNullOuter = 162.5 / kDoublesMm;

        /**
         * How finely a cell is sampled, in pixels per board millimetre. 2.0 is above
         * every scale #1497 measured on either fixture, so every cell is an upsample of
         * the pixels that are really there and none invents detail; it is also what makes
         * a near cell and a far cell the same picture, which is the whole point of
         * rectifying at all.
         */
        constexpr double kPxPerMm = 2.0;

        /**
         * The glyph heights tried, as fractions of the annulus depth. A BRACKET rather
         * than a size: a numeral has to fit inside the ring it is printed in, and every
         * one of these is tried for every cell and every number, so the winner chooses
         * its own and nothing here is fitted to a board (#1322).
         */
        const double kGlyphHeights[] = {0.25, 0.35, 0.45, 0.55};

        /**
         * How far a template is softened before it is compared, in pixels of the
         * rectified cell. A BRACKET again, and for a measured reason: #1497 found the
         * glyph 12 px tall at the far edge of the ring and 52 at the near one, so a far
         * cell arrives at 2 px/mm as an upsample of almost nothing and a near one arrives
         * sharp. A crisp template correlates poorly with a smeared glyph, so every
         * template is offered at every softness and the cell takes the one that fits it.
         */
        const double kGlyphBlurs[] = {0.6, 1.6, 3.0};

        /** The sequence, clockwise from the 20. The scorer's own, stated again here. */
        const int kSequence[20] = {20, 1, 18, 4, 13, 6, 10, 15, 2, 17, 3, 19, 7, 16, 8, 11, 14, 9, 12, 5};

        double intoTurn(double a)
        {
            const double twoPi = 2.0 * wire_model::kPi;
            a = std::fmod(a, twoPi);
            if (a < 0)
            {
                a += twoPi;
            }
            return a;
        }

        /** One number, drawn white on black at a stated cap height in pixels. */
        cv::Mat glyphOf(int number, int heightPx)
        {
            const std::string text = std::to_string(number);
            const int thickness = std::max(1, (int)std::lround(heightPx / 9.0));
            int baseline = 0;
            const cv::Size unit = cv::getTextSize(text, cv::FONT_HERSHEY_SIMPLEX, 1.0, thickness, &baseline);
            if (unit.height <= 0)
            {
                return cv::Mat();
            }
            const double scale = (double)heightPx / (double)unit.height;
            const cv::Size sz = cv::getTextSize(text, cv::FONT_HERSHEY_SIMPLEX, scale, thickness, &baseline);
            const int pad = 2;
            if (sz.width <= 0 || sz.height <= 0)
            {
                return cv::Mat();
            }
            cv::Mat canvas(sz.height + 2 * pad, sz.width + 2 * pad, CV_8U, cv::Scalar(0));
            cv::putText(canvas, text, cv::Point(pad, sz.height + pad - 1), cv::FONT_HERSHEY_SIMPLEX,
                        scale, cv::Scalar(255), thickness, cv::LINE_AA);
            return canvas;
        }

        /**
         * Remove the row and column means, exactly.
         *
         * This is property 1 of the header made arithmetic. A cell that scores high
         * against everything and a template that scores high against everything are both
         * removed, and what is left is only the part of a score that belongs to the PAIR
         * -- which is the only part any candidate rotation can be told apart by, since
         * every candidate uses every cell once and every template once.
         */
        void doubleCentre(std::vector<std::vector<double>> &s)
        {
            const int n = (int)s.size();
            if (n == 0)
            {
                return;
            }
            const int m = (int)s[0].size();
            std::vector<double> rowMean(n, 0.0), colMean(m, 0.0);
            double grand = 0.0;
            for (int i = 0; i < n; i++)
            {
                for (int j = 0; j < m; j++)
                {
                    rowMean[i] += s[i][j];
                    colMean[j] += s[i][j];
                    grand += s[i][j];
                }
            }
            for (int i = 0; i < n; i++)
            {
                rowMean[i] /= m;
            }
            for (int j = 0; j < m; j++)
            {
                colMean[j] /= n;
            }
            grand /= (double)(n * m);
            for (int i = 0; i < n; i++)
            {
                for (int j = 0; j < m; j++)
                {
                    s[i][j] = s[i][j] - rowMean[i] - colMean[j] + grand;
                }
            }
        }
    }

    bool notAsked()
    {
        static const bool v = []
        {
            const char *e = std::getenv("OD_NUMBER_ANCHOR");
            return e != nullptr && std::string(e) == "off";
        }();
        return v;
    }

    bool readsTheNumberlessRing()
    {
        static const bool v = []
        {
            const char *e = std::getenv("OD_NUMBER_ANCHOR");
            return e != nullptr && std::string(e) == "inner";
        }();
        return v;
    }

    double minimumSeparation()
    {
        static const double asked = []
        {
            const char *e = std::getenv("OD_NUMBER_ANCHOR_MIN");
            const double v = e ? std::atof(e) : -1.0;
            return (v > 0.0 && v <= 40.0) ? v : -1.0;
        }();
        // MEASURED, not chosen: testers/i1498_run.sh section 3's sweep.
        return (asked > 0.0) ? asked : 2.75;
    }

    Reading readTheNumbers(const cv::Mat &frame,
                           const std::vector<cv::Point2f> &endpoints,
                           const cv::RotatedRect &conic,
                           const cv::Point2f &bull,
                           double conicOfDoubles,
                           const std::string &debugDir)
    {
        Reading out;
        if (notAsked())
        {
            out.why = "the number reader was not asked (OD_NUMBER_ANCHOR=off)";
            return out;
        }
        if (frame.empty() || (int)endpoints.size() != wire_model::kFold)
        {
            out.why = "there is no whole wire ring to cut the number ring into cells with";
            return out;
        }

        const wire_model::Plane plane = wire_model::planeOf(conic, bull, conicOfDoubles);
        if (!plane.built)
        {
            out.why = "no board plane was built, so no cell of the number ring can be placed";
            return out;
        }

        const double inner = readsTheNumberlessRing() ? kNullInner : kRingInner;
        const double outer = readsTheNumberlessRing() ? kNullOuter : kRingOuter;

        // The cell picture is the SAME SIZE whichever annulus is read, so the control
        // differs from the measurement in what is printed there and in nothing else.
        const int rectH = (int)std::lround((kRimMm - kDoublesMm) * kPxPerMm);
        const int rectW = (int)std::lround(2.0 * std::sin(wire_model::kSector * 0.5) *
                                           ((kDoublesMm + kRimMm) * 0.5) * kPxPerMm);
        if (rectH < 4 || rectW < 4)
        {
            out.why = "the number ring is too thin to sample";
            return out;
        }

        // The board angle of every wire, in the ring's own index order. A cell is what
        // lies between two neighbouring wires, so a cell's index IS the wire index the
        // scorer would start counting from -- nothing has to be translated afterwards.
        std::vector<double> wireAngle(wire_model::kFold, 0.0);
        for (int j = 0; j < wire_model::kFold; j++)
        {
            wireAngle[j] = wire_model::boardAngleOf(plane, endpoints[j]);
        }

        cv::Mat grey;
        if (frame.channels() == 3)
        {
            cv::cvtColor(frame, grey, cv::COLOR_BGR2GRAY);
        }
        else
        {
            grey = frame;
        }

        std::vector<cv::Mat> cells(wire_model::kFold);
        for (int j = 0; j < wire_model::kFold; j++)
        {
            const double from = wireAngle[j];
            const double span = intoTurn(wireAngle[(j + 1) % wire_model::kFold] - from);
            cv::Mat mapX(rectH, rectW, CV_32F), mapY(rectH, rectW, CV_32F);
            for (int v = 0; v < rectH; v++)
            {
                // Down the picture is INWARD, and that is not a taste. A raster's
                // (column, row) is (x, y-down), so a cell sampled with the column running
                // the way the board's angle runs and the row running OUTWARD is a
                // reflection of what a camera saw -- and a mirrored glyph matches no
                // template of a number, whichever way up it is turned. Measured: with the
                // row running outward the best rotation on every camera of both fixtures
                // cleared its rivals by 2.3 to 3.0 standard deviations and disagreed with
                // the star camera; with it running inward, see testers/i1498_run.sh.
                const double t = 1.0 - (double)v / (rectH - 1);
                const double r = inner + t * (outer - inner);
                for (int u = 0; u < rectW; u++)
                {
                    const double s = (double)u / (rectW - 1);
                    const cv::Point2f p = wire_model::imageOfBoardAngle(plane, from + s * span, r);
                    mapX.at<float>(v, u) = p.x;
                    mapY.at<float>(v, u) = p.y;
                }
            }
            cv::Mat cell;
            cv::remap(grey, cell, mapX, mapY, cv::INTER_LINEAR, cv::BORDER_REPLICATE);
            cv::GaussianBlur(cell, cell, cv::Size(3, 3), 0);
            cv::Mat f;
            cell.convertTo(f, CV_32F);
            // Each cell on its own terms first: a near cell is brighter and higher in
            // contrast than a far one, and nothing here is about brightness.
            cv::Scalar mu, sd;
            cv::meanStdDev(f, mu, sd);
            cells[j] = (sd[0] > 1e-6) ? (f - mu[0]) / sd[0] : cv::Mat::zeros(f.size(), CV_32F);
        }

        // WHAT IS COMMON TO ALL TWENTY CELLS IS NOT A NUMBER -- AND SUBTRACTING IT
        // MADE THIS WORSE, which is worth the four lines it takes to say. The board
        // prints a white circle round the outside of its numbers; it crosses every cell
        // edge to edge at the same board radius in every one of them, and several
        // numerals touch it -- the thing #1497 removed its own tape measure for, because
        // every rule that separated the two was a radius fitted to this board's ring
        // (#1322). It does not have to be separated at all: taken across the twenty
        // cells the circle is the MEDIAN and a numeral is an outlier, so the per-pixel
        // median over the ring can simply be subtracted, naming no radius and no
        // fraction. It was built, and measured, and it is not here: on the six cameras it
        // moved the reading of none of them and moved the CONFIDENCE the wrong way --
        // the numberless control ring rose from 0.31-0.44 to 0.35-0.65 while the number
        // ring fell from 0.98-1.97 to 0.86-1.36, because what is left of a ring with no
        // numbers in it after its own median is removed is noise, and noise correlates
        // with something. The circle is common to every cell, so it is removed exactly by
        // `doubleCentre` further down -- where every other thing every cell shares is
        // removed too, and where it costs nothing.
        out.attempted = true;
        out.cells = wire_model::kFold;

        // The twenty templates, one per SEQUENCE POSITION, each at every height in the
        // bracket. Built once and matched against every cell.
        std::vector<std::vector<cv::Mat>> templates(wire_model::kFold);
        for (int m = 0; m < wire_model::kFold; m++)
        {
            for (double frac : kGlyphHeights)
            {
                cv::Mat g = glyphOf(kSequence[m], (int)std::lround(frac * rectH));
                if (g.empty() || g.rows > rectH || g.cols > rectW)
                {
                    continue;
                }
                for (double sigma : kGlyphBlurs)
                {
                    cv::Mat soft;
                    cv::GaussianBlur(g, soft, cv::Size(0, 0), sigma);
                    cv::Mat f;
                    soft.convertTo(f, CV_32F);
                    templates[m].push_back(f);
                }
            }
            if (templates[m].empty())
            {
                out.why = "no template of the number " + std::to_string(kSequence[m]) +
                          " fits inside a cell of this ring";
                return out;
            }
        }

        // The score matrix, once per half-turn. `flip` 0 is the cell as it was sampled --
        // outward is down the picture, the board's own angle runs left to right -- and 1
        // is the same cell turned through half a turn. Neither is assumed (property 4).
        std::vector<std::vector<std::vector<double>>> scores(
            2, std::vector<std::vector<double>>(wire_model::kFold, std::vector<double>(wire_model::kFold, 0.0)));
        for (int flip = 0; flip < 2; flip++)
        {
            for (int j = 0; j < wire_model::kFold; j++)
            {
                cv::Mat cell = cells[j];
                if (flip == 1)
                {
                    cv::Mat turned;
                    cv::rotate(cell, turned, cv::ROTATE_180);
                    cell = turned;
                }
                for (int m = 0; m < wire_model::kFold; m++)
                {
                    double best = -1.0;
                    for (const cv::Mat &t : templates[m])
                    {
                        cv::Mat result;
                        cv::matchTemplate(cell, t, result, cv::TM_CCOEFF_NORMED);
                        // WHERE THE TEMPLATE IS ALLOWED TO SIT: anywhere in the cell,
                        // and that was MEASURED rather than left alone. A board's number
                        // is printed centred on its wedge -- true of every board, not of
                        // this one -- so the angular position is not really free, and
                        // holding the template to the cell's own centre plus the wire
                        // model's `kSnapDeg` was written, run on both fixtures and
                        // removed again. It changed no camera's answer and it cost
                        // confidence on every one of the twelve rings: the six number
                        // rings fell from 3.50-4.54 to 2.76-4.79 while the six numberless
                        // ones barely moved, which took the worst real ring from 71% clear
                        // of the cut to 10%. The freedom a numberless ring gains from a
                        // free search is already paid for by `doubleCentre`, and the real
                        // glyph gains more from it -- the ring is cut by fitted wires, so
                        // a cell's idea of its own centre is worth less than a glyph's.
                        double lo = 0, hi = 0;
                        cv::minMaxLoc(result, &lo, &hi);
                        best = std::max(best, hi);
                    }
                    scores[flip][j][m] = best;
                }
            }
            doubleCentre(scores[flip]);
        }

        // The forty candidates. Cell j holds sequence position (j - s) mod 20 when the
        // 20 starts at wire s, so a candidate is one whole assignment of the twenty
        // templates to the twenty cells -- and, the sequence being a cyclic permutation,
        // the twenty assignments of one half-turn partition the score matrix exactly.
        // That is why these totals sum to zero and a separation can be read off them.
        struct Candidate
        {
            int start = 0;
            int flip = 0;
            double total = 0.0;
        };
        std::vector<Candidate> candidates;
        candidates.reserve(2 * wire_model::kFold);
        for (int flip = 0; flip < 2; flip++)
        {
            for (int s = 0; s < wire_model::kFold; s++)
            {
                double total = 0.0;
                for (int j = 0; j < wire_model::kFold; j++)
                {
                    const int m = ((j - s) % wire_model::kFold + wire_model::kFold) % wire_model::kFold;
                    total += scores[flip][j][m];
                }
                candidates.push_back({s, flip, total});
            }
        }

        int winner = 0;
        for (size_t i = 1; i < candidates.size(); i++)
        {
            if (candidates[i].total > candidates[winner].total)
            {
                winner = (int)i;
            }
        }
        double runnerUp = -1e300;
        for (size_t i = 0; i < candidates.size(); i++)
        {
            if ((int)i != winner)
            {
                runnerUp = std::max(runnerUp, candidates[i].total);
            }
        }

        // WHAT THE WINNER IS MEASURED AGAINST, and why it is not its thirty-nine rivals.
        //
        // The obvious separation -- how many standard deviations the winner stands above
        // the other candidates -- was measured first and it does not work, for a reason
        // that is arithmetic rather than photographic: a winner IS the largest of forty,
        // so the largest of forty numbers with nothing in them at all stands about two
        // deviations above its own fellows. Measured on the numberless ring of six
        // cameras: 1.89, 1.98, 2.06, 2.10, 2.24, 2.74, against 2.72 to 4.08 where there
        // really are numbers. The two populations touch, and they touch because that
        // statistic has a floor it cannot go below.
        //
        // The null this is asked against instead is ALL the ways the twenty numbers could
        // be put on the twenty cells -- the whole permutation group, not the twenty
        // rotations of it. For a doubly centred matrix the total of a uniformly random
        // assignment has mean zero and variance `sum of squares / (n - 1)` exactly, so
        // this costs no sampling and no seed: it is a closed form. It says "this reading
        // is N deviations better than putting the twenty numbers on the twenty cells at
        // random", which is the question, and it has no floor -- a ring with nothing
        // printed in it scores what chance scores.
        double sumsq = 0.0;
        for (int j = 0; j < wire_model::kFold; j++)
        {
            for (int m = 0; m < wire_model::kFold; m++)
            {
                const double v = scores[candidates[winner].flip][j][m];
                sumsq += v * v;
            }
        }
        const double chance = std::sqrt(sumsq / (double)(wire_model::kFold - 1));

        out.wedge20WireIndex = candidates[winner].start;
        out.glyphsReadOutward = candidates[winner].flip == 0;
        out.total = candidates[winner].total;
        out.margin = candidates[winner].total - runnerUp;
        out.separation = chance > 0 ? candidates[winner].total / chance : 0.0;
        out.read = out.separation >= minimumSeparation();

        char said[320];
        snprintf(said, sizeof(said),
                 "%s the board's own numbers: the 20 starts at wire %d, %.2f standard deviations "
                 "clear of chance (margin %.3f over the best of the other 39 rotations, cut %.2f)",
                 out.read ? "read" : "could NOT read", out.wedge20WireIndex, out.separation,
                 out.margin, minimumSeparation());
        out.why = said;

        if (!debugDir.empty())
        {
            const int pad = 6, labelH = 16;
            cv::Mat sheet(4 * (rectH + labelH + pad) + pad, 5 * (rectW + pad) + pad, CV_8UC3,
                          cv::Scalar(30, 30, 30));
            for (int j = 0; j < wire_model::kFold; j++)
            {
                cv::Mat shown;
                cells[j].convertTo(shown, CV_8U);
                if (!out.glyphsReadOutward)
                {
                    cv::rotate(shown, shown, cv::ROTATE_180);
                }
                cv::cvtColor(shown, shown, cv::COLOR_GRAY2BGR);
                const int row = j / 5, col = j % 5;
                const int sx = pad + col * (rectW + pad);
                const int sy = pad + row * (rectH + labelH + pad);
                shown.copyTo(sheet(cv::Rect(sx, sy, rectW, rectH)));
                const int m = ((j - out.wedge20WireIndex) % wire_model::kFold + wire_model::kFold) % wire_model::kFold;
                cv::putText(sheet, "w" + std::to_string(j) + " read " + std::to_string(kSequence[m]),
                            cv::Point(sx + 2, sy + rectH + 12), cv::FONT_HERSHEY_SIMPLEX, 0.35,
                            cv::Scalar(220, 220, 220), 1);
            }
            cv::imwrite(debugDir + "/read.png", sheet);
        }

        return out;
    }
}
