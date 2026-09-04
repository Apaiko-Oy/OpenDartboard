#pragma once

#include "capture.hpp"
#include "logging.hpp"
#include "od_clock.hpp"
#include "od_fix.hpp"
#include "skew_recorder.hpp"

#include <opencv2/opencv.hpp>
#include <chrono>
#include <cstdlib>
#include <sstream>
#include <thread>

namespace camera
{
    inline int odEnvInt(const char *name, int fallback)
    {
        const char *v = std::getenv(name);
        return (v && *v) ? std::atoi(v) : fallback;
    }

    // The OpenCV-backed capture source. On Linux it opens devices with CAP_V4L2 and
    // negotiates MJPEG; a path that looks like a video file is opened with OpenCV's
    // default backend. A Windows implementation is a sibling of this class and nothing
    // above the seam changes when it arrives.
    class OpenCvCaptureSource : public CaptureSource
    {
    public:
        bool open(const std::vector<std::string> &sources, int width, int height, int fps) override
        {
            // Instrument (#813): what CAP_PROP_POS_MSEC will mean for these sources.
            skew::clockKind() = (!sources.empty() && isVideoFile(sources[0])) ? "stream" : "host";
            captures_.clear();
            clocks_.clear();
            anchors_.clear();
            descriptions_.clear();
            nominal_fps_ = static_cast<double>(fps);

            log_info("Initializing " + log_string(sources.size()) + " cameras...");

            for (size_t i = 0; i < sources.size(); i++)
            {
                cv::VideoCapture cap;
                log_debug("Opening camera " + log_string(i + 1) + ": " + sources[i]);

                CaptureClock clock = CaptureClock::Unknown;

                if (isVideoFile(sources[i]))
                {
                    log_debug("Detected video file: " + sources[i]);
                    cap.open(sources[i]);

                    // #815: the frame period the spike window is converted with, taken
                    // from the stream itself rather than from the --fps flag, when asked.
                    if (od_fix::fpsFromStream() && cap.isOpened())
                    {
                        double stream_fps = cap.get(cv::CAP_PROP_FPS);
                        if (stream_fps > 0)
                        {
                            od_clock::frame_period_ms() = 1000.0 / stream_fps;
                            log_info("FRAME PERIOD from stream: " + log_string(stream_fps) + " fps -> " + log_string(od_clock::frame_period_ms()) + " ms");
                        }
                    }
                    clock = CaptureClock::StreamPosition;

#ifdef DEBUG_SEEK_VIDEO
                    double seek_seconds = 3 - (i * 0.18); // Example: seek 4 seconds for first video, 3 for second, etc.
                    if (seek_seconds > 0 && cap.isOpened())
                    {
                        double video_fps = cap.get(cv::CAP_PROP_FPS);
                        if (video_fps > 0)
                        {
                            int target_frame = static_cast<int>(video_fps * seek_seconds);
                            cap.set(cv::CAP_PROP_POS_FRAMES, target_frame);
                            log_debug("Seeked video " + log_string(i + 1) + " forward by " + log_string(seek_seconds) + " seconds (frame " + log_string(target_frame) + ")");
                        }
                        else
                        {
                            log_warning("Could not determine FPS for video " + sources[i] + ", skipping seek");
                        }
                    }
#endif
                }
                else
                {
                    cap.open(sources[i], cv::CAP_V4L2);
                    cap.set(cv::CAP_PROP_FRAME_WIDTH, width);
                    cap.set(cv::CAP_PROP_FRAME_HEIGHT, height);
                    cap.set(cv::CAP_PROP_FPS, fps);
                    // Set MJPEG codec for better performance
                    int fourcc = cv::VideoWriter::fourcc('M', 'J', 'P', 'G'); // MJPEG codec
                    cap.set(cv::CAP_PROP_FOURCC, fourcc);                     // Set MJPEG codec

                    // V4L2 hands back the dequeued buffer's timestamp, which is the host's own
                    // clock and is therefore shared by every device on this machine.
                    clock = CaptureClock::HostMonotonic;

                    log_debug("Opened camera " + log_string(i + 1) + " at " + log_string(width) + "x" + log_string(height) + " @ " + log_string(fps) + " FPS" +
                              " (FOURCC: " + log_string_src(decodeFourCC(fourcc)) + ")");
                }

                if (!cap.isOpened())
                {
                    log_error("Failed to open camera/video " + sources[i]);
                    return false;
                }

                // Verify camera properties after opening
                if (!isVideoFile(sources[i]))
                {
                    double actual_width = cap.get(cv::CAP_PROP_FRAME_WIDTH);
                    double actual_height = cap.get(cv::CAP_PROP_FRAME_HEIGHT);
                    double actual_fps = cap.get(cv::CAP_PROP_FPS);
                    double fourcc = cap.get(cv::CAP_PROP_FOURCC);

                    log_debug("Camera " + log_string(i + 1) + " verification:");
                    log_debug("  Resolution: " + log_string((int)actual_width) + "x" + log_string((int)actual_height) + " (expected: " + log_string(width) + "x" + log_string(height) + ")");
                    log_debug("  FPS: " + log_string((int)actual_fps) + " (expected: " + log_string(fps) + ")");
                    log_debug("  FOURCC: " + log_string_src(decodeFourCC(fourcc)) + " (expected: " + log_string_src(decodeFourCC(cv::VideoWriter::fourcc('M', 'J', 'P', 'G'))) + ")");
                    log_debug("  Backend: " + log_string_src(cap.getBackendName()) + " (expected: " + log_string_src((std::string) "V4L2") + ")");

                    if (actual_fps > 0)
                        nominal_fps_ = actual_fps;
                }
                else if (cap.get(cv::CAP_PROP_FPS) > 0)
                {
                    nominal_fps_ = cap.get(cv::CAP_PROP_FPS);
                }

                descriptions_.push_back(sources[i] + " (" + cap.getBackendName() + ")");
                captures_.push_back(cap);
                clocks_.push_back(clock);
                anchors_.push_back(0);
                log_info("Camera/video " + log_string(i + 1) + " initialized successfully");
            }

            return !captures_.empty();
        }

