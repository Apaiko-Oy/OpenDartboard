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
#include <algorithm>
#include <string>
#include <vector>
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

    // The third half of that backend knowledge, and #1319 is what it cost. How a
    // backend is asked for a COMPRESSED stream differs between the two, and neither
    // way works on the other:
    //
    //   V4L2  — CAP_PROP_FOURCC is the negotiation. It becomes a VIDIOC_S_FMT with
    //           V4L2_PIX_FMT_MJPEG, the driver honours it, and the rate that is then
    //           asked for is just a rate. No floor: the operator's --fps goes through
    //           untouched, as it always has, on the platform that never had this bug.
    //   MSMF  — CAP_PROP_FOURCC names the format OpenCV converts INTO and is refused
    //           for MJPG outright. The camera's mode is chosen by nearest frame rate
    //           and the format is never scored, so the RATE is the compression request
    //           and 30 is the floor that reaches a compressed mode on a UVC module
    //           whose uncompressed mode is slow because it is uncompressed.
    //
    // OD_CAPTURE_FPS overrides the floor, including to 0 to turn it off. It is here so
    // that the rig this issue was measured on can be asked a different question without
    // a rebuild, which is the only reason anything in this repository reads an
    // environment variable.
#ifdef _WIN32
    inline int deviceBackend() { return cv::CAP_MSMF; }
    inline const char *deviceBackendName() { return "MSMF"; }
    inline CaptureClock deviceClock() { return CaptureClock::SourceRelative; }
    inline double captureRateFloorDefault() { return 30.0; }
