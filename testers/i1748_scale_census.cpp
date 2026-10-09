// #1748: a doubles span the ring identity calls TREBLE, planted on a rig frame.
//
//   i1748_scale_census <clip> <camera-index> <seek-frame> <plant: none|arc> [frame-out.jpg]
//
// No recording shows the fault: live on 2026-10-08 camera 1's colour reached past
// 1.26 spans on a tenth of its rays (the room, #1731's red), `ring_identity::identify`
// called the span the TREBLE ring, `wire_processing::conicOfDoublesFor` then read the
// traced conic -- the same ring, at 1.0 span -- as the treble ring too and built the
// plane at 1.589 of it, and `board_model::fitBoardToCamera` accepted a model whose own
// fitted ring sat 65 mm from the wire it was taken for. This census makes that frame
// out of the fixture that has the same camera: mocks/rig-20260929/cam_2.mp4 is the
// physical camera the live log numbers 1 (bull (646,232), board 332 px on both), and
// its doubles ring survives the colour mask, so its span IS the doubles ring.
//
// `arc` paints an annulus of board red at 1.30..1.45 spans over a 60 degree arc, on the
// averaged frame, before anything looks at it. 60 degrees is 120 of the identity's 720
// rays, which is more than the 72 the 90th percentile needs, and 1.30..1.45 sits
// inside the treble band (1.26..2.00) the way the live reading did. The arc is small
// beside the board -- a twentieth of its area -- so STEP 1's largest outermost contour
// is still the board and the span is still the doubles ring; what changes is what lies
// OUTSIDE it, which is the one thing the identity reads. `none` is the control: the
// same frame, unpainted.
//
// Then the pipeline's own calibration, the pipeline's own identity of the traced conic
// and the pipeline's own fit, and the verdicts READ from them rather than restated:
//
//   I1748PLANT clip=<name> cam=<n> span=<px> centre=(x,y) arc=<deg>..<deg> radii=<spans>..<spans>
//   I1748 clip=<name> cam=<n> plant=<none|arc> sees=<0|1> identity=<doubles|trebles|unknown>
//         reach=<spans> span=<px> conic=<px> conicOfDoubles=<f> planeBuilt=<0|1>
//         geometryAccepted=<0|1> worstFittedMm=<f> worstHeldOutMm=<f>
//   I1748STORY clip=<name> cam=<n> plant=<..> <the fit's whole story>
//   I1748RING clip=<name> cam=<n> plant=<..> ring="<name>" heldout=<0|1> obs_mm=<x>
//             model_mm=<x> signed_mm=<x> inband=<0|1>
//
// The pipeline's own log lines (the ring identity, the wire model's WARN, the fit's
// INFO) are on stdout between these rows, and the harness reads the WARN from there.
//
// Measurement only: the assertions are in i1748_inside.sh.

#include <opencv2/opencv.hpp>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

#include "geometry_calibration.hpp"
#include "board_model.hpp"
#include "color_processing.hpp"
#include "ring_identity.hpp"
#include "wire_processing.hpp"

namespace
{
    bool averageOf(cv::VideoCapture &cap, int numFrames, cv::Mat &out)
    {
        cv::Mat sum;
        int consumed = 0;
        for (int i = 0; i < numFrames; i++)
        {
            cv::Mat f;
            if (!cap.read(f) || f.empty())
            {
                break;
            }
            cv::Mat ff;
            f.convertTo(ff, CV_32F);
            if (sum.empty())
            {
                sum = ff;
            }
            else
            {
                sum += ff;
            }
            consumed++;
        }
        if (consumed == 0)
        {
            return false;
        }
        cv::Mat mean = sum / consumed;
        mean.convertTo(out, CV_8U);
        return true;
    }

    std::string baseNameOf(const std::string &path)
    {
        const size_t slash = path.find_last_of("/\\");
        return slash == std::string::npos ? path : path.substr(slash + 1);
    }

