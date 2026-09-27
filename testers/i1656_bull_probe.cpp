// #1656: one look of one clip, calibrated with debug images on, so the bull stage's own
// pictures (the red/green mask it chose from, and the candidate it chose) can be looked at.
//
//   i1656_bull_probe <clip> <camera-index> <open|dev> <look: avg|k> [spacing] [averaged-frames]
//
// Frames are chosen exactly as testers/i1654_comb_census.cpp chooses them: the 30-frame
// average at the window's start, then look k is k*spacing frames after it. Writes the
// calibration's debug_frames/ under the working directory, plus look.png (the frame) and
// bull_crop.png (a 4x crop around the chosen bull). Prints one I1656PROBE line.
#include <opencv2/opencv.hpp>
#include <cstdio>
#include <iostream>
#include <string>

#include "geometry_calibration.hpp"

namespace
{
    int seekFrameFor(int cameraIndex, double fps)
    {
        const double seconds = 3.0 - (cameraIndex * 0.18);
        if (seconds <= 0 || fps <= 0)
            return 0;
        return (int)(fps * seconds);
    }
}

int main(int argc, char **argv)
{
    if (argc < 5)
    {
        std::cerr << "usage: i1656_bull_probe <clip> <camera-index> <open|dev> <avg|k> [spacing] [averaged]" << std::endl;
        return 2;
    }
    const std::string clip = argv[1];
    const int cam = atoi(argv[2]);
    const bool dev = std::string(argv[3]) == "dev";
    const std::string look = argv[4];
    const int spacing = argc > 5 ? atoi(argv[5]) : 5;
    const int averaged = argc > 6 ? atoi(argv[6]) : 30;

    cv::VideoCapture cap(clip);
    if (!cap.isOpened())
        return 2;
    const int seek = dev ? seekFrameFor(cam, cap.get(cv::CAP_PROP_FPS)) : 0;
    if (seek > 0)
        cap.set(cv::CAP_PROP_POS_FRAMES, seek);
    cv::Mat sum, f, frame;
    int counted = 0;
    for (int i = 0; i < averaged; i++)
    {
        if (!cap.read(f) || f.empty())
            continue;
        cv::Mat fl;
        f.convertTo(fl, CV_32F);
        if (counted == 0)
            sum = fl;
        else
            sum += fl;
        counted++;
    }
    if (counted == 0)
        return 2;
    if (look == "avg")
    {
        cv::Mat a = sum / (float)counted;
        a.convertTo(frame, CV_8U);
    }
    else
    {
        const int k = atoi(look.c_str());
        for (int s = 0; s < k * spacing; s++)
            if (!cap.read(frame) || frame.empty())
                return 2;
    }
    cv::imwrite("look.png", frame);
    const DartboardCalibration c = geometry_calibration::calibrateSingleCamera(frame, cam, true);
    printf("I1656PROBE clip=%s cam=%d win=%s look=%s sees=%d bull=%d,%d sub=%.2f,%.2f\n", clip.c_str(), cam + 1,
           dev ? "dev" : "open", look.c_str(), c.sees_board ? 1 : 0, c.bullCenter.x, c.bullCenter.y,
           c.bullSubpixel.x, c.bullSubpixel.y);
    for (int rad = 20; rad <= 70; rad += 10)
    {
        // The refinement's own view at several radii: the window's chroma, the cut on it,
        // and the pieces with the edge points kept, 4x.
        cv::Mat dbg, big;
        const cv::Point2f at = bull_processing::refineBullCentre(frame, c.bullCenter, rad, &dbg);
        printf("I1656PROBE refine at radius %d px: %.2f,%.2f\n", rad, at.x, at.y);
        if (!dbg.empty())
        {
            cv::resize(dbg, big, cv::Size(), 4, 4, cv::INTER_NEAREST);
            cv::imwrite("refine_" + std::to_string(rad) + ".png", big);
        }
    }
    const int h = 60;
    cv::Rect r(c.bullCenter.x - h, c.bullCenter.y - h, 2 * h, 2 * h);
    r &= cv::Rect(0, 0, frame.cols, frame.rows);
    if (r.area() > 0)
    {
        cv::Mat crop;
        cv::resize(frame(r), crop, cv::Size(), 4, 4, cv::INTER_NEAREST);
        cv::drawMarker(crop, cv::Point((c.bullCenter.x - r.x) * 4 + 2, (c.bullCenter.y - r.y) * 4 + 2),
                       cv::Scalar(255, 0, 255), cv::MARKER_CROSS, 20, 1);
        cv::drawMarker(crop, cv::Point((int)((c.bullSubpixel.x - r.x + 0.5) * 4), (int)((c.bullSubpixel.y - r.y + 0.5) * 4)),
                       cv::Scalar(0, 255, 255), cv::MARKER_TILTED_CROSS, 20, 1);
        cv::imwrite("bull_crop.png", crop);
    }
    return 0;
}
