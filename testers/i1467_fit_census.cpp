// #1467: what the twenty-fold model says on one frame of one clip, measured rather than
// assumed.
//
//   i1467_fit_census <clip> <cam-index> [looks] [spacing]
//
// It calls the REAL calibration -- geometry_calibration::calibrateSingleCamera, the same
// function the detector calls, not a cheaper imitation of it (#1437's method) -- and then
// rebuilds the colour mask that stage read so it can ask `wire_processing::wireCandidates`
// for the same candidates the wire stage saw. Every verdict below is READ from the modules
// rather than restated here, so a tree whose rule has moved prints the moved rule.
//
// Rows, all prefixed so they survive the calibration's own logging:
//
//   I1467    one per look: the fit, the ring it produced and whether the camera calibrated
//   I1467RES one per look: every candidate's board-space residual, in degrees
//   I1467FOLD one per look: the coherence at seven periodicities -- the control that says
//            the fit did not manufacture its own structure
//   I1467BULL one per look per displacement: the bull moved d px in eight directions,
//            worst case, with the coherence and the worst boundary error it produced.
//            This is the loud-failure measurement, and it is the reason a refusal can
//            exist at all.
#include <opencv2/opencv.hpp>
#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <cmath>

#include "geometry_calibration.hpp"
#include "wire_processing.hpp"
#include "wire_model.hpp"
#include "color_processing.hpp"
#include "bull_processing.hpp"
#include "roi_processing.hpp"

namespace
{
    inline constexpr double kPi = 3.14159265358979323846;

    int seekFrameFor(int cameraIndex, double fps)
    {
        const double seconds = 3.0 - (cameraIndex * 0.18);
        if (seconds <= 0 || fps <= 0) return 0;
        return (int)(fps * seconds);
    }

    bool averageOf(cv::VideoCapture &cap, int numFrames, cv::Mat &out)
    {
        cv::Mat sum; int consumed = 0;
        for (int i = 0; i < numFrames; i++)
        {
            cv::Mat f;
            if (!cap.read(f) || f.empty()) break;
            cv::Mat ff; f.convertTo(ff, CV_32F);
            if (sum.empty()) sum = ff; else sum += ff;
            consumed++;
        }
        if (consumed == 0) return false;
        cv::Mat mean = sum / consumed;
        mean.convertTo(out, CV_8U);
        return true;
    }

    // STEP 2 and STEP 2.5 of the pipeline, in the pipeline's own calls, so the census
    // hands the wire stage the mask the wire stage was handed.
    bool colourMaskFor(const cv::Mat &frame, int camIdx, cv::Mat &out)
    {
        const cv::Point frameCentre(frame.cols / 2, frame.rows / 2);
        color_processing::ColorParams colorParams;
        cv::Mat full = color_processing::processColors(frame, camIdx, false, colorParams);
        const bull_processing::BoardSighting board =
            bull_processing::measureBoard(full, frameCentre, bull_processing::BullParams());
        if (!board.found) return false;
        roi_processing::ROIParams roiParams;
        cv::Mat roi = roi_processing::processROI(frame, board.center, board.radius, false, camIdx, roiParams);
        out = color_processing::processColors(roi, camIdx, false, colorParams);
        return !out.empty();
    }

    double imageAngleOf(const cv::Point2f &p, const cv::Point2f &c)
    {
        return std::atan2(p.y - c.y, p.x - c.x);
    }

    double angleGapDeg(double a, double b)
    {
        double d = a - b;
        while (d > kPi) d -= 2 * kPi;
        while (d < -kPi) d += 2 * kPi;
        return std::fabs(d) * 180.0 / kPi;
    }
}

