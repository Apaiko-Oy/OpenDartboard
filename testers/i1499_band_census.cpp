// #1499: is the treble's 8% shortfall a fact of the optics, a fact of the board, or a
// fact of the fit? Three positions are measured along every ray from the bull and read
// against each other, because they are three different claims:
//
//   RAW    where the PAINT is: the averaged frame's own pixels classified red/green by a
//          permissive HSV test with no blur and no morphology, so nothing the pipeline
//          does to an edge is in it. This is the closest thing to the physical board a
//          camera can testify to.
//   PIPE   where the COLOUR STAGE puts the band: the same ray read off
//          color_processing::processColors' output, whose bilateral filter and closing
//          are allowed to move edges.
//   FIT    where the FITTED ELLIPSES sit: the crossing of calib.ellipses'
//          inner/outerTripleEllipse along the same ray -- what scorePoint judges darts by.
//
// Every position is printed as a fraction of the ray-traced doubles ellipse's crossing
// along the SAME ray, which is the radial ruler's own normalisation, so perspective is
// taken out to first order and the millimetres have a number to be compared with:
// 99/170 = 0.5824 and 107/170 = 0.6294.
//
// How the three separate the hypotheses:
//   RAW at the millimetres, FIT short            -> the fit reads the wrong points
//   RAW short everywhere, flat across angles     -> the board (or the shared optics)
//   RAW short, varying strongly with angle       -> perspective/centre error, optical
//   the same shortfall on the Unicorn mocks      -> neither this board nor this rig
//
//   i1499_band_census <clip> <cam-index> [annotate.jpg]
//
// One I1499RAY line per 3 degrees; an I1499 summary line with the medians; optionally an
// annotated still: fitted treble ellipses in magenta, the spec-scaled treble marks from
// the doubles ellipse in green, so the picture can settle what the numbers claim.
#include <opencv2/opencv.hpp>
#include <iostream>
#include <string>
#include <vector>
#include <cmath>
#include <algorithm>

#include "geometry_calibration.hpp"
#include "detector/geometry/calibration/color_processing.hpp"

namespace
{
    int seekFrameFor(int cameraIndex, double fps)
    {
        const double seconds = 3.0 - (cameraIndex * 0.18);
        if (seconds <= 0 || fps <= 0) return 0;
        return (int)(fps * seconds);
    }

    bool averageOf(cv::VideoCapture &cap, int numFrames, cv::Mat &out)
    {
        cv::Mat sum; int consumed = 0;
        for (int i = 0; i < numFrames; i++)
        {
            cv::Mat f;
            if (!cap.read(f) || f.empty()) break;
            cv::Mat ff; f.convertTo(ff, CV_32F);
            if (sum.empty()) sum = ff; else sum += ff;
            consumed++;
        }
        if (consumed == 0) return false;
        cv::Mat mean = sum / consumed;
        mean.convertTo(out, CV_8U);
        return true;
    }

    // The same crossing arithmetic score_processing's ruler uses (i1485's copy).
    double reachTo(cv::Point2f origin, cv::Point2f dir, const cv::RotatedRect &e)
    {
        double a = e.size.width / 2.0, b = e.size.height / 2.0;
        if (!(a > 0.0) || !(b > 0.0)) return -1.0;
        double th = -e.angle * CV_PI / 180.0, c = cos(th), s = sin(th);
        cv::Point2f rel = origin - e.center;
        double x0 = rel.x * c - rel.y * s, y0 = rel.x * s + rel.y * c;
        double dx = dir.x * c - dir.y * s, dy = dir.x * s + dir.y * c;
        double A = dx * dx / (a * a) + dy * dy / (b * b);
        double B = 2.0 * (x0 * dx / (a * a) + y0 * dy / (b * b));
        double C = x0 * x0 / (a * a) + y0 * y0 / (b * b) - 1.0;
        double disc = B * B - 4 * A * C;
        if (disc < 0 || !(A > 0)) return -1.0;
        double t = (-B + sqrt(disc)) / (2 * A);
        return t > 0 ? t : -1.0;
    }

    // Permissive, morphology-free: is this pixel dartboard red or green? Looser than
    // color_processing on saturation and value on purpose -- the question is where the
    // paint IS, not where the pipeline keeps it -- and tight enough on hue that cream,
    // black and wire do not answer.
    bool rawRedGreen(const cv::Vec3b &hsv)
    {
        const int h = hsv[0], s = hsv[1], v = hsv[2];
        const bool red = (h <= 12 || h >= 165) && s >= 70 && v >= 45;
        const bool green = (h >= 35 && h <= 92) && s >= 55 && v >= 35;
        return red || green;
    }

    bool pipeRedGreen(const cv::Vec3b &bgr)
    {
        return bgr == cv::Vec3b(0, 0, 255) || bgr == cv::Vec3b(0, 255, 0);
    }

    struct Band { double in = -1.0, out = -1.0; };

    // The first and last classified pixel between lo and hi board-fractions along the
    // ray, where the board is `edge` pixels out. A run must be at least minRun pixels so
    // a fleck does not become an edge.
    template <typename IsBand>
    Band bandAlongRay(cv::Point2f bull, cv::Point2f u, double edge, double lo, double hi,
                      const cv::Mat &img, IsBand isBand)
    {
        Band band;
        const int from = (int)(edge * lo), to = (int)(edge * hi);
        int run = 0; const int minRun = 3;
        int firstAt = -1, lastAt = -1;
        for (int d = from; d <= to; d++)
        {
            cv::Point p(cvRound(bull.x + u.x * d), cvRound(bull.y + u.y * d));
            if (p.x < 0 || p.y < 0 || p.x >= img.cols || p.y >= img.rows) break;
            if (isBand(img.at<cv::Vec3b>(p)))
            {
                run++;
                if (run >= minRun)
                {
                    if (firstAt < 0) firstAt = d - run + 1;
                    lastAt = d;
                }
            }
            else
            {
                run = 0;
            }
        }
        if (firstAt >= 0) { band.in = firstAt / edge; band.out = lastAt / edge; }
        return band;
    }