#else
    inline int deviceBackend() { return cv::CAP_V4L2; }
    inline const char *deviceBackendName() { return "V4L2"; }
    inline CaptureClock deviceClock() { return CaptureClock::HostMonotonic; }
    inline double captureRateFloorDefault() { return 0.0; }
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

    /**
     * #1338 instrumentation, and equally NOT a fix: the same variable, read as a list.
     *
     * OD_DROP_CAM has always named ONE slot to fail, which produces a camera that drops
     * frames. The state #1338 was filed about is two cameras that deliver nothing at all
     * on one bus (#1319), and it cannot be reached one slot at a time. `OD_DROP_CAM=1,2`
     * fails both; `OD_DROP_CAM=1` is exactly what it always was, so no existing harness
     * moves. The name is not doubled up because two environment variables differing by an
     * `s` is a trap somebody sets for themselves at two in the morning.
     */
    inline std::vector<int> odEnvInts(const char *name)
    {
        std::vector<int> out;
        const char *v = std::getenv(name);
        if (!v || !*v)
            return out;
        std::string all(v), one;
        std::istringstream in(all);
        while (std::getline(in, one, ','))
        {
            if (!one.empty())
                out.push_back(std::atoi(one.c_str()));
        }
        return out;
    }

    // #1319: the floor this deployment uses, read once. A value of 0 turns it off.
    inline double captureRateFloor()
    {
        const int floor_rate = odEnvInt("OD_CAPTURE_FPS", (int)captureRateFloorDefault());
        return floor_rate > 0 ? (double)floor_rate : 0.0;
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
    // #1282: how many consecutive refusals from an already-delivering file source are
    // read as its end rather than as a bad frame. Three, because a real end never
    // recovers and the cost of being wrong for two more cycles is two more ERROR lines.
    static const int kEndOfFootageFailures = 3;

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
            is_file_.clear();
            delivered_.clear();
            failures_.clear();
            ended_.clear();
            seeked_to_.clear();
            nominal_fps_ = static_cast<double>(fps);

            log_info("Initializing " + log_string(sources.size()) + " cameras...");

            for (size_t i = 0; i < sources.size(); i++)
            {
                cv::VideoCapture cap;
                log_debug("Opening camera " + log_string(i + 1) + ": " + sources[i]);
                // #1618: the frame DEBUG_SEEK_VIDEO put this file at, 0 where nothing
                // seeked it. What alignSeekedFiles() evens out once calibration is done.
                int seeked_to = 0;

                CaptureClock clock = CaptureClock::Unknown;
                // #1319: the rate the DEVICE was asked for, which the verification
                // below compares what it got against. A file is never asked for one, so
                // it keeps the operator's number and the file branch reads --fps as it
                // always has.
                double asked_fps = (double)fps;

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
                    // #1551: this seek DECIDES THE CALIBRATION WINDOW, and the window
                    // decides admission. The Scorer averages the first thirty frames this
                    // capture hands over, so a dev binary calibrates on ~3.0 s of the clip
                    // and a release binary on 0.0 s -- identical bytes, two different
                    // pictures. On mocks/rig-20260922 camera 1 that is the whole of the
                    // "nondeterministic" gate: R=0.577558 (refused) at this window,
                    // R=0.873343 (admitted) at the clip's opening, each bit-identical over
                    // every recorded run of its own binary. So two things are said here:
                    //
                    //   - the seek is announced at INFO rather than DEBUG, because a census
                    //     log that does not say which window it measured is a census that
                    //     will be compared across the flip (od-baselines/5bc3b0a was);
                    //   - OD_SEEK_VIDEO=off holds THIS binary at the clip's opening --
                    //     #815's convention, the falsification switch on one binary, so the
                    //     flip is reproducible on demand and "a different build" is never
                    //     the confound. Default unchanged: a dev binary seeks as it always
                    //     has, and every number testers/i1323_run.sh and i1331_run.sh pin
                    //     to this window stays where it was measured.
                    static const bool od_seek_off = []
                    {
                        const char *v = std::getenv("OD_SEEK_VIDEO");
                        return v && std::string(v) == "off";
                    }();
                    double seek_seconds = od_seek_off ? 0.0 : 3 - (i * 0.18); // Example: seek 4 seconds for first video, 3 for second, etc.
                    if (od_seek_off)
                    {
                        log_info("DEBUG_SEEK_VIDEO: video " + log_string(i + 1) +
                                 " held at the clip's opening (OD_SEEK_VIDEO=off) -- this dev binary is "
                                 "calibrating on the release window, so its numbers may be held against a "
                                 "release build's and NOT against this tree's own registry runs (#1551)");
                    }
                    if (seek_seconds > 0 && cap.isOpened())
                    {
                        double video_fps = cap.get(cv::CAP_PROP_FPS);
                        if (video_fps > 0)
                        {
                            int target_frame = static_cast<int>(video_fps * seek_seconds);
                            cap.set(cv::CAP_PROP_POS_FRAMES, target_frame);
                            seeked_to = target_frame;
                            log_info("DEBUG_SEEK_VIDEO: video " + log_string(i + 1) + " seeked forward by " +
                                     log_string(seek_seconds) + " seconds (frame " + log_string(target_frame) +
                                     ") -- every calibration number of this run belongs to this window, and a "
                                     "census from it may only be compared to a run that seeked the same way (#1551)");
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

                    // #1319: the rate the DEVICE is asked for is not the operator's
                    // --fps, because on MSMF the rate IS the format request — its mode
                    // chooser scores nearest-frame-rate and never looks at the subtype,
                    // so asking for 15 on a module whose modes are `mjpeg @30` and
                    // `yuyv422 @10` picks the uncompressed one by five frames a second.
                    //
                    // ONE set(), before the camera has ever been in the slow mode. An
                    // earlier shape of this fix asked for --fps, read back the
                    // shortfall and re-asked for 30; it negotiated correctly on cameras
                    // 1 and 2 and hung the board on camera 3, twice, never reaching
                    // calibration, while `--fps 30` — one set at open — works on all
                    // three. Each set(CAP_PROP_FPS) is two SetCurrentMediaType calls on
                    // a source reader already bound to the device, which for a UVC
                    // camera renegotiates its isochronous reservation on a bus two
                    // other cameras are already holding. capture.hpp carries the
                    // reading; the rule it leaves is that a device is configured once.
                    //
                    // CAP_PROP_CONVERT_RGB is deliberately NOT touched. Turning it off
                    // does make CAP_PROP_FOURCC settable on MSMF, but it also stops
                    // OpenCV decoding at all: read() would hand back the raw MJPEG
                    // bitstream as a 1-D Mat and every caller above this seam expects
                    // BGR. That is a different change and a much larger one.
                    asked_fps = captureRateRequest((double)fps, captureRateFloor());
                    cap.set(cv::CAP_PROP_FPS, asked_fps);

                    clock = deviceClock();

                    // Only about a camera that is actually there. A device that never
                    // opened refuses every property it is handed, and a warning that
                    // its backend would not take MJPG is a true sentence about the
                    // wrong thing sitting directly above "Failed to open camera".
                    if (cap.isOpened())
                    {
                        sayFinding(fourccRequestFinding((int)(i + 1), fourcc_accepted, deviceBackendName()));
                        sayFinding(rateFloorFinding((int)(i + 1), (double)fps, asked_fps, deviceBackendName()));
                    }

                    log_debug("Opened camera " + log_string(i + 1) + " at " + log_string(width) + "x" + log_string(height) + " @ " + log_string((int)asked_fps) + " FPS" +
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
                    log_debug("  FPS: " + log_string((int)actual_fps) + " (asked for: " + log_string((int)asked_fps) + ", --fps: " + log_string(fps) + ")");
                    log_debug("  FOURCC: " + log_string_src(negotiated.name) + " (expected: " + log_string_src(decodeFourCC(cv::VideoWriter::fourcc('M', 'J', 'P', 'G'))) + ")");
                    log_debug("  Backend: " + log_string_src(cap.getBackendName()) + " (expected: " + log_string_src((std::string)deviceBackendName()) + ")");

                    // Said at INFO or WARN, not DEBUG: what a camera negotiated, and
                    // whether the rate it was given is the rate it was asked for, are
                    // the two facts a person debugging three cameras on one USB bus has
                    // to see. #1319: "the backend said nothing" is its own outcome and
                    // no longer renders as a hole in the middle of the sentence.
                    sayFinding(formatFinding((int)(i + 1), (int)fourcc, (int)actual_width, (int)actual_height, actual_fps));
                    // #1319: compared against the rate the device was really ASKED for,
                    // not against --fps. Those are the same number everywhere there is
                    // no floor, and where there is one, holding a camera to a request
                    // nobody made of it is how a working board grows a standing warning.
                    sayFinding(rateFinding((int)(i + 1), actual_fps, asked_fps, /*is_device*/ true));

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
                // #1282. Reopening is how #899's recovery gets its sight back, and it
                // rewinds a file to frame 0 -- so an end recorded before the reopen is
                // not true of the capture that comes out of it.
                is_file_.push_back(isVideoFile(sources[i]));
                delivered_.push_back(false);
                failures_.push_back(0);
                ended_.push_back(false);
                seeked_to_.push_back(seeked_to);
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

        // #1618: see CaptureSource::alignSeekedFiles. Every read() since open() took one
        // frame from every file, so a file seeked to an earlier frame is still behind the
        // furthest one by exactly the difference of the two seeks; it is read forward by
        // that many frames and nothing else moves. A file nothing seeked (a release
        // build, or OD_SEEK_VIDEO=off) has seeked_to_ 0 on every camera and reads nothing.
        void alignSeekedFiles() override
        {
            int furthest = 0;
            for (size_t i = 0; i < seeked_to_.size(); i++)
            {
                if (i < is_file_.size() && is_file_[i])
                    furthest = std::max(furthest, seeked_to_[i]);
            }
            std::string said;
            for (size_t i = 0; i < seeked_to_.size() && i < captures_.size(); i++)
            {
                if (i >= is_file_.size() || !is_file_[i])
                    continue;
                const int behind = furthest - seeked_to_[i];
                int read_forward = 0;
                for (int n = 0; n < behind; n++)
                {
                    if (!captures_[i].grab())
                        break;
                    read_forward++;
                }
                seeked_to_[i] += read_forward;
                if (behind > 0)
                {
                    said += (said.empty() ? "" : ", ") + std::string("video ") + log_string(i + 1) +
                            " read forward " + log_string(read_forward) + " of " + log_string(behind) +
                            " frame(s)";
                }
            }
            if (!said.empty())
            {
                log_info("SEEK ALIGN: " + said + ", so every file camera now shows the same instant "
                         "of the recording -- DEBUG_SEEK_VIDEO's staggered seek stays the "
                         "calibration window and stops being the replay (#1618)");
            }
        }

        // #1282: true only when there is at least one source, every one of them is a
        // file, and every one of them has reached its end. One device among the sources
        // makes this false for ever, which is the point: a rig does not end.
        bool footageEnded() const override
        {
            if (captures_.empty())
                return false;
            for (size_t i = 0; i < captures_.size(); i++)
            {
                if (i >= is_file_.size() || !is_file_[i] || !ended_[i])
                    return false;
            }
            return true;
        }

        std::vector<Frame> read() override
        {
            // ---- #798 instrumentation: fault injection + drop reporting. NOT a fix. ----
            // It forces one named camera's read to fail on a schedule, so that a marked
            // slot can be produced on footage that never drops one. It moved here with
            // the seam; it used to live in captureFrames.
            static long od_cycle = 0;
            static const std::vector<int> od_drop_cam = odEnvInts("OD_DROP_CAM");
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

                const bool od_named = std::find(od_drop_cam.begin(), od_drop_cam.end(),
                                                static_cast<int>(i)) != od_drop_cam.end();
                if (od_inject && od_named)
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
                    // #1282: this source has now handed over a frame, so a later refusal
                    // is a refusal to CONTINUE rather than a refusal to start.
                    delivered_[i] = true;
                    failures_[i] = 0;
                    ended_[i] = false;
                }
                else
                {
                    // ---- #1282: the end of a clip, told from a camera that stopped ----
                    //
                    // read() answers false for both, and before this it was logged as the
                    // same thing: one ERROR line per camera per cycle, for ever, at
                    // whatever rate the loop runs -- about 200 MB of stdout in one Windows
                    // release run after the three mocks finished.
                    //
                    // Four things have to be true before a failure is called an end, and
                    // each one refuses a state that is NOT the end of a clip:
                    //
                    //   is_file_[i]      a device is never exhausted. A camera does not
                    //                    end, so a rig can never reach this branch at all.
                    //   not injected     #895's blind and #798's drop injection force
                    //                    success=false on whatever sources are configured,
                    //                    mocks included. A board made blind on purpose is
                    //                    the vigil's subject, not a finished clip.
                    //   delivered_[i]    a file that never gave a frame did not END, it
                    //                    failed to start -- a missing or unreadable clip,
                    //                    which stays the ERROR it is today.
                    //   isOpened()       a capture the backend has torn down is a fault.
                    //
                    // And then it must hold for kEndOfFootageFailures consecutive cycles,
                    // so one undecodable frame in the middle of a clip is not mistaken for
                    // its end. A real end never recovers; a decode hiccup does.
                    const bool injected = (od_inject && od_named) || od_blind;
                    if (!injected && i < is_file_.size() && is_file_[i] && delivered_[i] &&
                        captures_[i].isOpened())
                    {
                        if (!ended_[i] && ++failures_[i] >= kEndOfFootageFailures)
                        {
                            ended_[i] = true;
                            log_info("END OF FOOTAGE cam=" + std::to_string(i + 1) +
                                     " cycle=" + std::to_string(od_cycle) +
                                     " last_pos_ms=" + std::to_string((long)pos_ms) +
                                     " source=" + describe(i) +
                                     " — the clip has run out. A camera never does this.");
                        }
                    }
                    // The slot stays, marked, so that a camera's position never changes.
                    if (!ended_[i])
                    {
                        log_error("Failed to capture frame from camera " + log_string(i + 1) + " - slot marked unavailable");
                    }
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
                // #1282: a slot whose clip has ended is not a slot that was dropped, and
                // reporting it as one is the other half of the unbounded output. While
                // nothing has ended this is `captures_.size()` and every line below is
                // the line that was printed before.
                size_t expected = captures_.size();
                for (size_t i = 0; i < captures_.size() && i < ended_.size(); i++)
                {
                    if (ended_[i])
                        expected--;
                }
                if (have != expected)
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
        // #1282: the four things it takes to tell the end of a clip from a camera that
        // has stopped answering. See the read() docblock for what each one refuses.
        std::vector<bool> is_file_;
        std::vector<bool> delivered_;  // this source has handed over at least one frame
        std::vector<int> failures_;    // consecutive failed reads, reset by any success
        std::vector<bool> ended_;      // this file has reached its end and said so once
        std::vector<int> seeked_to_;   // #1618: the frame the dev seek put each file at
        double nominal_fps_ = 0.0;
    };

    inline std::unique_ptr<CaptureSource> makeCaptureSource()
    {
        return std::unique_ptr<CaptureSource>(new OpenCvCaptureSource());
    }
}
