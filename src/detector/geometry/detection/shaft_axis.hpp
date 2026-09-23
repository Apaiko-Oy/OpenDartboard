#pragma once

#include <opencv2/opencv.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

/**
 * #1511: THE NEW DART'S SHAFT AXIS, WITH UNCERTAINTY, PER CAMERA.
 *
 * At detector v0.1.10 a camera's whole statement about where the new dart POINTS is two
 * pixels: `tip_position`, the hull point furthest from the largest fresh contour's
 * centroid, and `center_position`, that centroid. The line between them is not evidence
 * of a physical shaft axis -- the hull spans every admitted contour, so a flight, a
 * shadow lobe or the older dart's silhouette can own either end (#1492 measured the
 * published tip 82 px across empty space from the piece the centroid was measured on).
 * This header is the observation the issue asks for instead: a LINE fitted to the fresh
 * figure's own spine, carrying its support extent, its fit residual, its directional
 * uncertainty, and a refusal by name where the figure does not hold a usable line.
 *
 * THE COORDINATE SYSTEM, STATED ONCE. The axis is returned in the same image pixel
 * space of `camera_index` that every tip already lives in -- the space
 * `board_model::fitBoardToCamera` CONSUMES. Lens distortion in that space is a
 * QUANTIFIED limitation, not a corrected one: #1510 Phase 1 decided (board_model.hpp,
 * "LENS DISTORTION IS A QUANTIFIED LIMITATION") that one calibration frame per camera
 * cannot constrain a distortion model, so the board fit measures what its flat
 * projective map fails to explain -- the held-out rings' signed millimetre residuals --
 * and #1513 owns estimating more. An axis consumer maps this line through that same
 * fit (`boardPointOf`), inheriting exactly the bound the fit already states; nothing
 * here pretends to a straighter space than the one the whole pipeline scores in.
 *
 * WHAT THE FIT IS. The support is the linked fresh-diff figure -- the same
 * single-linkage group of contours the tip machinery already reads (#1494's membership
 * rule), filled, inside the physical-board mask (#1364's tip mask). Its pixels are
 * binned into one-pixel columns along a trial direction; each column's mean lateral
 * offset is one CENTRELINE sample; a line is fitted to the samples, columns a robust
 * band rejects are trimmed, and the direction is rotated onto the fitted slope until it
 * stops moving. The spine is the point of it: a flight is WIDE but on a straight dart
 * it is symmetric about the same axis as the shaft, so its columns' means lie on the
 * shaft's own line -- fitting the silhouette's area would let the flight outvote the
 * shaft (the issue's stated hazard), fitting the column means makes every column one
 * vote whatever its width. A shadow to one side, a second dart at an angle, a bent
 * silhouette all bend or split the centreline instead, which is what the residual and
 * the trim fraction are for.
 *
 * THE STRAIGHT-AXIS ASSUMPTION, DOCUMENTED AS THE ISSUE DEMANDS. A dart is modelled as
 * straight: one line explains tip, shaft and flight. A dart this model does not fit --
 * bent, or seen with its shaft occluded so the visible spine is the flight's edge --
 * leaves its violation IN `centrelineRmsPx` (a curved spine cannot fit a line) and in
 * `trimmedFraction` (a split spine loses its minority half), and past the gates it is
 * a refusal naming those figures, never a straight line asserted over a bent dart.
 *
 * WHAT THIS IS NOT. It is not a tip. A valid axis with no visible tip is a usable
 * observation (the issue: an occluded tip must not force a fabricated endpoint), and a
 * valid tip on an unusable figure keeps publishing exactly as before -- the two are
 * decided independently and nothing here reads or writes `tip_found`. The direction is
 * a LINE's, sign-normalised (dx >= 0, ties on dy) so two runs answer the same bytes;
 * WHICH end is the point is #1512's cross-camera question, not answerable from one
 * silhouette without the tip evidence that stays separate.
 *
 * Pure and inline for the reason whyNoEventIsPossible is (#1338): a tester holds every
 * verdict below with synthetic figures and the real pipeline holds it with real ones.
 * testers/i1511_axis_check.cpp does.
 */
