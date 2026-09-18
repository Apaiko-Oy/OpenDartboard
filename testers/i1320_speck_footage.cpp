// #1320: the input where a small round thing would win. Built from the mock footage
// the detector already calibrates on, so the only difference between the control and
// this clip is the discs painted on it.
//
// The frame in the issue is the maintainer's, from a rig that is not in this repository
// and a camera that is not in these mocks, so it cannot be committed as a fixture. What
// can be committed is the shape of the failure: a small, very round, off-centre blob in
// the red/green mask, which under the old rule -- circularity * 0.8 plus a term that
// paid MORE the smaller the thing was -- outscores the bull it is nowhere near.
//
// It is made rather than committed for #1321's reason: three clips are build output,
// and the same discs painted on the same source give the same answer on any checkout.
//
//   i1320_speck_footage <in.mp4> <out.avi> <frames> <dx> <dy> x,y,r[ x,y,r]...
//
// <dx> <dy> shift the whole frame first, which is how the camera in #1320 was aimed:
// slightly right and low, so that the middle of the frame and the middle of the board
// stop being the same place. That matters to more than framing -- color_processing
// keeps a small blob when it is within a frame-width/10 of the middle of the FRAME,
// which on an off-aimed camera is a window sitting well off the middle of the BOARD.
//
// Each triple is a filled red disc in shifted-frame coordinates. Red because that is
// what color_processing detects; a disc within a frame-width/10 of the middle of the
// frame survives its component filter as bull's-eye area, which is exactly how a speck
// reaches bull detection on a real board.
//
// MJPG into .avi deliberately, as in i1321_dark_footage: it is the one writer a minimal
// OpenCV build is always configured with.
#include <opencv2/opencv.hpp>
#include <iostream>
#include <vector>

int main(int argc, char **argv)
{
    if (argc < 7)
    {
        std::cerr << "usage: i1320_speck_footage <in> <out> <frames> <dx> <dy> x,y,r [x,y,r]..." << std::endl;
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
