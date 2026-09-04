#pragma once

#include <opencv2/opencv.hpp>
#include <vector>
#include <cstdint>

namespace camera
{
    // What the number in Frame::pos_ms is on. Backend knowledge, held at the seam
    // so that nothing above has to know which platform produced the frame.
    enum class CaptureClock
    {
        Unknown,        // no instant was available
        HostMonotonic,  // V4L2: the dequeued buffer's timestamp, shared by every device on the host
        StreamPosition, // a file: position within the stream, not a wall clock at all
        SourceRelative  // MSMF: the source's own clock, near zero per stream; needs anchor_ns
    };

    // One camera's slot for one capture cycle. A slot always exists for every camera;
    // a camera that did not produce a frame leaves valid == false and an empty image.
    struct Frame
    {
        cv::Mat image;
        bool valid = false;
        int index = -1;                                 // which camera this slot is, always
        double pos_ms = -1.0;                           // CAP_PROP_POS_MSEC as the backend reported it
        CaptureClock clock = CaptureClock::Unknown;     // what pos_ms means
        int64_t anchor_ns = 0;                          // host steady_clock instant at which this camera's clock reads zero
        int64_t returned_ns = 0;                        // host steady_clock when read() returned: decode, not capture

        bool empty() const { return !valid || image.empty(); }
    };

    // How many slots actually hold a frame.
    inline size_t validCount(const std::vector<Frame> &frames)
    {
        size_t n = 0;
        for (const auto &f : frames)
        {
            if (!f.empty())
                n++;
        }
        return n;
    }

    // The images, in the same positions, so a stage that has not been converted yet
    // still reads position as camera identity.
    inline std::vector<cv::Mat> images(const std::vector<Frame> &frames)
    {
        std::vector<cv::Mat> out;
        out.reserve(frames.size());
        for (const auto &f : frames)
            out.push_back(f.empty() ? cv::Mat() : f.image);
        return out;
    }

    // The newest acquisition instant in the cycle, in microseconds on that cycle's
    // capture clock, or 0 if no slot carried one.
    inline uint64_t newestInstantUs(const std::vector<Frame> &frames)
    {
        double newest = -1.0;
        for (const auto &f : frames)
        {
            if (!f.empty() && f.pos_ms >= 0.0 && f.pos_ms > newest)
                newest = f.pos_ms;
        }
        return newest < 0.0 ? 0ULL : static_cast<uint64_t>(newest * 1000.0);
    }
}