        size_t size() const override { return captures_.size(); }

        double nominalFps() const override { return nominal_fps_; }

        std::string describe(size_t i) const override
        {
            return i < descriptions_.size() ? descriptions_[i] : std::string("");
        }

        std::vector<Frame> read() override
        {
            // ---- #798 instrumentation: fault injection + drop reporting. NOT a fix. ----
            // It forces one named camera's read to fail on a schedule, so that a marked
            // slot can be produced on footage that never drops one. It moved here with
            // the seam; it used to live in captureFrames.
            static long od_cycle = 0;
            static const int od_drop_cam = odEnvInt("OD_DROP_CAM", -1);
            static const int od_drop_every = odEnvInt("OD_DROP_EVERY", 0);
            static const int od_report_every = odEnvInt("OD_REPORT_EVERY", 0);
            static long od_short_cycles = 0;
            od_cycle++;
            const bool od_inject = (od_drop_every > 0 && (od_cycle % od_drop_every) == 0);

            std::vector<Frame> frames(captures_.size());

            for (size_t i = 0; i < captures_.size(); i++)
            {
                frames[i].index = static_cast<int>(i);
                frames[i].clock = clocks_[i];

                cv::Mat image;
                bool success = captures_[i].read(image);

                if (od_inject && static_cast<int>(i) == od_drop_cam)
                {
                    success = false;
                    image.release();
                }

                // The acquisition instant, taken from the backend, immediately after the
                // frame is in hand. This is the camera's own clock, not the loop's.
                double pos_ms = captures_[i].get(cv::CAP_PROP_POS_MSEC);
                int64_t now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                     std::chrono::steady_clock::now().time_since_epoch())
                                     .count();

                if (success && !image.empty())
                {
                    // The first frame anchors a per-source clock against the host's, which is
                    // what a source-relative backend needs and a host-monotonic one does not.
                    if (anchors_[i] == 0)
                        anchors_[i] = now_ns;

                    frames[i].image = image;
                    frames[i].valid = true;
                    frames[i].pos_ms = pos_ms;
                    frames[i].anchor_ns = anchors_[i];
                    frames[i].returned_ns = now_ns;
                }
                else
                {
                    // The slot stays, marked, so that a camera's position never changes.
                    log_error("Failed to capture frame from camera " + log_string(i + 1) + " - slot marked unavailable");
                    frames[i].pos_ms = pos_ms;
                    frames[i].returned_ns = now_ns;
                }
            }

