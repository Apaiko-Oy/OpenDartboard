// #1498: does the board's own printed number ring say where the sequence starts, and how
// strongly -- measured on the frame the detector really calibrates on.
//
//   i1498_number_anchor <outdir> <clip1> [<clip2> ...]
//
// THE CALIBRATION HALF IS #1497'S, WHICH IS #1493'S. Seek where DEBUG_SEEK_VIDEO seeks,
// capture::readAveraged(30), geometry_calibration::calibrateMultipleCameras, and then the
// board plane built exactly as wire_processing builds it. Nothing after calibration is
// replayed: a number ring does not move, and anchoring happens once.
//
// WHAT IT PRINTS, per camera:
//   I1498CAM   what the clip-wire finder said (isStarCamera, anchored, its wedge 20),
//              what the number reader said (its wedge 20, its separation, its margin,
//              which half-turn won), and whether the two AGREE where both answered.
//   I1498CELL  one row per cell: the number this reading assigns it.
//
// It asserts nothing here. The cut, the sweep it came from and the control live in
// testers/i1498_run.sh, which runs this binary three times -- the number ring, the
// numberless ring (OD_NUMBER_ANCHOR=inner) and not at all (OD_NUMBER_ANCHOR=off).
#include <opencv2/opencv.hpp>
#include <sys/stat.h>
#include <iostream>
#include <string>
#include <vector>

#include "geometry_calibration.hpp"
#include "wire_model.hpp"
#include "wire_processing.hpp"
#include "orientation_processing.hpp"
#include "number_anchor.hpp"

namespace
{
    /** DEBUG_SEEK_VIDEO's own arithmetic (utils/capture_opencv.hpp). */
    int seekFrameFor(int cameraIndex, double fps)
    {
        const double seconds = 3.0 - (cameraIndex * 0.18);
        if (seconds <= 0 || fps <= 0)
        {
            return 0;
        }
        return (int)(fps * seconds);
    }

    /** capture::readAveraged(30), which is what calibration is handed at start-up. */
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

    std::string fmt(double v, int prec = 3)
    {
        char buf[64];
        snprintf(buf, sizeof(buf), "%.*f", prec, v);
        return buf;
    }

    const int kSequence[20] = {20, 1, 18, 4, 13, 6, 10, 15, 2, 17, 3, 19, 7, 16, 8, 11, 14, 9, 12, 5};
}

int main(int argc, char **argv)
{
    if (argc < 3)
    {
        std::cerr << "usage: i1498_number_anchor <outdir> <clip> [<clip> ...]\n";
        return 2;
    }

    const std::string outDir = argv[1];
    const int cameras = argc - 2;

    std::vector<cv::VideoCapture> caps(cameras);
    std::vector<cv::Mat> initial_frames(cameras);
    for (int i = 0; i < cameras; i++)
    {
        caps[i].open(argv[i + 2]);
        if (!caps[i].isOpened())
        {
            std::cerr << "cannot open " << argv[i + 2] << "\n";
            return 2;
        }
        const double fps = caps[i].get(cv::CAP_PROP_FPS);
        caps[i].set(cv::CAP_PROP_POS_FRAMES, seekFrameFor(i, fps));
        if (!averageOf(caps[i], 30, initial_frames[i]))
        {
            std::cerr << "no frames from " << argv[i + 2] << "\n";
            return 2;
        }
    }

    std::vector<DartboardCalibration> calibrations =
        geometry_calibration::calibrateMultipleCameras(initial_frames, false, 1280, 720);

    for (size_t i = 0; i < calibrations.size(); i++)
    {
        const DartboardCalibration &calib = calibrations[i];
        const std::vector<cv::Point2f> endpoints(calib.wires.wireEndpoints.begin(),
                                                 calib.wires.wireEndpoints.end());
        const double conicOfDoubles = wire_processing::conicOfDoublesFor(calib);

        const std::string camDir = outDir + "/cam" + std::to_string(i + 1);
        mkdir(camDir.c_str(), 0777);

        const number_anchor::Reading reading =
            number_anchor::readTheNumbers(initial_frames[i], endpoints,
                                          calib.ellipses.outerDoubleEllipse,
                                          cv::Point2f(calib.bullCenter), conicOfDoubles, camDir);

        // WHAT THE CLIP WIRES SAID, TOLD APART FROM WHAT THE NUMBERS SAID -- and this
        // distinction is the whole worth of section 5, because calibration has ALREADY
        // run the reader by the time this census sees the camera. A camera anchored
        // `READ` carries the reader's own index in `wedge20WireIndex`, so reading it back
        // and calling it an independent opinion would be the reader agreeing with itself.
        // `clipIndex` is therefore -1 on such a camera, and is an answer only where the
        // clip wires really gave one.
        const bool byNumbers =
            calib.orientation.cameraPosition == orientation_processing::CameraPosition::READ;
        const bool starAnchored =
            calib.orientation.isStarCamera && orientation_processing::wedgeCanBeRead(calib.orientation);
        const int clipIndex = byNumbers ? -1 : calib.orientation.wedge20WireIndex;
        const int starIndex = starAnchored ? clipIndex : -1;
        const bool bothAnswer = starAnchored && reading.read;

        std::cout << "I1498CAM cam=" << (i + 1)
                  << " sees=" << (calib.sees_board ? 1 : 0)
                  << " wires=" << endpoints.size()
                  << " star=" << (calib.orientation.isStarCamera ? 1 : 0)
                  << " starAnchored=" << (starAnchored ? 1 : 0)
                  << " starWedge20=" << starIndex
                  << " clipWedge20=" << clipIndex
                  << " pos=" << orientation_processing::cameraPositionToString(calib.orientation.cameraPosition)
                  << " disagree=" << (calib.orientation.numbersDisagreeWithClips ? 1 : 0)
                  << " attempted=" << (reading.attempted ? 1 : 0)
                  << " read=" << (reading.read ? 1 : 0)
                  << " readWedge20=" << reading.wedge20WireIndex
                  << " separation=" << fmt(reading.separation, 2)
                  << " margin=" << fmt(reading.margin)
                  << " total=" << fmt(reading.total)
                  << " outward=" << (reading.glyphsReadOutward ? 1 : 0)
                  << " bothAnswer=" << (bothAnswer ? 1 : 0)
                  << " agree=" << (bothAnswer ? (starIndex == reading.wedge20WireIndex ? 1 : 0) : -1)
                  << std::endl;
        std::cout << "I1498WHY cam=" << (i + 1) << " " << reading.why << std::endl;

        if (reading.attempted)
        {
            for (int j = 0; j < 20; j++)
            {
                const int m = ((j - reading.wedge20WireIndex) % 20 + 20) % 20;
                std::cout << "I1498CELL cam=" << (i + 1) << " wire=" << j
                          << " number=" << kSequence[m] << std::endl;
            }
        }
        cv::imwrite(camDir + "/averaged.png", initial_frames[i]);
    }

    std::cout << "I1498END cameras=" << calibrations.size() << std::endl;
    return 0;
}
