// #1423: what ring each camera's span landed on, measured rather than assumed.
//
//   i1423_ring_census <clip> <camera-index> [looks] [spacing]
//
// Runs the pipeline's own STEP 1 -- color_processing::processColors on the full frame,
// then bull_processing::measureBoard's blur, threshold and largest outermost contour --
// and then asks ring_identity::identify what that span is. The verdict is READ from the
// module rather than restated here, so a tree whose rule has moved prints the moved rule.
//
// It also prints the measured ring's own WIDTH in spans on every row. That is the method
// #1423 was steered towards and refused on a measurement (a doubles ring is 8 mm of 170
// and a treble 8 mm of 107, 0.047 against 0.075), and a method refused on a measurement
// stays refused only for as long as the measurement can be re-taken.
//
// One line per look on stdout, prefixed so it survives the pipeline's own logging:
//
//   I1423 clip=<name> cam=<n> look=<k> span=<px> ring=<unknown|doubles|trebles>
//         reach=<spans> rays=<answered>/<asked> widthOfSpan=<f> boardRadius=<px>
//
#include <opencv2/opencv.hpp>
#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <cmath>

#include "color_processing.hpp"
#include "bull_processing.hpp"
#include "ring_identity.hpp"

namespace
{
    inline constexpr double kPi = 3.14159265358979323846;

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

    double pct(std::vector<double> v, double q)
    {
        if (v.empty()) return -1.0;
        std::sort(v.begin(), v.end());
        size_t i = (size_t)std::lround(q * (v.size() - 1));
        return v[i];
    }

    double medianOf(std::vector<double> v)
    {
        if (v.empty()) return -1.0;
        std::sort(v.begin(), v.end());
        return v[v.size() / 2];
    }
}

int main(int argc, char **argv)
{
    if (argc < 3) { std::cerr << "usage: i1423_ring_census <clip> <cam-index> [looks] [spacing]\n"; return 2; }
    const std::string clip = argv[1];
    const int camIdx = std::atoi(argv[2]);
    const int looks = (argc > 3) ? std::atoi(argv[3]) : 1;
    const int spacing = (argc > 4) ? std::atoi(argv[4]) : 30;

    cv::VideoCapture cap(clip);
    if (!cap.isOpened()) { std::cerr << "cannot open " << clip << "\n"; return 2; }
    const double fps = cap.get(cv::CAP_PROP_FPS);
    cap.set(cv::CAP_PROP_POS_FRAMES, seekFrameFor(camIdx, fps));

    for (int look = 0; look < looks; look++)
    {
        cv::Mat frame;
        if (!averageOf(cap, 30, frame)) break;

        cv::Mat colours = color_processing::processColors(frame, camIdx, false, color_processing::ColorParams());

        // measureBoard's own first three steps
        cv::Mat blurred, gray, binary;
        cv::GaussianBlur(colours, blurred, cv::Size(7, 7), 2.0);
        cv::cvtColor(blurred, gray, cv::COLOR_BGR2GRAY);
        cv::threshold(gray, binary, 1, 255, cv::THRESH_BINARY);

        std::vector<std::vector<cv::Point>> contours; std::vector<cv::Vec4i> hier;
        cv::findContours(binary, contours, hier, cv::RETR_TREE, cv::CHAIN_APPROX_SIMPLE);

        int best = -1; double bestArea = 0;
        for (size_t i = 0; i < contours.size(); i++)
        {
            if (hier[i][3] != -1) continue;
            double a = cv::contourArea(contours[i]);
            if (a > bestArea) { bestArea = a; best = (int)i; }
        }
        if (best < 0) { std::cout << "I1423 clip=" << clip << " cam=" << camIdx << " look=" << look << " NOBOARD\n"; continue; }

        cv::Point2f c; float span = 0;
        cv::minEnclosingCircle(contours[best], c, span);

        // The measured ring's own width, over 720 rays, on the UNBLURRED mask: the
        // refused method, kept measurable.
        cv::Mat rawGray, rawBin;
        cv::cvtColor(colours, rawGray, cv::COLOR_BGR2GRAY);
        cv::threshold(rawGray, rawBin, 1, 255, cv::THRESH_BINARY);

        const int rays = 720;
        std::vector<double> widths;
        for (int k = 0; k < rays; k++)
        {
            const double th = 2.0 * kPi * k / rays;
            const double cx = std::cos(th), cy = std::sin(th);
            int r1 = -1, r0 = -1;
            for (int r = cvRound(span * 1.05); r >= 1; r--)
            {
                int x = cvRound(c.x + cx * r), y = cvRound(c.y + cy * r);
                bool ink = (x >= 0 && y >= 0 && x < rawBin.cols && y < rawBin.rows) && rawBin.at<uchar>(y, x);
                if (ink && r1 < 0) { r1 = r; }
                else if (!ink && r1 >= 0) { r0 = r + 1; break; }
            }
            if (r1 > 0 && r0 > 0) { widths.push_back(r1 - r0 + 1); }
        }

        const ring_identity::Sighting ring = ring_identity::identify(colours, c, span);
        const char *name = (ring.ring == ring_identity::Ring::Doubles)   ? "doubles"
                           : (ring.ring == ring_identity::Ring::Trebles) ? "trebles"
                                                                        : "unknown";

        std::cout << "I1423 clip=" << clip << " cam=" << camIdx << " look=" << look
                  << " span=" << cvRound(span)
                  << " ring=" << name
                  << " reach=" << ring.reach
                  << " rays=" << ring.rays_answered << "/" << ring.rays_asked
                  << " widthOfSpan=" << (span > 0 ? medianOf(widths) / span : -1)
                  << " boardRadius=" << (span * ring.boardRadiusOfSpan())
                  << "\n";
        std::cout << "I1423SAY clip=" << clip << " cam=" << camIdx << " look=" << look
                  << " " << ring_identity::sentence(ring) << "\n";

        for (int s = 1; s < spacing; s++) { cv::Mat junk; if (!cap.read(junk)) break; }
    }
    return 0;
}
