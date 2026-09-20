// #1484: how long a clip is, so a run can say how much of it it consumed.
//
// The trap this issue is about is a truncated run that reads as a whole one: OD_MAX_CYCLES
// stops the detector mid-clip and the last line of the log is an ordinary END, exactly
// like a visit that finished. The detector says which millisecond of the stream each
// camera stopped on; what it cannot say is how many milliseconds there were. That is this.
//
//   i1484_clip_length <clip.mp4> [<clip.mp4> ...]
//   -> "<path> <frames> <fps> <duration_ms>" per clip, one per line
//
// A clip that cannot be opened prints a duration of 0 and says so on stderr, rather than
// being left out: a missing line would read as a clip whose length is unknown, and a
// length of zero is the thing a reader can notice.
#include <opencv2/opencv.hpp>
#include <iostream>

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        std::cerr << "usage: i1484_clip_length <clip.mp4> [...]" << std::endl;
        return 2;
    }
    int bad = 0;
    for (int i = 1; i < argc; i++)
    {
        cv::VideoCapture cap(argv[i]);
        double frames = 0, fps = 0, duration_ms = 0;
        if (!cap.isOpened())
        {
            std::cerr << "i1484_clip_length: cannot open " << argv[i] << std::endl;
            bad = 1;
        }
        else
        {
            frames = cap.get(cv::CAP_PROP_FRAME_COUNT);
            fps = cap.get(cv::CAP_PROP_FPS);
            if (fps > 0 && frames > 0)
            {
                duration_ms = 1000.0 * frames / fps;
            }
        }
        std::cout << argv[i] << " " << (long)frames << " " << fps << " " << (long)duration_ms << std::endl;
    }
    return bad;
}