namespace shaft_axis
{
    /**
     * The gates a figure must pass before its fitted line is called an axis, and the
     * numbers each one encodes. `gated = false` is the falsification arm -- the issue's
     * own required mutation is "removing the quality gate must make negative controls
     * fail", so the gate is removable at run time (OD_AXIS_GATE=off in
     * dart_processing.cpp) and per call here, one binary either way.
     */
    struct AxisParams
    {
        bool gated = true;

        // The support floor, in pixels of the filled figure. A RIG fact with the board
        // as denominator: the smallest dart figure measured on mocks/rig-20260918 is
        // 348 px on a ~197,000 px board (the sweep on
        // DartParams::board_change_percent_threshold), and empty-window residue there
        // measured 0 px (13-271 px on the barred mocks). 60 sits 5.8x under the
        // smallest real dart and above every measured residue, so it refuses noise
        // specks without ever refusing a dart either fixture has produced.
        int min_support_px = 60;

        // The shaft-evidence gate: support extent along the axis over the median column
        // width across it. A DART fact, not a tuned number: a dart's visible
        // shaft-plus-flight is several times longer than the figure is anywhere wide,
        // while a dart seen nearly end-on is a blob about as long as wide. MEASURED, by
        // i1511_axis_check on figures whose truth is built: every accepted synthetic
        // rod, flight and fragment figure reads 11.7-27.0, the end-on blob 1.19 and the
        // short jittery fragment 2.31, so 3.0 sits above everything that must refuse
        // with a factor of ~4 of clear air below the leanest real shaft shape.
        double min_elongation = 3.0;

        // The straightness gate on the kept centreline, px RMS about the fitted line --
        // the gate that refuses a composite figure, and the detector of a violated
        // straight-axis assumption. An OPTICAL fact at the clean end: a straight rod's
        // one-pixel-column means scatter by quantisation at 0.00-0.49 px (every clean
        // synthetic control), and a rod that survives a small trimmed lobe reads 2.26.
        // Everything one line does not explain measures FAR side: two crossing rods
        // 7.99, an off-axis end blob 7.04, a bent silhouette 7.62, a parallel older
        // dart 5.20 (all i1511_axis_check). 2.5 px: just over the worst clean survivor,
        // half the nearest composite.
        double max_centreline_rms_px = 2.5;

        // The usability gate on the direction itself, degrees at one sigma. #1512
        // intersects axes across cameras; a direction looser than this constrains
        // nothing there, and a sigma that could not be measured at all (kept <= 2
        // columns) is refused here by name. RECORDED: on every figure measured so far,
        // synthetic and fixture alike, some other gate refuses first -- the arithmetic
        // couples sigma to rms over extent^1.5, so a figure loose enough to fail 6.0
        // degrees with a clean rms is too short to pass elongation or the support
        // floor. It is kept as the backstop and as the CONTRACT the observation makes
        // to #1512, not as the gate expected to fire.
        double max_sigma_deg = 6.0;

        // A max_trim_fraction gate ("not one object") was DESIGNED AND REFUSED by
        // measurement, and the numbers are recorded so nobody re-adds it without new
        // ones: on every synthetic composite built to trip it, the contaminated first
        // fit tilts toward the distractor, the residual MAD inflates, the band widens
        // and NOTHING is trimmed -- off-axis end blob trim 0.000 rms 7.04, two
        // crossing rods trim 0.000 rms 7.99, parallel older dart trim 0.000 rms 5.20
        // -- so the RMS gate above is what actually refuses every such figure, and a
        // trim gate would be dead code wearing a meaning. `trimmedFraction` stays
        // REPORTED: where trimming works (a small lobe: 0.131 trimmed, axis saved
        // within 0.3 deg) it is the overlay's rejected-distractor census.

        // Robust-band shape, not gates: a column is trimmed when its residual exceeds
        // max(3 * 1.4826 * MAD, floor). The floor keeps one-pixel quantisation from
        // trimming half of a perfectly straight rod whose MAD is near zero.
        double trim_band_floor_px = 1.5;
        int max_iterations = 6;
    };

