// #1335: one frame of a video, written as a JPEG.
//
// The blind-camera phases (#892, #895, #1247, #1274) hand the detector a still image
// where a camera should be. They read that still from /tmp, or from another issue's run
// directory, so on a box that has rebooted -- or on any box but the one the issue was
// carried on -- the copy failed and the phase measured nothing. The still is a frame of
// the mocks this repository ships, so it is made from them rather than found.
//
//   still_frame <in.mp4> <out.jpg> [frame-index]
#include <opencv2/opencv.hpp>
#include <iostream>

int main(int argc, char **argv)
{
    if (argc < 3)
    {
        std::cerr << "usage: still_frame <in> <out.jpg> [frame-index]" << std::endl;
        return 2;
    }
    const int wanted = (argc > 3) ? atoi(argv[3]) : 0;
    cv::VideoCapture cap(argv[1]);
    if (!cap.isOpened())
    {
        std::cerr << "cannot open " << argv[1] << std::endl;
        return 1;
    }
    cv::Mat frame;
    for (int i = 0; i <= wanted; i++)
    {
        if (!cap.read(frame) || frame.empty())
        {
            break;
        }
    }
    if (frame.empty())
    {
        std::cerr << "no frame read from " << argv[1] << std::endl;
        return 1;
    }
    if (!cv::imwrite(argv[2], frame))
    {
        std::cerr << "cannot write " << argv[2] << std::endl;
        return 1;
    }
    std::cout << "wrote " << frame.cols << "x" << frame.rows << " to " << argv[2] << std::endl;
    return 0;
}
