// #1423 EXPLORATORY: what the largest coloured region actually looks like, radially.
//
//   i1423_ring_census <clip> <camera-index> [frames] [spacing]
//
// Prints, per look, the span measureBoard would take and a radial ink profile of the
// colour mask about that span's centre, so the identity of the measured ring can be read
// off rather than guessed at.
#include <opencv2/opencv.hpp>
#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <cmath>

#include "color_processing.hpp"
#include "bull_processing.hpp"

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

        // raw (unblurred) mask, for widths the 7x7 has not inflated
        cv::Mat rawGray, rawBin;
        cv::cvtColor(colours, rawGray, cv::COLOR_BGR2GRAY);
        cv::threshold(rawGray, rawBin, 1, 255, cv::THRESH_BINARY);

        // radial profile: share of rays with ink at each 0.05-span bin, and the outermost
        // ink run's width per ray
        const int rays = 720;
        const int bins = 44; // 0.00 .. 2.20 span
        std::vector<int> hit(bins, 0);
        std::vector<double> widths;
        std::vector<double> outerEdge;
        std::vector<double> reach;
        for (int k = 0; k < rays; k++)
        {
            const double th = 2.0 * kPi * k / rays;
            const double cx = std::cos(th), cy = std::sin(th);
            // bin occupancy
            for (int b = 0; b < bins; b++)
            {
                const double r = (b + 0.5) * 0.05 * span;
                // sample a few points across the bin
                bool any = false;
                for (double d = -0.02; d <= 0.02 && !any; d += 0.01)
                {
                    int x = cvRound(c.x + cx * (r + d * span)), y = cvRound(c.y + cy * (r + d * span));
                    if (x < 0 || y < 0 || x >= rawBin.cols || y >= rawBin.rows) continue;
                    if (rawBin.at<uchar>(y, x)) any = true;
                }
                if (any) hit[b]++;
            }
            // outermost ink run on this ray, searched inward from 1.15 span
            int r1 = -1, r0 = -1;
            for (int r = cvRound(span * 1.05); r >= 1; r--)
            {
                int x = cvRound(c.x + cx * r), y = cvRound(c.y + cy * r);
                bool ink = (x >= 0 && y >= 0 && x < rawBin.cols && y < rawBin.rows) && rawBin.at<uchar>(y, x);
                if (ink && r1 < 0) { r1 = r; }
                else if (!ink && r1 >= 0) { r0 = r + 1; break; }
            }
            if (r1 > 0 && r0 > 0) { widths.push_back(r1 - r0 + 1); outerEdge.push_back(r1); }
            for (int r = cvRound(span * 2.2); r >= 1; r--)
            {
                int x = cvRound(c.x + cx * r), y = cvRound(c.y + cy * r);
                if (x < 0 || y < 0 || x >= rawBin.cols || y >= rawBin.rows) continue;
                if (rawBin.at<uchar>(y, x)) { reach.push_back(r / (double)span); break; }
            }
        }

        std::cout << "I1423 clip=" << clip << " cam=" << camIdx << " look=" << look
                  << " span=" << cvRound(span)
                  << " area=" << (long)bestArea
                  << " rays=" << widths.size()
                  << " medWidth=" << medianOf(widths)
                  << " medWidthOfSpan=" << (span > 0 ? medianOf(widths) / span : -1)
                  << " medOuterOfSpan=" << (span > 0 ? medianOf(outerEdge) / span : -1)
                  << " reachRays=" << reach.size()
                  << " reachP50=" << pct(reach, 0.50)
                  << " reachP75=" << pct(reach, 0.75)
                  << " reachP90=" << pct(reach, 0.90)
                  << " reachMax=" << pct(reach, 1.00)
                  << "\n";
        std::cout << "I1423PROF clip=" << clip << " cam=" << camIdx << " look=" << look << " ";
        for (int b = 0; b < bins; b++)
        {
            std::cout << (int)std::lround(100.0 * hit[b] / rays) << (b + 1 < bins ? "," : "\n");
        }

        for (int s = 1; s < spacing; s++) { cv::Mat junk; if (!cap.read(junk)) break; }
    }
    return 0;
}