    /** One centreline sample: one one-pixel column along the axis. Kept for overlays
     *  and testers; a few hundred at most. */
    struct AxisColumn
    {
        double t = 0.0;     // along the axis, px, about the support centroid
        double u = 0.0;     // the column's mean lateral offset, px
        double width = 0.0; // lateral spread of the column, px
        int pixels = 0;
        bool kept = true;   // false: the robust band rejected it (a distractor's column)
    };

    /**
     * The observation. Image-space fields are in the pixels of `camera` -- the space
     * fitBoardToCamera consumes; the header's preamble owns the distortion statement.
     */
    struct AxisObservation
    {
        bool valid = false;
        std::string refusal = "no fresh figure was offered"; // named, with its numbers, when !valid

        cv::Point2f point{-1.f, -1.f};   // on the line: the kept support's centroid
        cv::Point2f direction{0.f, 0.f}; // unit; a LINE's direction, sign-normalised dx>=0
        double angleDeg = 0.0;           // atan2(dy,dx) of `direction`, degrees in [-90, 90)

        double extentPx = 0.0;           // kept support span along the axis
        double medianWidthPx = 0.0;      // median kept column width across it
        double centrelineRmsPx = 0.0;    // the fit residual: kept centreline about the line
        double sigmaDeg = -1.0;          // directional uncertainty, one sigma; -1 unmeasurable

        int supportPixels = 0;           // pixels of the whole filled figure
        int columns = 0;                 // centreline samples seen
        int trimmedColumns = 0;          // samples the robust band rejected
        double trimmedFraction = 0.0;

        // Event and frame identity, filled by the caller that owns them
        // (dart_processing::processDartState): which camera, which completed window,
        // and the capture-cycle ordinals the window averaged over.
        int camera = -1;
        long windowOrdinal = -1;
        long windowOpenedCycle = -1;
        long windowClosedCycle = -1;

        std::vector<AxisColumn> columnsDetail; // for overlays and testers
    };

    namespace detail
    {
        inline std::string fmt(const char *pattern, double v)
        {
            char buf[48];
            snprintf(buf, sizeof(buf), pattern, v);
            return buf;
        }

        inline double medianOf(std::vector<double> v)
        {
            if (v.empty())
            {
                return 0.0;
            }
            std::sort(v.begin(), v.end());
            return v[v.size() / 2];
        }
    }

    /**
     * The filled pixels of the linked figure -- the same pieces detectTipAndCenter
     * groups -- because an axis is fitted to the object's mass, not to its outline:
     * outline points weight a wide flight's long edges as heavily as the whole shaft.
     */
    inline std::vector<cv::Point> pixelsOfPieces(const std::vector<std::vector<cv::Point>> &pieces,
                                                 const cv::Size &frame)
    {
        std::vector<cv::Point> pixels;
        if (pieces.empty() || frame.width <= 0 || frame.height <= 0)
        {
            return pixels;
        }
        cv::Mat mask = cv::Mat::zeros(frame, CV_8UC1);
        cv::drawContours(mask, pieces, -1, cv::Scalar(255), cv::FILLED);
        cv::findNonZero(mask, pixels);
        return pixels;
    }

