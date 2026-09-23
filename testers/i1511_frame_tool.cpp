// #1511: the annotation instrument -- frames out of a fixture clip, by index, with a
// coordinate grid a person can read pixel positions off.
//
// The shaft annotations in testers/i1511_annotations/ were measured BY EYE with this
// tool, never by the detector (#1504: annotation-by-detection-order is what the
// acceptance forbids). Three modes:
//
//   sheet <video> <out.jpg> <first> <stride> <count>
//       a contact sheet of `count` thumbnails every `stride` frames from `first`,
//       frame numbers burned in -- how landing moments are FOUND.
//   frame <video> <out.jpg> <index> [prev]
//       one full frame; with `prev`, the earlier frame is blended in red beside it so
//       the NEW dart is obvious to a human eye (the line is still measured on the raw
//       pixels, the blend only says which dart arrived).
//   crop <video> <out.jpg> <index> <cx> <cy> <half> [scale]
//       an upscaled crop centred on (cx,cy), half-width `half`, with a 25 px grid in
//       FULL-FRAME coordinates labelled every 50 px -- how a line's two points are
//       READ.
//   check <video> <out.jpg> <index> <x1> <y1> <x2> <y2> [half] [scale]
//       the crop again, with the CANDIDATE line drawn on it (endpoints dotted, the
//       line extended thin). A coordinate is easy to misread and a line lying beside
//       a shaft is easy to see, so every annotation is refined and finally ACCEPTED
//       through this mode; the accepted images are the annotation's own evidence.
//
// It decides nothing and asserts nothing; it is a ruler.

#include <opencv2/opencv.hpp>
#include <cstdio>
#include <cstdlib>
#include <string>

using namespace cv;

static Mat frameAt(VideoCapture &cap, int index)
{
    cap.set(CAP_PROP_POS_FRAMES, index);
    Mat f;
    cap.read(f);
    return f;
}

