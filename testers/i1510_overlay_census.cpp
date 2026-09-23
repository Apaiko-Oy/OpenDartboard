// #1510: the one-board fit on real footage -- overlay, residuals, verdict, per camera.
//
//   i1510_overlay_census <clip> <camera-index> <seek-frame> <overlay-out.jpg> [frame-out.jpg]
//
// Runs the pipeline's own calibration -- geometry_calibration::calibrateSingleCamera on
// an averaged frame, the same averaging a detector run calibrates on -- from the FRAME
// the caller names, then fits the board profile through board_model and prints what the
// detector's own STEP 10 log line says, plus one machine-readable row per ring so the
// harness can diff residuals rather than prose. The seek argument exists because of
// #1514's finding: mocks/rig-20260922 STARTS with a dart parked in the board, so an
// empty-board fit on that fixture must be taken at the clean frame the issue names
// (9 s, frame 270 at 30 fps), never at frame 0. `frame-out` dumps the averaged input
// frame itself, which is how "clean" is verified by eye rather than asserted.
//
// Output rows, prefixed so they survive the pipeline's own logging:
//
//   I1510 clip=<name> cam=<n> frame=<k> <the fit's whole story>
//   I1510RING clip=<name> cam=<n> ring=<name> heldout=<0|1> rays=<n> obs_mm=<x>
//             model_mm=<x> signed_mm=<x> rms_px=<x> max_px=<x> inband=<0|1>
//
// Measurement only: it asserts nothing. The assertions live in i1510_board_check.cpp,
// against a planted truth; this file is where the fixtures are READ.

#include <opencv2/opencv.hpp>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

#include "geometry_calibration.hpp"
#include "board_model.hpp"

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
}

int main(int argc, char **argv)
{
    if (argc < 5)
    {
        std::cerr << "usage: i1510_overlay_census <clip> <camera-index> <seek-frame> "
                     "<overlay-out.jpg> [frame-out.jpg]" << std::endl;
        return 2;
    }
    const std::string clip = argv[1];
    const int cameraIndex = std::atoi(argv[2]);
    const int seekFrame = std::atoi(argv[3]);
    const std::string overlayOut = argv[4];

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
    if (argc > 5)
    {
        cv::imwrite(argv[5], frame);
    }

    DartboardCalibration calib = geometry_calibration::calibrateSingleCamera(frame, cameraIndex, false);

    const board_model::BoardProfile profile =
        board_model::profileFromSpec(perspective_processing::DartboardSpec());
    const board_model::BoardFit fit = board_model::fitBoardToCamera(
        profile, calib, wire_processing::conicOfDoublesFor(calib));

    const std::string tag = "clip=" + baseNameOf(clip) + " cam=" + std::to_string(cameraIndex + 1) +
                            " frame=" + std::to_string(seekFrame);
    std::cout << "I1510 " << tag << " " << fit.story << std::endl;

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
        printf("I1510RING clip=%s cam=%d ring=\"%s\" heldout=%d rays=%d obs_mm=%.2f model_mm=%.2f "
               "signed_mm=%+.2f rms_px=%.2f max_px=%.2f inband=%d\n",
               baseNameOf(clip).c_str(), cameraIndex + 1, name, r.heldOut ? 1 : 0, r.rays,
               r.medianObservedMm, r.modelMm, r.signedMedianMm, r.rmsPx, r.maxPx, r.inBand ? 1 : 0);
    }

    if (fit.planeBuilt)
    {
        cv::Mat overlay = frame.clone();
        board_model::drawModelOverlay(overlay, profile, calib, fit);
        cv::imwrite(overlayOut, overlay);
    }
    return 0;
}