            // #798's drop report: a cycle that lost a slot says so, by camera and by
            // stream position, because nothing else in the program ever did.
            {
                std::string pos;
                for (size_t i = 0; i < frames.size(); i++)
                    pos += (i ? "," : "") + std::to_string((long)frames[i].pos_ms);
                const size_t have = validCount(frames);
                if (have != captures_.size())
                {
                    od_short_cycles++;
                    log_error("CAPDROP cycle=" + std::to_string(od_cycle) + " returned=" + std::to_string(have) + "/" + std::to_string(captures_.size()) + " pos_ms=[" + pos + "]");
                }
                if (od_report_every > 0 && (od_cycle % od_report_every) == 0)
                {
                    log_info("CAPSTAT cycle=" + std::to_string(od_cycle) + " returned=" + std::to_string(have) + "/" + std::to_string(captures_.size()) + " short_so_far=" + std::to_string(od_short_cycles) + " pos_ms=[" + pos + "]");
                }
            }

            // #811: the acquisition clock the motion state machine may be judged on.
            // The newest position this cycle carried, monotonically.
            for (size_t i = 0; i < frames.size(); i++)
            {
                if (frames[i].empty())
                    continue;
                long long pos = (long long)frames[i].pos_ms;
                if (pos > od_clock::capture_ms().load())
                    od_clock::capture_ms().store(pos);
            }
            od_clock::cycles().fetch_add(1);

            // Instrument (#813): one line per capture cycle, when OD_SKEW_LOG names a
            // file. The three instants come off the Frame the seam already fills in.
            {
                std::vector<double> skew_pos_ms(frames.size(), -1.0);
                std::vector<long long> skew_anchor_us(frames.size(), -1);
                std::vector<long long> skew_ret_us(frames.size(), -1);
                for (size_t i = 0; i < frames.size(); i++)
                {
                    skew_pos_ms[i] = frames[i].empty() ? -1.0 : frames[i].pos_ms;
                    skew_anchor_us[i] = frames[i].anchor_ns / 1000;
                    skew_ret_us[i] = frames[i].returned_ns / 1000;
                }
                skew::record(frames.size(), validCount(frames), skew_pos_ms, skew_anchor_us, skew_ret_us);
            }

            reportCycle(frames);

#ifdef DEBUG_VIA_VIDEO_INPUT
            std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(1000.0 / 60)));