    /**
     * Fit the axis. Pure over the pixel list: no frame, no globals, no state.
     *
     * The identity fields are left for the caller; everything measured is filled here,
     * refusals included -- an ungated call (params.gated == false) still MEASURES every
     * gate figure and reports them, it just no longer refuses on them, which is what
     * lets one binary show the gates are load-bearing (the issue's required mutation).
     */
    inline AxisObservation observeShaftAxis(const std::vector<cv::Point> &supportPixels,
                                            const AxisParams &params = AxisParams())
    {
        AxisObservation out;
        out.supportPixels = (int)supportPixels.size();

        // Structural floors first: below these there is nothing to measure, gated or
        // not. Three pixels cannot disagree with any line.
        if (out.supportPixels < 3)
        {
            out.refusal = "no figure: " + std::to_string(out.supportPixels) +
                          " support pixels cannot carry a line";
            return out;
        }
        if (params.gated && out.supportPixels < params.min_support_px)
        {
            out.refusal = "support floor: " + std::to_string(out.supportPixels) +
                          " px of figure against a floor of " + std::to_string(params.min_support_px) +
                          " (the smallest rig dart measured 348 px)";
            return out;
        }

        // Initial direction: the principal axis of the whole support. Closed-form 2x2.
        double mx = 0.0, my = 0.0;
        for (const cv::Point &p : supportPixels)
        {
            mx += p.x;
            my += p.y;
        }
        mx /= out.supportPixels;
        my /= out.supportPixels;
        double sxx = 0.0, sxy = 0.0, syy = 0.0;
        for (const cv::Point &p : supportPixels)
        {
            const double dx = p.x - mx, dy = p.y - my;
            sxx += dx * dx;
            sxy += dx * dy;
            syy += dy * dy;
        }
        double theta = 0.5 * std::atan2(2.0 * sxy, sxx - syy);
        cv::Point2d dir(std::cos(theta), std::sin(theta));
        cv::Point2d origin(mx, my);

        // One measurement of the figure in the CURRENT frame (origin, dir): bin along
        // dir at one-pixel pitch, fit the column means by unweighted least squares --
        // every column is one vote whatever its width, so the flight cannot outvote
        // the shaft -- then one robust trim pass and a refit. The band is the columns'
        // own scatter, never a constant fitted to a fixture (#1322).
        struct Fit
        {
            std::vector<AxisColumn> cols;
            double a = 0.0, slope = 0.0, tbar = 0.0;
            double sumTT = 0.0; // kept columns' spread about tbar
            double s2 = 0.0;    // sum of squared kept residuals
            int kept = 0;
        };
        auto measure = [&](void) -> Fit
        {
            Fit f;
            const cv::Point2d nrm(-dir.y, dir.x);
            std::vector<double> ts(out.supportPixels), us(out.supportPixels);
            double tmin = 1e18, tmax = -1e18;
            for (int i = 0; i < out.supportPixels; i++)
            {
                const double dx = supportPixels[i].x - origin.x, dy = supportPixels[i].y - origin.y;
                ts[i] = dx * dir.x + dy * dir.y;
                us[i] = dx * nrm.x + dy * nrm.y;
                tmin = std::min(tmin, ts[i]);
                tmax = std::max(tmax, ts[i]);
            }
            const int nbins = (int)std::floor(tmax - tmin) + 1;
            if (nbins < 2)
            {
                return f; // degenerate: everything in one column; the gates refuse it
            }
            struct Acc
            {
                double su = 0, umin = 1e18, umax = -1e18;
                int n = 0;
            };
            std::vector<Acc> acc(nbins);
            for (int i = 0; i < out.supportPixels; i++)
            {
                const int b = std::min(nbins - 1, (int)std::floor(ts[i] - tmin));
                acc[b].su += us[i];
                acc[b].umin = std::min(acc[b].umin, us[i]);
                acc[b].umax = std::max(acc[b].umax, us[i]);
                acc[b].n++;
            }
            for (int b = 0; b < nbins; b++)
            {
                if (acc[b].n == 0)
                {
                    continue; // a fragmentation gap: absent, never interpolated
                }
                AxisColumn c;
                c.t = tmin + b + 0.5;
                c.u = acc[b].su / acc[b].n;
                c.width = acc[b].umax - acc[b].umin + 1.0;
                c.pixels = acc[b].n;
                f.cols.push_back(c);
            }
            for (int pass = 0; pass < 2 && (int)f.cols.size() >= 2; pass++)
            {
                double st = 0.0, su = 0.0;
                f.kept = 0;
                for (const AxisColumn &c : f.cols)
                {
                    if (!c.kept)
                    {
                        continue;
                    }
                    st += c.t;
                    su += c.u;
                    f.kept++;
                }
                if (f.kept < 2)
                {
                    break;
                }
                f.tbar = st / f.kept;
                const double ubar = su / f.kept;
                double stt = 0.0, stu = 0.0;
                for (const AxisColumn &c : f.cols)
                {
                    if (!c.kept)
                    {
                        continue;
                    }
                    stt += (c.t - f.tbar) * (c.t - f.tbar);
                    stu += (c.t - f.tbar) * (c.u - ubar);
                }
                if (!(stt > 0.0))
                {
                    f.kept = 0;
                    break;
                }
                f.slope = stu / stt;
                f.a = ubar;
                f.sumTT = stt;
                if (pass == 1)
                {
                    break;
                }
                std::vector<double> absResiduals;
                absResiduals.reserve(f.cols.size());
                for (const AxisColumn &c : f.cols)
                {
                    absResiduals.push_back(std::fabs(c.u - (f.a + f.slope * (c.t - f.tbar))));
                }
                const double mad = detail::medianOf(absResiduals);
                const double band = std::max(3.0 * 1.4826 * mad, params.trim_band_floor_px);
                for (AxisColumn &c : f.cols)
                {
                    c.kept = std::fabs(c.u - (f.a + f.slope * (c.t - f.tbar))) <= band;
                }
            }
            f.s2 = 0.0;
            for (const AxisColumn &c : f.cols)
            {
                if (!c.kept)
                {
                    continue;
                }
                const double r = c.u - (f.a + f.slope * (c.t - f.tbar));
                f.s2 += r * r;
            }
            return f;
        };

        // Iterate: measure, move the origin onto the fitted line, rotate the direction
        // onto the fitted slope, until the correction stops moving; then one final
        // measurement in the settled frame, so every reported figure -- columns
        // included -- is stated in the frame the answer is given in.
        Fit fit;
        for (int iter = 0; iter < params.max_iterations; iter++)
        {
            fit = measure();
            if (fit.kept < 2)
            {
                break;
            }
            const cv::Point2d nrm(-dir.y, dir.x);
            origin += dir * fit.tbar + nrm * fit.a; // the fitted line's point at tbar
            const double delta = std::atan(fit.slope);
            const double cd = std::cos(delta), sd = std::sin(delta);
            dir = cv::Point2d(dir.x * cd + nrm.x * sd, dir.y * cd + nrm.y * sd);
            const double norm = std::sqrt(dir.x * dir.x + dir.y * dir.y);
            dir *= 1.0 / norm;
            if (std::fabs(delta) < 1e-3)
            {
                break;
            }
        }
        fit = measure();

        // Measure everything, gated or not.
        out.columns = (int)fit.cols.size();
        double tKeptMin = 1e18, tKeptMax = -1e18;
        std::vector<double> keptWidths;
        for (const AxisColumn &c : fit.cols)
        {
            if (!c.kept)
            {
                continue;
            }
            tKeptMin = std::min(tKeptMin, c.t);
            tKeptMax = std::max(tKeptMax, c.t);
            keptWidths.push_back(c.width);
        }
        out.trimmedColumns = out.columns - fit.kept;
        out.trimmedFraction = out.columns > 0 ? (double)out.trimmedColumns / out.columns : 1.0;
        out.columnsDetail = fit.cols;
        if (fit.kept < 2)
        {
            out.refusal = "no line: only " + std::to_string(fit.kept) +
                          " centreline column(s) survive over " + std::to_string(out.columns) +
                          " seen, and one column is any direction at all";
            return out;
        }
        out.extentPx = tKeptMax - tKeptMin;
        out.medianWidthPx = detail::medianOf(keptWidths);
        out.centrelineRmsPx = std::sqrt(fit.s2 / fit.kept);
        // Directional uncertainty: the slope's standard error, columns as the
        // measurements. var(slope) = s^2 / sum((t - tbar)^2) with s^2 the residual
        // variance at kept-2 degrees of freedom -- ordinary line-fit arithmetic, and
        // the columns rather than the pixels are the honest N: a column's pixels share
        // one lateral truth, so counting them would claim sqrt(width) knowledge nobody
        // measured.
        if (fit.kept > 2 && fit.sumTT > 0.0)
        {
            const double sFit2 = fit.s2 / (fit.kept - 2);
            out.sigmaDeg = std::sqrt(sFit2 / fit.sumTT) * 180.0 / CV_PI;
        }
        // The settled frame's final small correction, folded into the answer.
        {
            const cv::Point2d nrm(-dir.y, dir.x);
            origin += dir * fit.tbar + nrm * fit.a;
            const double delta = std::atan(fit.slope);
            const double cd = std::cos(delta), sd = std::sin(delta);
            dir = cv::Point2d(dir.x * cd + nrm.x * sd, dir.y * cd + nrm.y * sd);
            const double norm = std::sqrt(dir.x * dir.x + dir.y * dir.y);
            dir *= 1.0 / norm;
        }
        mx = origin.x;
        my = origin.y;

        // The line, sign-normalised: a line has no arrow, and two runs must answer the
        // same bytes about one figure.
        if (dir.x < 0.0 || (dir.x == 0.0 && dir.y < 0.0))
        {
            dir = -dir;
        }
        out.direction = cv::Point2f((float)dir.x, (float)dir.y);
        out.point = cv::Point2f((float)mx, (float)my);
        double angle = std::atan2(dir.y, dir.x) * 180.0 / CV_PI;
        if (angle >= 90.0)
        {
            angle -= 180.0;
        }
        if (angle < -90.0)
        {
            angle += 180.0;
        }
        out.angleDeg = angle;

        // The gates, each refusing with its numbers against its threshold (#1321).
        std::string refusals;
        auto refuse = [&refusals](const std::string &why)
        { refusals += (refusals.empty() ? "" : "; ") + why; };
        const double elongation = out.medianWidthPx > 0.0 ? out.extentPx / out.medianWidthPx : 0.0;
        if (params.gated)
        {
            if (elongation < params.min_elongation)
            {
                refuse("not a shaft: extent " + detail::fmt("%.1f", out.extentPx) +
                       " px over median width " + detail::fmt("%.1f", out.medianWidthPx) +
                       " px is " + detail::fmt("%.2f", elongation) + ", under the " +
                       detail::fmt("%.1f", params.min_elongation) +
                       " elongation gate (a near-end-on dart or a blob, not a line of evidence)");
            }
            if (out.centrelineRmsPx > params.max_centreline_rms_px)
            {
                refuse("not straight: the kept centreline scatters " +
                       detail::fmt("%.2f", out.centrelineRmsPx) + " px RMS about the line against a " +
                       detail::fmt("%.1f", params.max_centreline_rms_px) +
                       " px gate -- a bent silhouette, a shadow lobe, or two objects one line does not explain");
            }
            if (out.sigmaDeg < 0.0 || out.sigmaDeg > params.max_sigma_deg)
            {
                refuse("direction unusable: sigma " +
                       (out.sigmaDeg < 0.0 ? std::string("unmeasurable")
                                           : detail::fmt("%.2f", out.sigmaDeg) + " deg") +
                       " against a " + detail::fmt("%.1f", params.max_sigma_deg) +
                       " deg gate -- too short or too scattered to constrain #1512's intersection");
            }
        }
        if (!refusals.empty())
        {
            out.refusal = refusals;
            return out;
        }
        out.valid = true;
        out.refusal.clear();
        return out;
    }