    /**
     * STEP 1 as the pipeline measures it (i1423_ring_census's replica): the colour
     * stage on the full frame, blurred, thresholded, and the smallest circle around
     * the largest outermost contour. Only the plant reads this; the calibration below
     * measures its own.
     */
    bool spanOf(const cv::Mat &frame, int camIdx, cv::Point2f &centre, float &span)
    {
        cv::Mat colours = color_processing::processColors(frame, camIdx, false, color_processing::ColorParams());
        cv::Mat blurred, gray, binary;
        cv::GaussianBlur(colours, blurred, cv::Size(7, 7), 2.0);
        cv::cvtColor(blurred, gray, cv::COLOR_BGR2GRAY);
        cv::threshold(gray, binary, 1, 255, cv::THRESH_BINARY);
        std::vector<std::vector<cv::Point>> contours;
        std::vector<cv::Vec4i> hier;
        cv::findContours(binary, contours, hier, cv::RETR_TREE, cv::CHAIN_APPROX_SIMPLE);
        int best = -1;
        double bestArea = 0;
        for (size_t i = 0; i < contours.size(); i++)
        {
            if (hier[i][3] != -1)
            {
                continue;
            }
            const double a = cv::contourArea(contours[i]);
            if (a > bestArea)
            {
                bestArea = a;
                best = (int)i;
            }
        }
        if (best < 0)
        {
            return false;
        }
        cv::minEnclosingCircle(contours[best], centre, span);
        return span > 0;
    }

    const double kArcInner = 1.30;  // spans; the treble band starts at 1.2605
    const double kArcOuter = 1.45;  // spans; well under the band's top, 2.003
    const double kArcDeg = 60.0;    // 120 of 720 rays, where the 90th percentile needs 72
    const int kEdgeMargin = 12;     // px the arc keeps from the frame edge (ADR-0079 s.2 asks 1)

    /** Paint the arc where it fits in the frame; false when no 60 degree arc does. */
    bool paintArc(cv::Mat &frame, const cv::Point2f &centre, float span, double &a0, double &a1)
    {
        const double rOut = span * kArcOuter;
        for (int start = 0; start < 360; start += 15)
        {
            bool fits = true;
            for (int d = 0; d <= (int)kArcDeg && fits; d += 2)
            {
                const double th = (start + d) * CV_PI / 180.0;
                const double x = centre.x + rOut * std::cos(th), y = centre.y + rOut * std::sin(th);
                fits = x >= kEdgeMargin && y >= kEdgeMargin &&
                       x < frame.cols - kEdgeMargin && y < frame.rows - kEdgeMargin;
            }
            if (!fits)
            {
                continue;
            }
            a0 = start;
            a1 = start + kArcDeg;
            const double rMid = span * 0.5 * (kArcInner + kArcOuter);
            const int thickness = std::max(1, (int)std::lround(span * (kArcOuter - kArcInner)));
            // Board red: hue 0, saturation and value well inside the colour stage's
            // 60..255 windows (ColorParams), the way the paint on the board is.
            cv::ellipse(frame, centre, cv::Size((int)std::lround(rMid), (int)std::lround(rMid)), 0.0, a0, a1,
                        cv::Scalar(30, 30, 200), thickness, cv::LINE_8);
            return true;
        }
        return false;
    }

    const char *identityName(int ring)
    {
        switch (static_cast<ring_identity::Ring>(ring))
        {
        case ring_identity::Ring::Doubles:
            return "doubles";
        case ring_identity::Ring::Trebles:
            return "trebles";
        default:
            return "unknown";
        }
    }
}

