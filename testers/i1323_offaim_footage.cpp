// #1323: the input where the board is not where the frame's middle is.
//
// Built from the mock footage the detector already calibrates on, the way
// i1320_speck_footage is, because the camera this issue was measured on is the
// maintainer's and cannot be committed. What can be committed is the framing: the whole
// frame shifted right and low, so that the middle of the FRAME and the middle of the
// BOARD stop being the same place, with the board still whole and still in shot.
//
//   i1323_offaim_footage <in.mp4> <out.avi> <frames> <dx> <dy> [x,y,r]...
//
// The discs are optional, and that is the only thing this does that
// i1320_speck_footage cannot: an off-aimed clip with nothing painted on it is what
// proves the real bull is found, and one with a speck painted near the middle of the
// frame is what proves the speck is still not. Each triple is a filled red disc in
// shifted-frame coordinates, red because that is what color_processing detects.
//
// MJPG into .avi deliberately, as in i1320_speck_footage and i1321_dark_footage: it is
// the one writer a minimal OpenCV build is always configured with.
#include <opencv2/opencv.hpp>
#include <iostream>
#include <vector>

int main(int argc, char **argv)
{
    if (argc < 6)
    {
        std::cerr << "usage: i1323_offaim_footage <in> <out> <frames> <dx> <dy> [x,y,r]..." << std::endl;
        return 2;
    }
    const std::string in = argv[1], out = argv[2];
    const int wanted = atoi(argv[3]);
    const double dx = atof(argv[4]), dy = atof(argv[5]);

    std::vector<cv::Point> centres;
    std::vector<int> radii;
    for (int a = 6; a < argc; a++)
    {
        int x = 0, y = 0, r = 0;
        if (sscanf(argv[a], "%d,%d,%d", &x, &y, &r) != 3)
        {
            std::cerr << "bad disc '" << argv[a] << "', want x,y,r" << std::endl;
            return 2;
        }
        centres.push_back(cv::Point(x, y));
        radii.push_back(r);
    }

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

    cv::Mat frame, shifted;
    const cv::Mat shift = (cv::Mat_<double>(2, 3) << 1, 0, dx, 0, 1, dy);
    int written = 0;
    while (written < wanted && cap.read(frame) && !frame.empty())
    {
        cv::warpAffine(frame, shifted, shift, frame.size(), cv::INTER_LINEAR, cv::BORDER_REPLICATE);
        frame = shifted;
        for (size_t d = 0; d < centres.size(); d++)
        {
            cv::circle(frame, centres[d], radii[d], cv::Scalar(0, 0, 255), -1, cv::LINE_AA);
        }
        writer.write(frame);
        written++;
    }
    writer.release();
    std::cout << "wrote " << written << " frames of " << w << "x" << h << " shifted by ("
              << dx << "," << dy << ") with " << centres.size() << " disc(s) to " << out << std::endl;
    return written > 0 ? 0 : 1;
}
