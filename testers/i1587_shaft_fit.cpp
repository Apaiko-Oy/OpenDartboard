// #1587: the arrival-mask line fit that testers/i1511_annotations/README.md describes
// ("rig-20260922 since #1585"), as a program, so a row it produced can be re-derived.
//
//   fit   <video> <before> <after> <x0> <x1> <y0> <y1> [thresh=26] [minw=3] [maxw=24]
//       mask = max-channel |frame(after) - frame(before)| > thresh, inside the column
//       window [x0,x1] and the row window [y0,y1]. Per image row the longest contiguous
//       run of mask is taken; runs narrower than minw or wider than maxw (a flight) are
//       dropped; x = m*y + c is least-squares fitted to the run centres with three
//       rounds of 4 px outlier rejection. Prints the fitted line at the highest and
//       lowest kept rows (the lowest is the visible entry, `tip`), the kept row count,
//       the span and the max residual.
//   trace <video> <ref> <first> <last> <x0> <x1> <y0> <y1> [thresh=26]
//       per frame, how many pixels of the window differ from frame `ref` -- how an
//       arrival frame is FOUND without asking the detector.
//
// `before` and `after` are chosen from the trace so that the mask holds exactly one
// dart: after the previous arrival has settled, before the next one lands. It decides
// nothing and asserts nothing; it is a ruler, like i1511_frame_tool.cpp beside it.
//
// unrun-tester: an instrument a person runs by hand to measure annotation rows; the
// README beside testers/i1511_annotations/ records the invocations.

#include <opencv2/opencv.hpp>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace cv;

static Mat frameAt(VideoCapture &cap, int index)
{
    cap.set(CAP_PROP_POS_FRAMES, index);
    Mat f;
    cap.read(f);
    return f;
}

static Mat diffMask(const Mat &a, const Mat &b, int thresh)
{
    Mat d;
    absdiff(a, b, d);
    std::vector<Mat> ch;
    split(d, ch);
    Mat m = max(max(ch[0], ch[1]), ch[2]);
    return m > thresh;
}

int main(int argc, char **argv)
{
    if (argc < 3)
    {
        printf("usage: fit|trace ... (see header)\n");
        return 2;
    }
    const std::string mode = argv[1];
    VideoCapture cap(argv[2]);
    if (!cap.isOpened())
    {
        printf("cannot open %s\n", argv[2]);
        return 1;
    }
    if (mode == "trace" && argc >= 10)
    {
        int ref = atoi(argv[3]), first = atoi(argv[4]), last = atoi(argv[5]);
        Rect r(atoi(argv[6]), atoi(argv[8]), atoi(argv[7]) - atoi(argv[6]),
               atoi(argv[9]) - atoi(argv[8]));
        int thresh = argc > 10 ? atoi(argv[10]) : 26;
        Mat base = frameAt(cap, ref);
        for (int i = first; i <= last; ++i)
        {
            Mat f = frameAt(cap, i);
            printf("frame %d changed %d\n", i, countNonZero(diffMask(f, base, thresh)(r)));
        }
        return 0;
    }
    if (mode != "fit" || argc < 9)
    {
        printf("bad arguments\n");
        return 2;
    }
    int before = atoi(argv[3]), after = atoi(argv[4]);
    int x0 = atoi(argv[5]), x1 = atoi(argv[6]), y0 = atoi(argv[7]), y1 = atoi(argv[8]);
    int thresh = argc > 9 ? atoi(argv[9]) : 26;
    int minw = argc > 10 ? atoi(argv[10]) : 3;
    int maxw = argc > 11 ? atoi(argv[11]) : 24;
    Mat mask = diffMask(frameAt(cap, after), frameAt(cap, before), thresh);
    std::vector<double> ys, xs;
    for (int y = std::max(0, y0); y <= std::min(mask.rows - 1, y1); ++y)
    {
        int bestStart = -1, bestLen = 0, start = -1;
        for (int x = std::max(0, x0); x <= std::min(mask.cols - 1, x1) + 1; ++x)
        {
            bool on = x <= std::min(mask.cols - 1, x1) && mask.at<uchar>(y, x);
            if (on && start < 0)
                start = x;
            if (!on && start >= 0)
            {
                if (x - start > bestLen)
                {
                    bestLen = x - start;
                    bestStart = start;
                }
                start = -1;
            }
        }
        if (bestLen >= minw && bestLen <= maxw)
        {
            ys.push_back(y);
            xs.push_back(bestStart + (bestLen - 1) / 2.0);
        }
    }
    std::vector<bool> keep(ys.size(), true);
    double m = 0, c = 0;
    for (int round = 0; round < 4; ++round)
    {
        double sy = 0, sx = 0, syy = 0, sxy = 0;
        int n = 0;
        for (size_t i = 0; i < ys.size(); ++i)
            if (keep[i])
            {
                sy += ys[i];
                sx += xs[i];
                syy += ys[i] * ys[i];
                sxy += ys[i] * xs[i];
                ++n;
            }
        if (n < 2)
        {
            printf("too few rows (%d)\n", n);
            return 1;
        }
        double den = n * syy - sy * sy;
        m = den != 0 ? (n * sxy - sy * sx) / den : 0;
        c = (sx - m * sy) / n;
        if (round == 3)
            break;
        for (size_t i = 0; i < ys.size(); ++i)
            keep[i] = std::fabs(xs[i] - (m * ys[i] + c)) <= 4.0;
    }
    double ytop = 1e9, ybot = -1e9, maxres = 0;
    int kept = 0;
    for (size_t i = 0; i < ys.size(); ++i)
        if (keep[i])
        {
            ++kept;
            ytop = std::min(ytop, ys[i]);
            ybot = std::max(ybot, ys[i]);
            maxres = std::max(maxres, std::fabs(xs[i] - (m * ys[i] + c)));
        }
    printf("FIT rows=%zu kept=%d span=%.0f..%.0f maxres=%.2f line=%.0f,%.0f,%.0f,%.0f "
           "tip=%.0f,%.0f\n",
           ys.size(), kept, ytop, ybot, maxres, m * ytop + c, ytop, m * ybot + c, ybot,
           m * ybot + c, ybot);
    return 0;
}
