// #1339: the same footage with the board filling less of the frame.
//
// Two rigs are two rooms as well as two lenses, so comparing them cannot tell a
// denominator that is scale-free from one that happens to suit the second room. This
// makes the controlled version of the comparison: one clip, every dart and every arm in
// it unchanged, and the single property #1339 is about -- how much of the frame the
// board fills -- moved by a known factor. A ratio taken over the board is unmoved by
// that; a ratio taken over the frame falls with the square of it.
//
//   i1339_scaled_footage in.mp4 out.avi 0.6 [max_frames]
//
// The frame keeps its size; the picture is scaled about its centre and the margin is
// filled with the clip's own first frame, scaled the same way and held still, so the
// border is not a black rectangle that changes the background model.
#include <opencv2/opencv.hpp>
#include <cstdio>
#include <cstdlib>

using namespace cv;

int main(int argc, char **argv)
{
    if (argc < 4)
    {
        fprintf(stderr, "usage: %s in.mp4 out.avi scale [max_frames]\n", argv[0]);
        return 2;
    }
    const double scale = atof(argv[3]);
    const long maxFrames = argc > 4 ? atol(argv[4]) : 0;
    if (!(scale > 0.0 && scale <= 1.0))
    {
        fprintf(stderr, "scale must be in (0,1]\n");
        return 2;
    }

    VideoCapture in(argv[1]);
    if (!in.isOpened())
    {
        fprintf(stderr, "cannot open %s\n", argv[1]);
        return 2;
    }
    const int w = (int)in.get(CAP_PROP_FRAME_WIDTH);
    const int h = (int)in.get(CAP_PROP_FRAME_HEIGHT);
    const double fps = in.get(CAP_PROP_FPS) > 0 ? in.get(CAP_PROP_FPS) : 30.0;

    VideoWriter out(argv[2], VideoWriter::fourcc('M', 'J', 'P', 'G'), fps, Size(w, h));
    if (!out.isOpened())
    {
        fprintf(stderr, "cannot write %s\n", argv[2]);
        return 2;
    }

    const int sw = (int)lround(w * scale), sh = (int)lround(h * scale);
    const int x0 = (w - sw) / 2, y0 = (h - sh) / 2;

    Mat canvas, frame, small;
    long n = 0;
    while (in.read(frame) && !frame.empty())
    {
        if (canvas.empty())
        {
            // The margin: this clip's own first frame, so it looks like the room rather
            // than like a black card, and never moves.
            resize(frame, canvas, Size(w, h), 0, 0, INTER_AREA);
            GaussianBlur(canvas, canvas, Size(31, 31), 0);
        }
        resize(frame, small, Size(sw, sh), 0, 0, INTER_AREA);
        Mat composed = canvas.clone();
        small.copyTo(composed(Rect(x0, y0, sw, sh)));
        out.write(composed);
        if (++n % 300 == 0)
            fprintf(stderr, ".");
        if (maxFrames > 0 && n >= maxFrames)
            break;
    }
    out.release();
    fprintf(stderr, "\n%ld frames at %.2fx into %s\n", n, scale, argv[2]);
    return n > 0 ? 0 : 1;
}