int main(int argc, char **argv)
{
    if (argc < 4)
    {
        printf("usage: sheet|frame|crop ... (see header)\n");
        return 2;
    }
    const std::string mode = argv[1];
    VideoCapture cap(argv[2]);
    if (!cap.isOpened())
    {
        printf("cannot open %s\n", argv[2]);
        return 1;
    }
    const std::string out = argv[3];

    if (mode == "sheet" && argc >= 7)
    {
        const int first = atoi(argv[4]), stride = atoi(argv[5]), count = atoi(argv[6]);
        const int cols = 6, thumbW = 420;
        const int rows = (count + cols - 1) / cols;
        Mat sheet;
        int thumbH = 0;
        for (int k = 0; k < count; k++)
        {
            Mat f = frameAt(cap, first + k * stride);
            if (f.empty())
            {
                break;
            }
            Mat t;
            resize(f, t, Size(thumbW, thumbW * f.rows / f.cols));
            if (sheet.empty())
            {
                thumbH = t.rows;
                sheet = Mat::zeros(rows * thumbH, cols * thumbW, CV_8UC3);
            }
            putText(t, "f" + std::to_string(first + k * stride), Point(6, 24),
                    FONT_HERSHEY_SIMPLEX, 0.8, Scalar(0, 255, 255), 2);
            t.copyTo(sheet(Rect((k % cols) * thumbW, (k / cols) * thumbH, thumbW, thumbH)));
        }
        if (sheet.empty())
        {
            printf("no frames\n");
            return 1;
        }
        imwrite(out, sheet);
        printf("sheet %s\n", out.c_str());
        return 0;
    }
    if (mode == "frame" && argc >= 5)
    {
        const int index = atoi(argv[4]);
        Mat f = frameAt(cap, index);
        if (f.empty())
        {
            printf("no frame %d\n", index);
            return 1;
        }
        if (argc >= 6)
        {
            Mat prev = frameAt(cap, atoi(argv[5]));
            if (!prev.empty())
            {
                Mat gNow, gPrev, diff;
                cvtColor(f, gNow, COLOR_BGR2GRAY);
                cvtColor(prev, gPrev, COLOR_BGR2GRAY);
                absdiff(gNow, gPrev, diff);
                threshold(diff, diff, 25, 255, THRESH_BINARY);
                // paint what changed red ON the current frame, for the eye alone
                std::vector<Mat> ch(3);
                split(f, ch);
                ch[2] = max(ch[2], diff);
                merge(ch, f);
            }
        }
        putText(f, "f" + std::to_string(index), Point(8, 30), FONT_HERSHEY_SIMPLEX, 1.0,
                Scalar(0, 255, 255), 2);
        imwrite(out, f);
        printf("frame %s\n", out.c_str());
        return 0;
    }
    if (mode == "check" && argc >= 9)
    {
        const int index = atoi(argv[4]);
        const double x1 = atof(argv[5]), y1 = atof(argv[6]), x2 = atof(argv[7]), y2 = atof(argv[8]);
        const int half = argc >= 10 ? atoi(argv[9]) : 130;
        const int scale = argc >= 11 ? atoi(argv[10]) : 3;
        Mat f = frameAt(cap, index);
        if (f.empty())
        {
            printf("no frame %d\n", index);
            return 1;
        }
        const int cx = (int)((x1 + x2) / 2), cy = (int)((y1 + y2) / 2);
        const Rect roi(std::max(0, cx - half), std::max(0, cy - half),
                       std::min(f.cols - std::max(0, cx - half), 2 * half),
                       std::min(f.rows - std::max(0, cy - half), 2 * half));
        Mat big;
        resize(f(roi), big, Size(), scale, scale, INTER_NEAREST);
        auto toBig = [&](double x, double y)
        { return Point((int)((x - roi.x) * scale), (int)((y - roi.y) * scale)); };
        // the line, extended across the crop, thin, so the shaft stays visible under it
        const Point2d d(x2 - x1, y2 - y1);
        const double n = std::sqrt(d.x * d.x + d.y * d.y);
        if (n > 0)
        {
            const Point2d u(d.x / n, d.y / n);
            line(big, toBig(x1 - u.x * 500, y1 - u.y * 500), toBig(x2 + u.x * 500, y2 + u.y * 500),
                 Scalar(255, 255, 0), 1);
        }
        circle(big, toBig(x1, y1), 5, Scalar(255, 0, 255), 2);
        circle(big, toBig(x2, y2), 5, Scalar(255, 0, 255), 2);
        char cap1[128];
        snprintf(cap1, sizeof(cap1), "f%d line (%.0f,%.0f)-(%.0f,%.0f)", index, x1, y1, x2, y2);
        putText(big, cap1, Point(8, big.rows - 34), FONT_HERSHEY_SIMPLEX, 0.7, Scalar(255, 255, 0), 2);
        imwrite(out, big);
        printf("check %s roi=%d,%d %dx%d\n", out.c_str(), roi.x, roi.y, roi.width, roi.height);
        return 0;
    }
    if (mode == "crop" && argc >= 7)
    {
        const int index = atoi(argv[4]), cx = atoi(argv[5]), cy = atoi(argv[6]);
        const int half = argc >= 8 ? atoi(argv[7]) : 120;
        const int scale = argc >= 9 ? atoi(argv[8]) : 4;
        Mat f = frameAt(cap, index);
        if (f.empty())
        {
            printf("no frame %d\n", index);
            return 1;
        }
        const Rect roi(std::max(0, cx - half), std::max(0, cy - half),
                       std::min(f.cols - std::max(0, cx - half), 2 * half),
                       std::min(f.rows - std::max(0, cy - half), 2 * half));
        Mat big;
        resize(f(roi), big, Size(), scale, scale, INTER_NEAREST);
        // A sparse grid a person can actually anchor a reading to: dim lines every
        // 25 full-frame px, bright labelled lines every 50, labels repeated top and
        // bottom / both sides so no reading is far from a number.
        for (int x = ((roi.x + 24) / 25) * 25; x < roi.x + roi.width; x += 25)
        {
            const int gx = (x - roi.x) * scale;
            const bool major = x % 50 == 0;
            line(big, Point(gx, 0), Point(gx, big.rows),
                 major ? Scalar(0, 100, 255) : Scalar(0, 160, 0), 1);
            if (major)
            {
                putText(big, std::to_string(x), Point(gx + 3, 22), FONT_HERSHEY_SIMPLEX, 0.6,
                        Scalar(0, 100, 255), 2);
                putText(big, std::to_string(x), Point(gx + 3, big.rows - 8), FONT_HERSHEY_SIMPLEX,
                        0.6, Scalar(0, 100, 255), 2);
            }
        }
        for (int y = ((roi.y + 24) / 25) * 25; y < roi.y + roi.height; y += 25)
        {
            const int gy = (y - roi.y) * scale;
            const bool major = y % 50 == 0;
            line(big, Point(0, gy), Point(big.cols, gy),
                 major ? Scalar(0, 100, 255) : Scalar(0, 160, 0), 1);
            if (major)
            {
                putText(big, std::to_string(y), Point(4, gy - 4), FONT_HERSHEY_SIMPLEX, 0.6,
                        Scalar(0, 100, 255), 2);
                putText(big, std::to_string(y), Point(big.cols - 70, gy - 4), FONT_HERSHEY_SIMPLEX,
                        0.6, Scalar(0, 100, 255), 2);
            }
        }
        imwrite(out, big);
        printf("crop %s roi=%d,%d %dx%d scale=%d\n", out.c_str(), roi.x, roi.y, roi.width,
               roi.height, scale);
        return 0;
    }
    printf("bad arguments\n");
    return 2;
}