    double median(std::vector<double> v)
    {
        if (v.empty()) return -1.0;
        std::sort(v.begin(), v.end());
        return v[v.size() / 2];
    }

    std::string fmt(double v)
    {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.4f", v);
        return buf;
    }
}

int main(int argc, char **argv)
{
    if (argc < 3) { std::cerr << "usage: i1499_band_census <clip> <cam-index> [annotate.jpg]\n"; return 2; }
    const std::string clip = argv[1];
    const int camIdx = std::atoi(argv[2]);
    const std::string annotate = (argc > 3) ? argv[3] : "";

    cv::VideoCapture cap(clip);
    if (!cap.isOpened()) { std::cerr << "cannot open " << clip << "\n"; return 2; }
    cap.set(cv::CAP_PROP_POS_FRAMES, seekFrameFor(camIdx, cap.get(cv::CAP_PROP_FPS)));

    cv::Mat frame;
    if (!averageOf(cap, 30, frame)) { std::cerr << "no frames in " << clip << "\n"; return 2; }

    DartboardCalibration calib = geometry_calibration::calibrateSingleCamera(frame, camIdx, false);
    const auto &E = calib.ellipses;
    if (!calib.sees_board || !E.hasValidDoubles)
    {
        std::cout << "I1499 clip=" << clip << " cam=" << camIdx << " ok=0" << std::endl;
        return 0;
    }

    cv::Mat hsv;
    cv::cvtColor(frame, hsv, cv::COLOR_BGR2HSV);
    color_processing::ColorParams colorParams;
    cv::Mat pipe = color_processing::processColors(frame, camIdx, false, colorParams);

    const cv::Point2f bull(calib.bullCenter);
    std::vector<double> rawIn, rawOut, pipeIn, pipeOut, fitIn, fitOut, dblIn;
    for (int deg = 0; deg < 360; deg += 3)
    {
        const double th = deg * CV_PI / 180.0;
        const cv::Point2f u((float)cos(th), (float)sin(th));
        const double edge = reachTo(bull, u, E.outerDoubleEllipse);
        if (edge <= 0) continue;

        // The treble window: past the 25 ring, short of the doubles band.
        Band raw = bandAlongRay(bull, u, edge, 0.30, 0.80, hsv, rawRedGreen);
        Band pp  = bandAlongRay(bull, u, edge, 0.30, 0.80, pipe, pipeRedGreen);
        const double fi = reachTo(bull, u, E.innerTripleEllipse) / edge;
        const double fo = reachTo(bull, u, E.outerTripleEllipse) / edge;
        const double di = reachTo(bull, u, E.innerDoubleEllipse) / edge;

        std::cout << "I1499RAY clip=" << clip << " cam=" << camIdx << " deg=" << deg
                  << " rawIn=" << fmt(raw.in) << " rawOut=" << fmt(raw.out)
                  << " pipeIn=" << fmt(pp.in) << " pipeOut=" << fmt(pp.out)
                  << " fitIn=" << fmt(fi) << " fitOut=" << fmt(fo)
                  << " dblIn=" << fmt(di) << std::endl;

        if (raw.in > 0) { rawIn.push_back(raw.in); rawOut.push_back(raw.out); }
        if (pp.in > 0) { pipeIn.push_back(pp.in); pipeOut.push_back(pp.out); }
        if (fi > 0) fitIn.push_back(fi);
        if (fo > 0) fitOut.push_back(fo);
        if (di > 0) dblIn.push_back(di);
    }

    std::cout << "I1499 clip=" << clip << " cam=" << camIdx << " ok=1"
              << " outerD=" << fmt(0.5 * std::max(E.outerDoubleEllipse.size.width, E.outerDoubleEllipse.size.height))
              << " rawIn=" << fmt(median(rawIn)) << " rawOut=" << fmt(median(rawOut))
              << " pipeIn=" << fmt(median(pipeIn)) << " pipeOut=" << fmt(median(pipeOut))
              << " fitIn=" << fmt(median(fitIn)) << " fitOut=" << fmt(median(fitOut))
              << " dblIn=" << fmt(median(dblIn))
              << " rays=" << rawIn.size()
              << " spec_innerT=0.5824 spec_outerT=0.6294" << std::endl;

    if (!annotate.empty())
    {
        cv::Mat vis = frame.clone();
        auto scaled = [&](double f)
        {
            cv::RotatedRect e = E.outerDoubleEllipse;
            e.size.width *= (float)f; e.size.height *= (float)f;
            return e;
        };
        if (E.innerTripleEllipse.size.area() > 0) cv::ellipse(vis, E.innerTripleEllipse, cv::Scalar(255, 0, 255), 2);
        if (E.outerTripleEllipse.size.area() > 0) cv::ellipse(vis, E.outerTripleEllipse, cv::Scalar(255, 0, 255), 2);
        cv::ellipse(vis, scaled(99.0 / 170.0), cv::Scalar(0, 255, 0), 1);
        cv::ellipse(vis, scaled(107.0 / 170.0), cv::Scalar(0, 255, 0), 1);
        cv::ellipse(vis, E.outerDoubleEllipse, cv::Scalar(0, 255, 255), 1);
        cv::imwrite(annotate, vis);
    }
    return 0;
}
