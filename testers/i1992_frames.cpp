// turnaus#1992: the 30-frame average of a clip's opening, as a raw PPM, for
// testers/i1992_crossing_census.py. The image holds no ffmpeg and no cv2 for python, so
// the one footage step of the census is this file: read `count` frames from `start`,
// average them as the detector's own calibration does, write P6. Measurement tooling;
// unrun-tester: it makes the census instrument's input frames from footage and asserts
// nothing; run by hand before testers/i1992_crossing_census.py --frames (docs/rig.md).
// nothing in the detector includes it.
//
//   i1992_frames <clip.mp4> <start frame> <count> <out.ppm>
#include <opencv2/opencv.hpp>
#include <cstdio>
#include <cstdlib>

int main(int argc, char **argv)
{
    if (argc != 5)
    {
        fprintf(stderr, "usage: i1992_frames <clip> <start> <count> <out.ppm>\n");
        return 2;
    }
    cv::VideoCapture cap(argv[1]);
    if (!cap.isOpened())
    {
        fprintf(stderr, "i1992_frames: cannot open %s\n", argv[1]);
        return 1;
    }
    const int start = atoi(argv[2]), count = atoi(argv[3]);
    cv::Mat frame, sum;
    int taken = 0;
    for (int i = 0; i < start + count; i++)
    {
        if (!cap.read(frame) || frame.empty())
        {
            break;
        }
        if (i < start)
        {
            continue;
        }
        cv::Mat f;
        frame.convertTo(f, CV_32FC3);
        if (sum.empty())
        {
            sum = f;
        }
        else
        {
            sum += f;
        }
        taken++;
    }
    if (taken != count)
    {
        fprintf(stderr, "i1992_frames: %s gave %d of %d frames from %d\n", argv[1], taken, count, start);
        return 1;
    }
    cv::Mat mean, rgb;
    sum.convertTo(mean, CV_8UC3, 1.0 / taken);
    cv::cvtColor(mean, rgb, cv::COLOR_BGR2RGB);
    FILE *out = fopen(argv[4], "wb");
    if (!out)
    {
        return 1;
    }
    fprintf(out, "P6\n%d %d\n255\n", rgb.cols, rgb.rows);
    for (int y = 0; y < rgb.rows; y++)
    {
        fwrite(rgb.ptr(y), 1, (size_t)rgb.cols * 3, out);
    }
    fclose(out);
    printf("i1992_frames: %s frames %d..%d -> %s (%dx%d)\n", argv[1], start, start + count - 1, argv[4], rgb.cols, rgb.rows);
    return 0;
}
