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
    // Which OpenCV backend opens a *device* on this platform, and what the instant it
    // then answers CAP_PROP_POS_MSEC with actually is. Both halves are backend
    // knowledge and this is the only place in the program that holds either.
    //
    //   V4L2  — the dequeued buffer's timestamp: the host's own clock, so the three
    //           cameras' instants are directly comparable and their spread IS the skew.
    //   MSMF  — the Media Foundation sample time: the SOURCE's clock, which starts near
    //           zero per stream. Three of them are three unrelated clocks. Subtracting
    //           them from each other yields a number near zero that looks like an
    //           excellent skew and is not a skew at all. Frame::anchor_ns is what makes
    //           them comparable, and it is filled in below for every backend so that it
    //           cannot be the thing a port forgets.
#ifdef _WIN32
    inline int deviceBackend() { return cv::CAP_MSMF; }
    inline const char *deviceBackendName() { return "MSMF"; }
    inline CaptureClock deviceClock() { return CaptureClock::SourceRelative; }
#else
    inline int deviceBackend() { return cv::CAP_V4L2; }
    inline const char *deviceBackendName() { return "V4L2"; }
    inline CaptureClock deviceClock() { return CaptureClock::HostMonotonic; }
#endif

    // What the skew instrument calls this clock in its report.
    inline const char *clockKindName(CaptureClock c)
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

    // A device source is a V4L2 path on Linux and a device index on Windows, because
    // Media Foundation has no filesystem name for a camera. "0", "/dev/video0" and
    // "video=Iriun Webcam" all arrive here as the string the user typed.
    inline bool deviceIndexOf(const std::string &source, int &index)
    {
        if (source.empty())
            return false;
        for (char c : source)
        {
            if (c < '0' || c > '9')
                return false;
        }
        index = std::atoi(source.c_str());
        return true;
    }

    inline int odEnvInt(const char *name, int fallback)
    {
        const char *v = std::getenv(name);
        return (v && *v) ? std::atoi(v) : fallback;
    }

    // #1319: a Finding carries its own severity, because which of these is a warning
    // IS the finding. One place turns one into a line.
    inline void sayFinding(const Finding &finding)
    {
        if (!finding.said)
            return;
        if (finding.severity == Severity::Warning)
            log_warning(finding.text);
        else
            log_info(finding.text);
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
            // Taken from the backend that is about to be opened rather than guessed
            // from the path, because on Windows a device is neither "stream" nor
            // "host" and a report that says "host" over Media Foundation instants is
            // the exact mistake §5.2 of the Windows study predicts.
            skew::clockKind() = clockKindName(
                (!sources.empty() && isVideoFile(sources[0])) ? CaptureClock::StreamPosition : deviceClock());
            // #1319: what each DEVICE ended up running, filled in as the loop opens them,
            // so the bandwidth claim can be made once at the bottom by something that
            // knows how many there are. A video file never enters it.
            std::vector<OpenedCamera> opened_devices;

            captures_.clear();
            clocks_.clear();
            anchors_.clear();
            anchored_.clear();
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
                    int device_index = 0;
                    if (deviceIndexOf(sources[i], device_index))
                        cap.open(device_index, deviceBackend());
                    else
                        cap.open(sources[i], deviceBackend());

                    // On Media Foundation the negotiation and the open are the same act —
                    // there is no out-of-band VIDIOC_S_FMT — so MJPG is asked for here and
                    // what was actually granted is read back below and logged. Three
                    // 1280x720 cameras do not fit on one bus uncompressed, so a silent
                    // fall back to YUY2 is the bandwidth failure arriving disguised as a
                    // timing one.
                    //
                    // #1319: the FOURCC request is KEPT, and it is kept because of V4L2
                    // rather than because of MSMF. On V4L2 this really is the negotiation
                    // — it becomes a VIDIOC_S_FMT with V4L2_PIX_FMT_MJPEG and it already
                    // works, which is why the Linux board has never had this bug. On MSMF
                    // it is refused (the property means the conversion target there; the
                    // reading of OpenCV's own source is in capture.hpp), so the return
                    // value is kept and the refusal is said by name below.
                    int fourcc = cv::VideoWriter::fourcc('M', 'J', 'P', 'G');
                    const bool fourcc_accepted = cap.set(cv::CAP_PROP_FOURCC, fourcc);
                    cap.set(cv::CAP_PROP_FRAME_WIDTH, width);
                    cap.set(cv::CAP_PROP_FRAME_HEIGHT, height);

                    // #1319: the rate is negotiated rather than set once, because on
                    // MSMF the rate IS the format request — its mode chooser scores
                    // nearest-frame-rate and never looks at the subtype, so asking for
                    // 15 on a module whose modes are `mjpeg @30` and `yuyv422 @10`
                    // picks the uncompressed one by five frames a second. The escalation
                    // fires only when the first request was answered with something
                    // slower, so a camera that is already giving what it was asked for —
                    // every V4L2 device, and any MSMF device whose mode list holds the
                    // requested rate — is asked once and left alone.
                    //
                    // CAP_PROP_CONVERT_RGB is deliberately NOT touched. Turning it off
                    // does make CAP_PROP_FOURCC settable on MSMF, but it also stops
                    // OpenCV decoding at all: read() would hand back the raw MJPEG
                    // bitstream as a 1-D Mat and every caller above this seam expects
                    // BGR. That is a different change and a much larger one.
                    const RateOutcome rate = negotiateRate(
                        [&cap](double ask) -> double
                        {
                            cap.set(cv::CAP_PROP_FPS, ask);
                            return cap.get(cv::CAP_PROP_FPS);
                        },
                        static_cast<double>(fps));

                    clock = deviceClock();

                    sayFinding(fourccRequestFinding((int)(i + 1), fourcc_accepted, deviceBackendName()));
                    sayFinding(negotiationFinding((int)(i + 1), rate));

                    log_debug("Opened camera " + log_string(i + 1) + " at " + log_string(width) + "x" + log_string(height) + " @ " + log_string(fps) + " FPS" +
                              " (requested FOURCC: " + log_string_src(decodeFourCC(fourcc)) + ", backend: " + log_string_src((std::string)deviceBackendName()) + ")");
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

                    const NegotiatedFormat negotiated = readFourCC((int)fourcc);

                    log_debug("Camera " + log_string(i + 1) + " verification:");
                    log_debug("  Resolution: " + log_string((int)actual_width) + "x" + log_string((int)actual_height) + " (expected: " + log_string(width) + "x" + log_string(height) + ")");
                    log_debug("  FPS: " + log_string((int)actual_fps) + " (expected: " + log_string(fps) + ")");
                    log_debug("  FOURCC: " + log_string_src(negotiated.name) + " (expected: " + log_string_src(decodeFourCC(cv::VideoWriter::fourcc('M', 'J', 'P', 'G'))) + ")");
                    log_debug("  Backend: " + log_string_src(cap.getBackendName()) + " (expected: " + log_string_src((std::string)deviceBackendName()) + ")");

                    // Said at INFO or WARN, not DEBUG: what a camera negotiated, and
                    // whether the rate it was given is the rate it was asked for, are
                    // the two facts a person debugging three cameras on one USB bus has
                    // to see. #1319: "the backend said nothing" is its own outcome and
                    // no longer renders as a hole in the middle of the sentence.
                    sayFinding(formatFinding((int)(i + 1), (int)fourcc, (int)actual_width, (int)actual_height, actual_fps));
                    sayFinding(rateFinding((int)(i + 1), actual_fps, (double)fps, /*is_device*/ true));

                    OpenedCamera device;
                    device.code = (int)fourcc;
                    device.width = (int)actual_width;
                    device.height = (int)actual_height;
                    device.fps = actual_fps > 0 ? actual_fps : (double)fps;
                    opened_devices.push_back(device);

                    if (actual_fps > 0)
                        nominal_fps_ = actual_fps;
                }
                else if (cap.get(cv::CAP_PROP_FPS) > 0)
                {
                    nominal_fps_ = cap.get(cv::CAP_PROP_FPS);

                    // #1319: the same comparison a device gets, at INFO, because a clip
                    // has the rate it was recorded at and --fps paces nothing here. It
                    // is deliberately NOT the format verification above, which stays
                    // device-only: a file has no negotiation to report.
                    sayFinding(rateFinding((int)(i + 1), nominal_fps_, (double)fps, /*is_device*/ false));
                }

                descriptions_.push_back(sources[i] + " (" + cap.getBackendName() + ")");
                captures_.push_back(cap);
                clocks_.push_back(clock);
                anchors_.push_back(0);
                anchored_.push_back(false);
                log_info("Camera/video " + log_string(i + 1) + " initialized successfully");
            }

            // #1319: said ONCE, here, because this is the first point in the program
            // that knows how many devices were opened and what each of them is running.
            // It used to be a clause bolted to every per-camera warning, where the
            // number of cameras is exactly the thing that is not in scope.
            sayFinding(busFinding(opened_devices));

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
            // ---- #895 instrumentation, the same shape and equally NOT a fix. ----
            // OD_DROP_CAM fails ONE named slot on a schedule, which is a camera that
            // drops frames. This fails EVERY slot from a named cycle onward and never
            // recovers, which is the different thing a pub actually produces: somebody
            // catches the USB hub at nine o'clock and the board is blind from then on.
            // At the seam the two are the same observable -- read() returning false for
            // every capture -- so this stands in for an unplug that cannot be performed
            // on a container with three video files for cameras.
            static const int od_blind_after = odEnvInt("OD_BLIND_AFTER", 0);
            // ---- #899 instrumentation, the third of the same shape and equally NOT a
            // feature. OD_BLIND_AFTER is one-way by construction -- "and never recovers"
            // is what #895 wanted from it -- so nothing in the program could produce the
            // half of a pub evening this issue is about: the plug goes back in. This
            // names how LONG the cameras stay unplugged, in milliseconds of wall clock
            // from the cycle OD_BLIND_AFTER blinded them.
            //
            // Milliseconds rather than a second cycle number, and the difference is not a
            // taste: the thing being tested is a threshold measured in seconds and a
            // backoff measured in seconds, while a blind cycle costs about a millisecond
            // because read() returns immediately -- so a window stated in cycles is a
            // window of unpredictable length in the only unit the behaviour under test is
            // written in. Stated in milliseconds, "the cameras come back during the
            // second retry" is a fact about the run rather than a guess about its speed.
            //
            // The clock is shared by every capture source this process opens, so a
            // recovery that reopens the cameras is still inside the same window -- which
            // is what makes an early attempt fail and a later one succeed.
            static const int od_blind_for_ms = odEnvInt("OD_BLIND_FOR_MS", 0);
            static std::chrono::steady_clock::time_point od_blinded_at{};
            static bool od_unblinding_said = false;
            static long od_short_cycles = 0;
            od_cycle++;
            const bool od_inject = (od_drop_every > 0 && (od_cycle % od_drop_every) == 0);
            bool od_blind = (od_blind_after > 0 && od_cycle >= od_blind_after);
            if (od_blind && od_cycle == od_blind_after)
            {
                od_blinded_at = std::chrono::steady_clock::now();
                log_error("BLINDING cycle=" + std::to_string(od_cycle) +
                          " every camera stops answering from here (OD_BLIND_AFTER)" +
                          (od_blind_for_ms > 0 ? " for " + std::to_string(od_blind_for_ms) + " ms" : ""));
            }
            if (od_blind && od_blind_for_ms > 0)
            {
                const long blind_ms = (long)std::chrono::duration_cast<std::chrono::milliseconds>(
                                          std::chrono::steady_clock::now() - od_blinded_at)
                                          .count();
                if (blind_ms >= od_blind_for_ms)
                {
                    od_blind = false;
                    if (!od_unblinding_said)
                    {
                        od_unblinding_said = true;
                        log_warning("UNBLINDING cycle=" + std::to_string(od_cycle) + " after " +
                                    std::to_string(blind_ms) +
                                    " ms the cameras answer again (OD_BLIND_FOR_MS)");
                    }
                }
            }

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

                // #895: and the unplug, which takes every slot and keeps them.
                if (od_blind)
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
                    // ---- the per-stream clock anchor (Windows study §5.2) ----
                    // The anchor is the host instant at which THIS camera's clock reads
                    // zero, so that pos_ms + anchor is a host instant for every backend
                    // and the three are comparable. It is NOT simply the host instant of
                    // the first frame: that is only the same number when the source's
                    // clock happens to read zero on the frame you first got, and Media
                    // Foundation promises no such thing — a reader that has been running
                    // hands you a sample time already some way into the stream.
                    //
                    // Getting this wrong on MSMF does not produce an error. It produces
                    // three streams that agree because they all start near zero, i.e. a
                    // skew near zero, which reads as an excellent result.
                    //
                    // What it costs, said out loud: now_ns is taken after read() returns,
                    // so the anchor carries that camera's first-frame transfer and decode
                    // latency as a constant per-camera bias for the rest of the run. That
                    // bias is the residual error in the anchored figure and OpenCV offers
                    // no way to remove it.
                    if (!anchored_[i])
                    {
                        int64_t origin_ns = now_ns;
                        if (pos_ms >= 0.0)
                            origin_ns -= static_cast<int64_t>(pos_ms * 1e6);
                        anchors_[i] = origin_ns;
                        anchored_[i] = true;
                        log_info("CAPANCHOR cam=" + std::to_string(i + 1) +
                                 " clock=" + std::string(clockKindName(clocks_[i])) +
                                 " first_pos_ms=" + std::to_string((long long)pos_ms) +
                                 " host_return_us=" + std::to_string((long long)(now_ns / 1000)) +
                                 " anchor_us=" + std::to_string((long long)(anchors_[i] / 1000)));
                    }

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
        std::vector<bool> anchored_;
        std::vector<std::string> descriptions_;
        double nominal_fps_ = 0.0;
    };

    inline std::unique_ptr<CaptureSource> makeCaptureSource()
    {
        return std::unique_ptr<CaptureSource>(new OpenCvCaptureSource());
    }
}
