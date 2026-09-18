// #1318's non-board camera, as a file source.
//
// The failure this issue is about was measured on Windows hardware nobody here has: a
// laptop's built-in webcam at index 0, pointed at the operator, calibrated as camera 1.
// What made it a dartboard as far as the detector was concerned is in the issue's own
// numbers -- the doubles mask came back at 237,036 white pixels against 27,624 for a real
// board camera in the same run, "skin tones and warm lighting being what the red/green
// mask keys on". This program writes a video with that signature so the Linux container
// can be handed a camera that is not looking at a dartboard.
//
// It is not a picture of a person and does not pretend to be. It is a warm room: a
// skin-toned oval on a warm wall, drifting slightly, which is what the HSV red range
// (hue 0-20 and 160-180, saturation and value over 60) actually keys on. `wall` is the
// other half of the control -- a grey room with no warm colour in it at all -- because a
// filter that only ever refuses red things has not been shown to refuse anything else.
//
//   g++ -O2 -o i1318_make_source testers/i1318_make_source.cpp $(pkg-config --cflags --libs opencv4)
//   ./i1318_make_source face mocks/not_a_board.mp4 1280 720 15 90
#include <opencv2/opencv.hpp>
#include <cstdlib>
#include <cstring>
#include <string>

int main(int argc, char **argv)
{
    const std::string scene = argc > 1 ? argv[1] : "face";
    const std::string path = argc > 2 ? argv[2] : "not_a_board.mp4";
    const int width = argc > 3 ? std::atoi(argv[3]) : 1280;
    const int height = argc > 4 ? std::atoi(argv[4]) : 720;
    const int fps = argc > 5 ? std::atoi(argv[5]) : 15;
    const int frames = argc > 6 ? std::atoi(argv[6]) : 90;

    cv::VideoWriter writer(path, cv::VideoWriter::fourcc('m', 'p', '4', 'v'), fps, cv::Size(width, height));
    if (!writer.isOpened())
    {
        std::fprintf(stderr, "cannot write %s\n", path.c_str());
        return 1;
    }

    cv::RNG rng(20260918);
    for (int n = 0; n < frames; n++)
    {
        cv::Mat frame(height, width, CV_8UC3);
        const double drift = std::sin(n * 0.08);

        if (scene == "wall")
        {
            // A grey office wall. No dartboard, and nothing warm either.
            frame.setTo(cv::Scalar(150, 150, 148));
            cv::rectangle(frame, cv::Rect(0, (int)(height * 0.72), width, height), cv::Scalar(120, 118, 115), -1);
        }
        else
        {
            // A warm room, lit by a bulb: the wall first, then a head and shoulders.
            frame.setTo(cv::Scalar(120, 150, 185)); // BGR: a warm beige wall
            cv::ellipse(frame,
                        cv::Point((int)(width * 0.5 + drift * 8), (int)(height * 0.46)),
                        cv::Size((int)(width * 0.20), (int)(height * 0.34)),
                        0, 0, 360, cv::Scalar(120, 155, 205), -1); // skin
            cv::ellipse(frame,
                        cv::Point((int)(width * 0.5 + drift * 8), (int)(height * 1.05)),
                        cv::Size((int)(width * 0.34), (int)(height * 0.40)),
                        0, 0, 360, cv::Scalar(105, 140, 190), -1); // shoulders
            // Hair, which is the only thing in the picture that is not warm.
            cv::ellipse(frame,
                        cv::Point((int)(width * 0.5 + drift * 8), (int)(height * 0.22)),
                        cv::Size((int)(width * 0.21), (int)(height * 0.16)),
                        0, 180, 360, cv::Scalar(40, 42, 48), -1);
        }

        cv::Mat noise(height, width, CV_8UC3);
        rng.fill(noise, cv::RNG::NORMAL, 0, 4);
        frame += noise;
        writer.write(frame);
    }
    writer.release();
    std::fprintf(stderr, "wrote %s (%s, %dx%d, %d frames)\n", path.c_str(), scene.c_str(), width, height, frames);
    return 0;
}