int main(int argc, char **argv)
{
    // The tree's own refusal threshold, printed by the tree rather than restated by the
    // harness: a section that hard-coded it would go on passing after somebody moved it.
    std::cout << "I1467MIN min=" << wire_model::minimumCoherence()
              << " cut=" << wire_model::kResidualCutDeg
              << " snap=" << wire_model::kSnapDeg << "\n";

    if (argc < 3) { std::cerr << "usage: i1467_fit_census <clip> <cam-index> [looks] [spacing]\n"; return 2; }
    const std::string clip = argv[1];
    const int camIdx = std::atoi(argv[2]);
    const int looks = (argc > 3) ? std::atoi(argv[3]) : 1;
    const int spacing = (argc > 4) ? std::atoi(argv[4]) : 30;

    cv::VideoCapture cap(clip);
    if (!cap.isOpened()) { std::cerr << "cannot open " << clip << "\n"; return 2; }
    const double fps = cap.get(cv::CAP_PROP_FPS);
    cap.set(cv::CAP_PROP_POS_FRAMES, seekFrameFor(camIdx, fps));

    for (int look = 0; look < looks; look++)
    {
        cv::Mat frame;
        if (!averageOf(cap, 30, frame)) break;

        const std::string where = " clip=" + clip + " cam=" + std::to_string(camIdx) +
                                  " look=" + std::to_string(look);

        DartboardCalibration calib = geometry_calibration::calibrateSingleCamera(frame, camIdx, false);

        std::cout << "I1467" << where
                  << " doubles=" << (calib.ellipses.hasValidDoubles ? 1 : 0)
                  << " wires=" << calib.wires.wiresDetected
                  << " ok=" << (calib.wires.isValid ? 1 : 0)
                  << " cand=" << calib.wires.fit_candidates
                  << " asked=" << (calib.wires.fit_asked ? 1 : 0)
                  << " R=" << calib.wires.fit_coherence
                  << " inlier=" << calib.wires.fit_inlier_fraction
                  << " snapped=" << calib.wires.fit_snapped;

        if (!calib.ellipses.hasValidDoubles)
        {
            std::cout << " tilt=-1 rms=-1 gaps=-1\n";
            for (int s = 1; s < spacing; s++) { cv::Mat junk; if (!cap.read(junk)) break; }
            continue;
        }

        // The same plane and the same candidates the stage used, from the stage's own
        // functions. A census that re-derived either would be measuring its own copy.
        cv::Mat colourMask;
        std::vector<cv::Point2f> candidates;
        if (colourMaskFor(frame, camIdx, colourMask))
        {
            candidates = wire_processing::wireCandidates(frame, colourMask, calib);
        }

        const cv::Point2f bull((float)calib.bullCenter.x, (float)calib.bullCenter.y);
        const wire_model::Plane plane =
            wire_model::planeOf(calib.ellipses.outerDoubleEllipse, bull, wire_processing::conicOfDoublesFor(calib));
        const wire_model::Fit fit = wire_model::fitTwentyFold(plane, candidates);

        // The smallest gap between two of the twenty boundaries the camera really shipped.
        // A ring of twenty with two boundaries on top of each other is not a board's ring,
        // so "twenty distinct" is asserted rather than assumed.
        double smallestGap = 999.0;
        const int kept = (int)calib.wires.wireEndpoints.size();
        for (int i = 0; i < kept; i++)
        {
            for (int j = i + 1; j < kept; j++)
            {
                smallestGap = std::min(smallestGap,
                                       angleGapDeg(imageAngleOf(calib.wires.wireEndpoints[i], bull),
                                                   imageAngleOf(calib.wires.wireEndpoints[j], bull)));
            }
        }

        std::cout << " tilt=" << plane.tilt
                  << " rms=" << fit.rmsResidualDeg
                  << " gaps=" << (kept > 1 ? smallestGap : -1.0)
                  << " conic=" << wire_processing::conicOfDoublesFor(calib)
                  << "\n";

        // Every candidate's residual, for the pooled distribution.
        const std::vector<double> res = wire_model::residualsOf(plane, candidates, fit.offset);
        std::cout << "I1467RES" << where << " n=" << res.size() << " res=";
        for (size_t i = 0; i < res.size(); i++) { std::cout << (i ? "," : "") << res[i]; }
        std::cout << "\n";

        // THE CONTROL. If the fit had invented the periodicity, every fold would score
        // alike.
        const int folds[] = {16, 18, 19, 20, 21, 22, 24};
        std::cout << "I1467FOLD" << where;
        for (int f : folds) { std::cout << " n" << f << "=" << wire_model::coherenceAtFold(plane, candidates, f); }
        std::cout << "\n";

        // THE LOUD FAILURE. The bull is displaced in eight directions and the worst case
        // is reported: the coherence the fit then has, and how far the ring it generates
        // moves from the one the true bull generated. The refusal downstream is a
        // comparison on the first of those two numbers, so this is the measurement that
        // says whether it can see the second.
        const wire_model::Ring trueRing = wire_model::ringFrom(plane, fit, candidates, bull);
        const int displacements[] = {0, 1, 2, 3, 5, 8, 12, 20};
        for (int d : displacements)
        {
            double worstR = 1.0, worstErr = 0.0;
            for (int k = 0; k < 8; k++)
            {
                const double a = 2.0 * kPi * k / 8.0;
                const cv::Point2f moved(bull.x + (float)(d * std::cos(a)), bull.y + (float)(d * std::sin(a)));
                const wire_model::Plane p2 =
                    wire_model::planeOf(calib.ellipses.outerDoubleEllipse, moved, wire_processing::conicOfDoublesFor(calib));
                const wire_model::Fit f2 = wire_model::fitTwentyFold(p2, candidates);
                worstR = std::min(worstR, f2.coherence);
                if (!p2.built || !f2.built) { worstErr = 999.0; continue; }
                const wire_model::Ring r2 = wire_model::ringFrom(p2, f2, candidates, moved);
                if (r2.endpoints.size() != trueRing.endpoints.size()) { worstErr = 999.0; continue; }
                // Each boundary against the nearest of the true ring's, in image angle
                // about the TRUE bull: the comparison a scorer would care about.
                for (const cv::Point2f &q : r2.endpoints)
                {
                    double best = 999.0;
                    for (const cv::Point2f &t : trueRing.endpoints)
                    {
                        best = std::min(best, angleGapDeg(imageAngleOf(q, bull), imageAngleOf(t, bull)));
                    }
                    worstErr = std::max(worstErr, best);
                }
            }
            std::cout << "I1467BULL" << where << " d=" << d << " R=" << worstR << " maxerr=" << worstErr << "\n";
        }

        for (int s = 1; s < spacing; s++) { cv::Mat junk; if (!cap.read(junk)) break; }
    }
    return 0;
}