#endif

            return frames;
        }

        std::vector<Frame> readAveraged(int numFrames) override
        {
            // Instrument (#813): calibration averaging is not a scoring cycle.
            struct SkewSuppress
            {
                SkewSuppress() { skew::suppressed() = true; }
                ~SkewSuppress() { skew::suppressed() = false; }
            } skew_suppress;

            std::vector<cv::Mat> sums(captures_.size());
            std::vector<int> counts(captures_.size(), 0);
            std::vector<Frame> out(captures_.size());

            for (size_t i = 0; i < captures_.size(); i++)
            {
                out[i].index = static_cast<int>(i);
                out[i].clock = clocks_[i];
            }

            for (int n = 0; n < numFrames; n++)
            {
                std::vector<Frame> cycle = read();
                for (size_t i = 0; i < cycle.size() && i < sums.size(); i++)
                {
                    if (cycle[i].empty())
                        continue;

                    cv::Mat as_float;
                    cycle[i].image.convertTo(as_float, CV_32F);

                    if (counts[i] == 0)
                        sums[i] = as_float;
                    else
                        sums[i] += as_float;

                    counts[i]++;
                    out[i].pos_ms = cycle[i].pos_ms;
                    out[i].anchor_ns = cycle[i].anchor_ns;
                    out[i].returned_ns = cycle[i].returned_ns;
                }
            }

            for (size_t i = 0; i < sums.size(); i++)
            {
                if (counts[i] == 0)
                    continue;

                // Divided by what this camera contributed, not by how many cycles were asked for.
                cv::Mat averaged = sums[i] / static_cast<float>(counts[i]);
                averaged.convertTo(out[i].image, CV_8U);
                out[i].valid = true;
            }

            return out;
        }

    private:
        // Instrumentation, off unless OD_CAPSEAM names a reporting interval. It prints what
        // the seam now knows and the program did not: per camera the backend's acquisition
        // instant, and separately when each read() returned.
        void reportCycle(const std::vector<Frame> &frames)
        {
            static long every = -1;
            static long cycle = 0;
            if (every < 0)
            {
                const char *v = getenv("OD_CAPSEAM");
                every = v ? atol(v) : 0;
            }
            cycle++;
            if (every <= 0 || (cycle % every) != 0)
                return;

            double newest = -1e18, oldest = 1e18;
            int64_t ret_newest = INT64_MIN, ret_oldest = INT64_MAX;
            std::ostringstream pos, ret;
            pos << "[";
            ret << "[";
            for (size_t i = 0; i < frames.size(); i++)
            {
                if (i)
                {
                    pos << ",";
                    ret << ",";
                }
                if (frames[i].empty())
                {
                    pos << "-";
                    ret << "-";
                    continue;
                }
                pos << (long long)frames[i].pos_ms;
                ret << (long long)((frames[i].returned_ns - frames[i].anchor_ns) / 1000);
                newest = std::max(newest, frames[i].pos_ms);
                oldest = std::min(oldest, frames[i].pos_ms);
                ret_newest = std::max(ret_newest, frames[i].returned_ns);
                ret_oldest = std::min(ret_oldest, frames[i].returned_ns);
            }
            pos << "]";
            ret << "]";

            std::ostringstream line;
            line << "CAPSEAM iter=" << cycle
                 << " valid=" << validCount(frames) << "/" << frames.size()
                 << " clock=" << clockName(frames.empty() ? CaptureClock::Unknown : frames[0].clock)
                 << " pos_ms=" << pos.str();
            if (newest > -1e17)
                line << " pos_spread_ms=" << (long long)(newest - oldest);
            line << " ret_us=" << ret.str();
            if (ret_newest != INT64_MIN)
                line << " read_spread_us=" << (long long)((ret_newest - ret_oldest) / 1000);

            log_info(line.str());
        }

        static const char *clockName(CaptureClock c)
        {
            switch (c)
            {
            case CaptureClock::HostMonotonic:
                return "host";
            case CaptureClock::StreamPosition:
                return "stream";
            case CaptureClock::SourceRelative:
                return "source";
            default:
                return "unknown";
            }
        }

        std::vector<cv::VideoCapture> captures_;
        std::vector<CaptureClock> clocks_;
        std::vector<int64_t> anchors_;
        std::vector<std::string> descriptions_;
        double nominal_fps_ = 0.0;
    };

    inline std::unique_ptr<CaptureSource> makeCaptureSource()
    {
        return std::unique_ptr<CaptureSource>(new OpenCvCaptureSource());
    }
}
