// #1317: the input that makes the wire stage find fewer than twenty wires, built from
// footage on which it finds twenty.
//
// The maintainer's rig produced `Selected 9 averaged wires from 21 candidates` live on
// 2026-09-18, and mocks/rig-20260918 is that rig -- but a recording is one frame at a
// time and the frame the detector calibrates on is whichever one it seeks to, so the
// clean fifteen seconds of that fixture give 20, 20 and 21 wires rather than nine (see
// testers/phases1317/1317-partial.sh, which measures the scan). The partial detection is
// therefore constructed, the way #1321 constructs its dark footage: from the same source,
// by one stated transform, so the same input gives the same count on any checkout.
//
//   i1317_partial_footage <in> <out> <start-seconds> <occlude-fraction> <blur> <frames>
//
// Two transforms, either or neither, because the two halves of the fault are reached
// differently and both were measured before this file was settled.
//
// `occlude-fraction` blacks out that fraction of the frame's width, from the right edge:
// something standing in front of one side of the board. It costs boundary points as well
// as wires, so past about a quarter of the width the camera is refused at the ellipse
// stage and never reaches the wires at all -- which is a fine thing for a camera to do
// and no use as an input to this issue.
//
// `blur` is the one that reaches the wire stage. A Gaussian of that kernel width softens
// the colour transitions and the Hough edges the wire detector reads, while the doubles
// ring -- a wide annulus found by ray tracing rather than by edges -- is still fitted from
// well over the fifty rays the ellipse fitter needs. It is a camera slightly out of focus,
// which is a thing a rig really is, and it is how this tester gets a board that calibrates
// its ring and comes up short on its wires.
//
// MJPG into .avi for the same reason #1321 gives: the one writer a minimal OpenCV build
// is always configured with, read back through the same VideoCapture path as the mocks.
#include <opencv2/opencv.hpp>
#include <iostream>

int main(int argc, char **argv)
{
    if (argc < 7)
    {
        std::cerr << "usage: i1317_partial_footage <in> <out> <start-seconds> <occlude-fraction> <blur> <frames>" << std::endl;
        return 2;
    }
    const std::string in = argv[1], out = argv[2];
    const double start_seconds = atof(argv[3]);
    const double occlude = atof(argv[4]);
    const int blur = atoi(argv[5]);
    const int wanted = atoi(argv[6]);

    cv::VideoCapture cap(in);
    if (!cap.isOpened())
    {
        std::cerr << "cannot open " << in << std::endl;
        return 1;
    }
    const int w = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_WIDTH));
    const int h = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_HEIGHT));
    double fps = cap.get(cv::CAP_PROP_FPS);
    if (fps <= 1.0 || fps > 240.0)
    {
        fps = 15.0;
    }
    if (start_seconds > 0.0)
    {
        cap.set(cv::CAP_PROP_POS_FRAMES, static_cast<int>(fps * start_seconds));
    }

    cv::VideoWriter writer(out, cv::VideoWriter::fourcc('M', 'J', 'P', 'G'), fps, cv::Size(w, h));
    if (!writer.isOpened())
    {
        std::cerr << "cannot write " << out << std::endl;
        return 1;
    }

    const int keep = (occlude > 0.0 && occlude < 1.0) ? static_cast<int>(w * (1.0 - occlude)) : w;
    cv::Mat frame;
    int written = 0;
    while (written < wanted && cap.read(frame) && !frame.empty())
    {
        if (keep < w)
        {
            frame(cv::Rect(keep, 0, w - keep, h)).setTo(cv::Scalar(0, 0, 0));
        }
        if (blur > 1)
        {
            const int k = (blur % 2 == 0) ? blur + 1 : blur;
            cv::GaussianBlur(frame, frame, cv::Size(k, k), 0.0);
        }
        writer.write(frame);
        written++;
    }
    writer.release();
    std::cout << "wrote " << written << " frames of " << w << "x" << h
              << " from " << start_seconds << "s, occluding " << occlude
              << " of the width, blur " << blur << ", to " << out << std::endl;
    return written > 0 ? 0 : 1;
}
