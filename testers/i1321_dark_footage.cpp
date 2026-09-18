// #1321: the input that makes a board fail to calibrate, built from the mock footage
// that makes it succeed.
//
// The detector only reads a camera whose path ends in a video extension (camera::
// isVideoFile), so a still JPEG is not an input here -- #1247's 895-dark.sh used three
// of them and measured cameras that would not open, which is the other half of #892 and
// not a calibration failure at all. So the dark input is a file, and it is made rather
// than committed: three darkened clips of mocks/cam_*.mp4 are 300 kB of build output,
// and the same brightness scale applied to the same source gives the same failure on
// any checkout.
//
//   i1321_dark_footage <in.mp4> <out.avi> <scale> <frames>
//
// MJPG into .avi deliberately: it is the one writer a minimal OpenCV build is always
// configured with, and the detector reads it through the same VideoCapture path as the
// mocks.
#include <opencv2/opencv.hpp>
#include <iostream>

int main(int argc, char **argv)
{
    if (argc < 5)
    {
        std::cerr << "usage: i1321_dark_footage <in> <out> <scale> <frames>" << std::endl;
        return 2;
    }
    const std::string in = argv[1], out = argv[2];
    const double scale = atof(argv[3]);
    const int wanted = atoi(argv[4]);

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

    cv::VideoWriter writer(out, cv::VideoWriter::fourcc('M', 'J', 'P', 'G'), fps, cv::Size(w, h));
    if (!writer.isOpened())
    {
        std::cerr << "cannot write " << out << std::endl;
        return 1;
    }

    cv::Mat frame, dark;
    int written = 0;
    while (written < wanted && cap.read(frame) && !frame.empty())
    {
        frame.convertTo(dark, -1, scale, 0.0);
        writer.write(dark);
        written++;
    }
    writer.release();
    std::cout << "wrote " << written << " frames of " << w << "x" << h << " at scale " << scale
              << " to " << out << std::endl;
    return written > 0 ? 0 : 1;
}