int main(int argc, char **argv)
{
    if (argc < 5)
    {
        std::cerr << "usage: i1748_scale_census <clip> <camera-index> <seek-frame> <none|arc> [frame-out.jpg]"
                  << std::endl;
        return 2;
    }
    const std::string clip = argv[1];
    const int cameraIndex = std::atoi(argv[2]);
    const int seekFrame = std::atoi(argv[3]);
    const std::string plant = argv[4];
    if (plant != "none" && plant != "arc")
    {
        std::cerr << "plant must be none or arc" << std::endl;
        return 2;
    }

    cv::VideoCapture cap(clip);
    if (!cap.isOpened())
    {
        std::cerr << "cannot open " << clip << std::endl;
        return 2;
    }
    cap.set(cv::CAP_PROP_POS_FRAMES, seekFrame);
    cv::Mat frame;
    if (!averageOf(cap, 30, frame))
    {
        std::cerr << "no frames at index " << seekFrame << " of " << clip << std::endl;
        return 2;
    }

    const std::string tag = "clip=" + baseNameOf(clip) + " cam=" + std::to_string(cameraIndex + 1);

    if (plant == "arc")
    {
        cv::Point2f centre;
        float span = 0;
        if (!spanOf(frame, cameraIndex, centre, span))
        {
            std::cout << "I1748PLANT " << tag << " NOSPAN" << std::endl;
            return 3;
        }
        double a0 = 0, a1 = 0;
        if (!paintArc(frame, centre, span, a0, a1))
        {
            std::cout << "I1748PLANT " << tag << " NOROOM span=" << cvRound(span) << std::endl;
            return 3;
        }
        printf("I1748PLANT %s span=%d centre=(%d,%d) arc=%.0f..%.0f radii=%.2f..%.2f\n", tag.c_str(),
               cvRound(span), cvRound(centre.x), cvRound(centre.y), a0, a1, kArcInner, kArcOuter);
    }
    if (argc > 5)
    {
        cv::imwrite(argv[5], frame);
    }

    DartboardCalibration calib = geometry_calibration::calibrateSingleCamera(frame, cameraIndex, false);

    const double conicOfDoubles = calib.sees_board ? wire_processing::conicOfDoublesFor(calib) : 0.0;
    const board_model::BoardProfile profile =
        board_model::profileFromSpec(perspective_processing::DartboardSpec());
    const board_model::BoardFit fit = board_model::fitBoardToCamera(profile, calib, conicOfDoubles);

    const double conicPx = ellipse_processing::ringReach(calib.ellipses.outerDoubleEllipse);
    double worstFitted = 0.0, worstHeldOut = 0.0;
    for (int i = 0; i <= ellipse_processing::kRingCount; i++)
    {
        const board_model::RingResidual &r = fit.rings[i];
        if (!r.observed)
        {
            continue;
        }
        double &worst = r.heldOut ? worstHeldOut : worstFitted;
        worst = std::max(worst, std::fabs(r.signedMedianMm));
    }

    printf("I1748 %s plant=%s sees=%d identity=%s reach=%.3f span=%.1f conic=%.1f conicOfDoubles=%.4f "
           "planeBuilt=%d geometryAccepted=%d worstFittedMm=%.1f worstHeldOutMm=%.1f\n",
           tag.c_str(), plant.c_str(), calib.sees_board ? 1 : 0, identityName(calib.look.ring_measured),
           calib.look.ring_reach_of_span, calib.look.board_span_px, conicPx, conicOfDoubles,
           fit.planeBuilt ? 1 : 0, fit.geometryAccepted ? 1 : 0, worstFitted, worstHeldOut);
    std::cout << "I1748STORY " << tag << " plant=" << plant << " " << fit.story << std::endl;

    static const int ringOf[ellipse_processing::kRingCount + 1] = {
        ellipse_processing::kInnerBull, ellipse_processing::kOuterBull,
        ellipse_processing::kInnerTriple, ellipse_processing::kOuterTriple,
        ellipse_processing::kInnerDouble, ellipse_processing::kRingCount};
    for (int i = 0; i <= ellipse_processing::kRingCount; i++)
    {
        const board_model::RingResidual &r = fit.rings[i];
        if (!r.observed)
        {
            continue;
        }
        const char *name = ringOf[i] == ellipse_processing::kRingCount
                               ? "outer-double"
                               : ellipse_processing::ringName(ringOf[i]);
        printf("I1748RING %s plant=%s ring=\"%s\" heldout=%d obs_mm=%.2f model_mm=%.2f signed_mm=%+.2f inband=%d\n",
               tag.c_str(), plant.c_str(), name, r.heldOut ? 1 : 0, r.medianObservedMm, r.modelMm,
               r.signedMedianMm, r.inBand ? 1 : 0);
    }
    return 0;
}
