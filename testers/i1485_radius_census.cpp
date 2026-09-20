// #1485: what radius is a dart judged against, and which ring ellipse decides it.
//
//   i1485_radius_census <clip> <camera-index> [looks] [spacing]
//
// Runs the pipeline's own calibration -- geometry_calibration::calibrateSingleCamera on
// an averaged frame, the same frame a detector run calibrates on -- and then reads the
// SIX RING ELLIPSES the scorer divides a dart by, as multiples of the ray-traced doubles
// ring. A dartboard's own millimetres fix those six numbers exactly:
//
//     6.35/170 = 0.037   15.9/170 = 0.094   99/170 = 0.582
//     107/170  = 0.629   162/170  = 0.953   170/170 = 1.000
//
// so a ring ellipse's departure from its own number is a measurement and not a fit. The
// doubles ring is the reference because it is the one thing on this calibration that is
// ray-traced from the bull outwards and cross-checked by three other issues (#1393's
// ~316 px fit, #1378's 1.63 spans, #1423's reach): every other ring here is fitted to a
// colour CONTOUR and can be fitted to the wrong contour without anything noticing.
//
// It then scores synthetic tips at known true radii through `score_processing::scorePoint`
// -- the shipped decision, not a restatement of it -- and prints what each one published.
//
// One line per look on stdout, prefixed so it survives the pipeline's own logging:
//
//   I1485 clip=<name> cam=<n> look=<k> ok=<0|1> outerD=<px>
//         innerBull=<x> outerBull=<x> innerT=<x> outerT=<x> innerD=<x>   (spans of outerD)
//   I1485RING clip=<name> cam=<n> look=<k> f=<true radius> got=<score> ring=<ring> r=<radius>
//
#include <opencv2/opencv.hpp>
#include <iostream>
#include <string>
#include <vector>
#include <cmath>

#include "geometry_calibration.hpp"
#include "detector/geometry/detection/score_processing.hpp"

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

    // Distance from `origin` along unit `dir` to the boundary of `e`, negative where the
    // ray never crosses it. The same arithmetic score_processing's ruler uses.
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

    std::string fmt(double v)
    {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.4f", v);
        return buf;
    }
}

int main(int argc, char **argv)
{
    if (argc < 3) { std::cerr << "usage: i1485_radius_census <clip> <cam-index> [looks] [spacing]\n"; return 2; }
    const std::string clip = argv[1];
    const int camIdx = std::atoi(argv[2]);
    const int looks = (argc > 3) ? std::atoi(argv[3]) : 1;
    const int spacing = (argc > 4) ? std::atoi(argv[4]) : 30;

    cv::VideoCapture cap(clip);
    if (!cap.isOpened()) { std::cerr << "cannot open " << clip << "\n"; return 2; }
    const double fps = cap.get(cv::CAP_PROP_FPS);
    cap.set(cv::CAP_PROP_POS_FRAMES, seekFrameFor(camIdx, fps));

    // Eight true radii, named by where they fall on a real board.
    struct Probe { double f; const char *ring; };
    const Probe probes[] = {
        {0.020, "bull"},   {0.070, "outer"},  {0.300, "single"}, {0.500, "single"},
        {0.605, "triple"}, {0.800, "single"}, {0.975, "double"}, {1.150, "off"},
    };

    for (int look = 0; look < looks; look++)
    {
        cv::Mat frame;
        if (!averageOf(cap, 30, frame)) break;

        DartboardCalibration calib = geometry_calibration::calibrateSingleCamera(frame, camIdx, false);
        const auto &E = calib.ellipses;
        const double outerD = 0.5 * std::max(E.outerDoubleEllipse.size.width, E.outerDoubleEllipse.size.height);

        auto span = [&](const cv::RotatedRect &e)
        { return outerD > 0 ? 0.5 * std::max(e.size.width, e.size.height) / outerD : -1.0; };

        std::cout << "I1485 clip=" << clip << " cam=" << camIdx << " look=" << look
                  << " ok=" << (calib.sees_board && E.hasValidDoubles ? 1 : 0)
                  << " outerD=" << fmt(outerD)
                  << " innerBull=" << fmt(span(E.innerBullEllipse))
                  << " outerBull=" << fmt(span(E.outerBullEllipse))
                  << " innerT=" << fmt(span(E.innerTripleEllipse))
                  << " outerT=" << fmt(span(E.outerTripleEllipse))
                  << " innerD=" << fmt(span(E.innerDoubleEllipse))
                  << " bulls=" << (E.hasValidBulls ? 1 : 0)
                  << " triples=" << (E.hasValidTriples ? 1 : 0)
                  << std::endl;

        // The scoring sweep. A tip is placed on a ray from the bull at a known TRUE
        // fraction of the board's radius along that ray, where the board is the
        // ray-traced doubles ellipse, and the shipped scorer is asked what it is.
        if (outerD > 0)
        {
            const cv::Point2f bull(calib.bullCenter);
            for (int deg = 0; deg < 360; deg += 45)
            {
                const double th = deg * CV_PI / 180.0;
                const cv::Point2f u((float)cos(th), (float)sin(th));
                const double edge = reachTo(bull, u, E.outerDoubleEllipse);
                if (edge <= 0) continue;
                for (const Probe &p : probes)
                {
                    const cv::Point2f tip = bull + u * (float)(edge * p.f);
                    score_processing::PointScore s = score_processing::scorePoint(tip, calib);
                    std::cout << "I1485RING clip=" << clip << " cam=" << camIdx
                              << " look=" << look << " deg=" << deg
                              << " f=" << fmt(p.f) << " want=" << p.ring
                              << " got=" << s.score
                              << " ring=" << (s.ring.empty() ? "-" : s.ring)
                              << " r=" << (s.board.has_radius ? fmt(s.board.radius) : std::string("none"))
                              << std::endl;
                }
            }
        }

        for (int i = 1; i < spacing; i++) { cv::Mat skip; if (!cap.read(skip)) break; }
    }
    return 0;
}
