// #1392: the same board, mounted closer -- which is the same picture behind a longer
// lens, which is a crop.
//
// The defect this issue is about is that a camera was refused on a share of its FRAME,
// and ring pixels go as the square of how much of the frame the board fills. To measure
// that rather than argue it, one clip has to be made to fill more of its own frame with
// the same board, the same room, the same exposure and the same darts. Scaling down is
// already available -- testers/i1339_scaled_footage.cpp, which #1339 wrote for exactly
// this reason -- and it only goes one way: it cannot make a board bigger than the source
// clip already shows it.
//
// So this crops. A window of <w>x<h> is taken around <cx>,<cy> and written at its own
// size, which is what a camera bolted nearer, or one behind a longer lens, really
// produces: the same board, more of the frame, nothing else changed. The window is
// clamped to the source so it never invents pixels, and it is the caller's job to leave
// the board wholly inside it -- ADR-0079 §2 refuses a board that runs off its picture,
// and a crop that clips one is measuring that instead.
//
//   i1392_closer_footage in.mp4 out.avi cx cy w h [max_frames]
//
// MJPG into .avi deliberately, as in i1320_speck_footage and i1321_dark_footage: it is
// the one writer a minimal OpenCV build is always configured with.
#include <opencv2/opencv.hpp>
#include <cstdio>
#include <cstdlib>

using namespace cv;

int main(int argc, char **argv)
{
    if (argc < 7)
    {
        fprintf(stderr, "usage: %s in.mp4 out.avi cx cy w h [max_frames]\n", argv[0]);
        return 2;
    }
    const int cx = atoi(argv[3]), cy = atoi(argv[4]);
    const int cw = atoi(argv[5]), ch = atoi(argv[6]);
    const long maxFrames = argc > 7 ? atol(argv[7]) : 0;

    VideoCapture in(argv[1]);
    if (!in.isOpened())
    {
        fprintf(stderr, "cannot open %s\n", argv[1]);
        return 2;
    }
    const int w = (int)in.get(CAP_PROP_FRAME_WIDTH);
    const int h = (int)in.get(CAP_PROP_FRAME_HEIGHT);
    const double fps = in.get(CAP_PROP_FPS) > 0 ? in.get(CAP_PROP_FPS) : 30.0;

    if (cw <= 0 || ch <= 0 || cw > w || ch > h)
    {
        fprintf(stderr, "the window %dx%d does not fit inside the source's %dx%d\n", cw, ch, w, h);
        return 2;
    }

    int x0 = cx - cw / 2, y0 = cy - ch / 2;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x0 + cw > w) x0 = w - cw;
    if (y0 + ch > h) y0 = h - ch;
    const Rect window(x0, y0, cw, ch);

    VideoWriter out(argv[2], VideoWriter::fourcc('M', 'J', 'P', 'G'), fps, Size(cw, ch));
    if (!out.isOpened())
    {
        fprintf(stderr, "cannot write %s\n", argv[2]);
        return 2;
    }

    Mat frame;
    long n = 0;
    while (in.read(frame) && !frame.empty())
    {
        out.write(frame(window).clone());
        if (++n % 25 == 0) { fprintf(stderr, "."); }
        if (maxFrames > 0 && n >= maxFrames) { break; }
    }
    out.release();
    fprintf(stderr, "\n%ld frames of %dx%d at (%d,%d) into %s\n", n, cw, ch, x0, y0, argv[2]);
    return 0;
}
