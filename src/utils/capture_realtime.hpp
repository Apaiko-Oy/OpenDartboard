#pragma once
// turnaus#1683: a video file played as a live camera plays -- the REAL-TIME REPLAY.
//
// A file source hands over the next frame whenever it is asked, however long the loop
// took since the last one, so a replay never drops a frame and never waits for one. A
// camera does neither. It produces frames at its own rate whether or not anybody reads
// them, and a read hands over the NEWEST frame it has (OpenCV's Media Foundation reader
// keeps one sample and overwrites it, which is the live Windows rig, docs/rig.md), or
// waits for the next one if the newest was already taken. A loop slower than 33.3 ms
// therefore skips frames on a rig and skips none on a file.
//
// RealtimeFeed is that camera, made of a file. One thread per file decodes frame after
// frame and publishes each at its presentation time, measured on the wall clock from
// the instant every feed of the source was started together. The loop's read() takes
// the newest published frame, blocking only when it has already taken it. So:
//
//   - a frame is never available before its presentation time;
//   - a slow cycle skips the frames that were published and overwritten meanwhile,
//     and the next read gets the newest, as a live camera does;
//   - decoding a skipped frame costs the feed's thread, not the loop (a camera's
//     sensor does not charge the detector either).
//
// What it does NOT reproduce, said here rather than discovered: the camera's own
// exposure and noise are the recording's, not a fresh one; a V4L2 device (Linux) queues
// up to four buffers and hands over the OLDEST when the loop is slow, not the newest;
// and h264 decoding runs on this machine's cores, which the loop shares.
//
// Off unless OD_REALTIME_REPLAY=on. Only file sources are ever fed this way.

#include <opencv2/opencv.hpp>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <mutex>
#include <string>
#include <thread>
#include "cycle_cost.hpp"

namespace camera
{
    inline bool realtimeReplayOn()
    {
        static const bool on = []
        {
            const char *v = std::getenv("OD_REALTIME_REPLAY");
            return v && std::string(v) == "on";
        }();
        return on;
    }

    class RealtimeFeed
    {
    public:
        struct Taken
        {
            bool ok = false;
            cv::Mat image;
            double pos_ms = -1.0;
            long frame = -1;   // the file's frame index of what was handed over
            long skipped = 0;  // frames published since the last read and never handed over
        };

        /** `cap` is taken over: from here only the feed's thread touches it.
         *  `origin` is the wall instant at which presentation time `origin_pts_ms`
         *  of this file is due; every feed of one source shares both. */
        RealtimeFeed(cv::VideoCapture cap, std::chrono::steady_clock::time_point origin, double origin_pts_ms)
            : cap_(cap), origin_(origin), origin_pts_ms_(origin_pts_ms)
        {
            thread_ = std::thread([this]
                                  { run(); });
        }

        ~RealtimeFeed()
        {
            {
                std::lock_guard<std::mutex> lock(m_);
                stop_ = true;
            }
            cv_.notify_all();
            if (thread_.joinable())
                thread_.join();
        }

        RealtimeFeed(const RealtimeFeed &) = delete;
        RealtimeFeed &operator=(const RealtimeFeed &) = delete;

        /** The newest frame, waiting for the next one if the newest was already taken. */
        Taken take()
        {
            Taken t;
            std::unique_lock<std::mutex> lock(m_);
            cv_.wait(lock, [this]
                     { return stop_ || eof_ || seq_ > taken_seq_; });
            if (seq_ <= taken_seq_)
                return t; // ended (or stopping) with nothing new
            t.ok = true;
            t.image = newest_;
            t.pos_ms = newest_pos_ms_;
            t.frame = newest_frame_;
            t.skipped = seq_ - taken_seq_ - 1;
            taken_seq_ = seq_;
            return t;
        }

        bool ended()
        {
            std::lock_guard<std::mutex> lock(m_);
            return eof_ && seq_ <= taken_seq_;
        }

    private:
        void run()
        {
            long index = (long)cap_.get(cv::CAP_PROP_POS_FRAMES);
            for (;;)
            {
                cv::Mat image; // a fresh buffer every frame: the one published is shared
                const double decode_t0 = cycle_cost::on() ? cycle_cost::nowMs() : 0.0;
                const bool ok = cap_.read(image) && !image.empty();
                if (cycle_cost::on())
                    cycle_cost::offloopDecodeUs() += (long long)((cycle_cost::nowMs() - decode_t0) * 1000.0);
                const double pos_ms = ok ? cap_.get(cv::CAP_PROP_POS_MSEC) : -1.0;
                if (!ok)
                {
                    std::lock_guard<std::mutex> lock(m_);
                    eof_ = true;
                    cv_.notify_all();
                    return;
                }
                const auto due = origin_ + std::chrono::microseconds(
                                               (long long)((pos_ms - origin_pts_ms_) * 1000.0));
                {
                    std::unique_lock<std::mutex> lock(m_);
                    // sleeping on the condition so a stop is not kept waiting a frame
                    cv_.wait_until(lock, due, [this]
                                   { return stop_; });
                    if (stop_)
                        return;
                    newest_ = image;
                    newest_pos_ms_ = pos_ms;
                    newest_frame_ = index;
                    seq_++;
                }
                cv_.notify_all();
                index++;
            }
        }

        cv::VideoCapture cap_;
        const std::chrono::steady_clock::time_point origin_;
        const double origin_pts_ms_;
        std::thread thread_;
        std::mutex m_;
        std::condition_variable cv_;
        bool stop_ = false;
        bool eof_ = false;
        long seq_ = 0;
        long taken_seq_ = 0;
        cv::Mat newest_;
        double newest_pos_ms_ = -1.0;
        long newest_frame_ = -1;
    };
}