    /**
     * The observation drawn on this camera's frame: kept support pieces, the centreline
     * columns (kept green, trimmed red -- the trimmed ones ARE the rejected
     * distractors), the fitted axis through the support extent, and the one-sigma
     * uncertainty fan at both ends. The acceptance's overlay, produced wherever the
     * probe asks for it.
     */
    inline void drawAxisOverlay(cv::Mat &canvas, const AxisObservation &axis,
                                const std::vector<std::vector<cv::Point>> &pieces)
    {
        if (canvas.empty())
        {
            return;
        }
        cv::drawContours(canvas, pieces, -1, cv::Scalar(80, 80, 80), cv::FILLED);
        cv::drawContours(canvas, pieces, -1, cv::Scalar(255, 200, 0), 1);

        const cv::Point2d d(axis.direction.x, axis.direction.y);
        const cv::Point2d n(-d.y, d.x);
        const cv::Point2d p(axis.point.x, axis.point.y);
        for (const AxisColumn &c : axis.columnsDetail)
        {
            // Column samples are stored about the fitted origin: u is residual space.
            const cv::Point2d q = p + d * c.t + n * (c.u);
            cv::circle(canvas, cv::Point((int)std::lround(q.x), (int)std::lround(q.y)), 1,
                       c.kept ? cv::Scalar(0, 220, 0) : cv::Scalar(0, 0, 255), -1);
        }
        if (axis.extentPx > 0.0)
        {
            const double half = axis.extentPx / 2.0;
            const cv::Point2d e0 = p - d * half, e1 = p + d * half;
            cv::line(canvas, cv::Point((int)e0.x, (int)e0.y), cv::Point((int)e1.x, (int)e1.y),
                     axis.valid ? cv::Scalar(0, 255, 255) : cv::Scalar(128, 128, 255), 2);
            if (axis.sigmaDeg > 0.0)
            {
                const double s = axis.sigmaDeg * CV_PI / 180.0;
                for (const double sign : {-1.0, 1.0})
                {
                    const cv::Point2d fan(d.x * std::cos(sign * s) - d.y * std::sin(sign * s),
                                          d.x * std::sin(sign * s) + d.y * std::cos(sign * s));
                    cv::line(canvas, cv::Point((int)e0.x, (int)e0.y),
                             cv::Point((int)(e0.x - fan.x * 40), (int)(e0.y - fan.y * 40)),
                             cv::Scalar(255, 0, 255), 1);
                    cv::line(canvas, cv::Point((int)e1.x, (int)e1.y),
                             cv::Point((int)(e1.x + fan.x * 40), (int)(e1.y + fan.y * 40)),
                             cv::Scalar(255, 0, 255), 1);
                }
            }
        }
        const std::string caption =
            axis.valid ? "AXIS " + detail::fmt("%.1f", axis.angleDeg) + " deg +/- " +
                             detail::fmt("%.2f", axis.sigmaDeg) + ", extent " +
                             detail::fmt("%.0f", axis.extentPx) + " px, rms " +
                             detail::fmt("%.2f", axis.centrelineRmsPx)
                       : "NO AXIS: " + axis.refusal;
        cv::putText(canvas, caption.substr(0, 110), cv::Point(10, 22), cv::FONT_HERSHEY_SIMPLEX, 0.5,
                    axis.valid ? cv::Scalar(0, 255, 0) : cv::Scalar(0, 0, 255), 1);
    }

    /** The census line i1511's harness parses: one line, every figure, refusal last so
     *  a parser may take the rest of the line. */
    inline std::string censusLine(const AxisObservation &axis, double gapToTipPx)
    {
        char head[320];
        snprintf(head, sizeof(head),
                 "I1511AXIS window=%ld opened=%ld closed=%ld cam=%d valid=%d p=(%.1f,%.1f) "
                 "d=(%.4f,%.4f) angle=%.2f extent=%.1f width=%.1f rms=%.2f sigma=%.3f px=%d "
                 "cols=%d trimmed=%d frac=%.3f tipGap=%.1f refusal=",
                 axis.windowOrdinal, axis.windowOpenedCycle, axis.windowClosedCycle,
                 axis.camera + 1, axis.valid ? 1 : 0, axis.point.x, axis.point.y,
                 axis.direction.x, axis.direction.y, axis.angleDeg, axis.extentPx,
                 axis.medianWidthPx, axis.centrelineRmsPx, axis.sigmaDeg, axis.supportPixels,
                 axis.columns, axis.trimmedColumns, axis.trimmedFraction, gapToTipPx);
        return std::string(head) + (axis.valid ? "-" : axis.refusal);
    }
}
