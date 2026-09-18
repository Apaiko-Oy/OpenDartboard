#pragma once

#include "frame.hpp"
#include <cstdio>
#include <memory>
#include <string>
#include <vector>
#include <algorithm>

namespace camera
{
    // Determine if a given path is a video file based on its extension
    inline bool isVideoFile(const std::string &path)
    {
        std::string lower_path = path;
        std::transform(lower_path.begin(), lower_path.end(), lower_path.begin(), ::tolower);

        const std::vector<std::string> video_extensions = {".mp4", ".avi", ".mkv", ".mov", ".wmv"};
        for (const auto &ext : video_extensions)
        {
            if (lower_path.length() >= ext.length() &&
                lower_path.substr(lower_path.length() - ext.length()) == ext)
            {
                return true;
            }
        }
        return false;
    }

    // Simple function to decode a fourcc code to its four characters. Kept because a
    // code known to be printable -- the MJPG constant this program asks for -- has no
    // other spelling. It is NOT how a code READ BACK from a backend is named: that code
    // may be zero, and this returns "" for zero. readFourCC() below is what a read-back
    // goes through, and #1319 is the issue that cost.
    inline std::string decodeFourCC(int fourcc)
    {
        char code[5];
        code[0] = (fourcc & 0xFF);
        code[1] = (fourcc >> 8) & 0xFF;
        code[2] = (fourcc >> 16) & 0xFF;
        code[3] = (fourcc >> 24) & 0xFF;
        code[4] = '\0';
        return std::string(code);
    }

    // ---- #1319: what a backend answered when asked what it negotiated ----
    //
    // "The backend did not say" and "the backend said something that is not MJPG" are
    // two different facts about a camera and they used to be one empty string. MSMF
    // answers CAP_PROP_FOURCC with 0 on these modules, decodeFourCC(0) built a char[5]
    // whose first byte is '\0', and the warning read "Camera 1 negotiated , not MJPG" --
    // a sentence with a hole in it, three times, which is part of why a maintainer
    // reading it concluded the cameras had no MJPG mode when they have one.
    enum class FormatReport
    {
        NotReported, // the backend answered 0: it is not telling us what it granted
        Unprintable, // a non-zero code that is not four printable characters
        Named        // four printable characters: MJPG, YUY2, NV12, ...
    };

    struct NegotiatedFormat
    {
        FormatReport report = FormatReport::NotReported;
        int code = 0;
        std::string name;             // never empty, whatever the code was
        bool is_mjpg = false;         //
        double bytes_per_pixel = 0.0; // 0 when not knowable: compressed, unnamed, or unreported
    };

    // How many bytes one pixel costs on the wire, for the uncompressed formats a UVC
    // camera actually falls back to. A compressed format answers 0, which means "not
    // knowable from the format alone" rather than "free" -- the bandwidth statement
    // below refuses to make a claim about a camera whose rate it cannot compute.
    inline double bytesPerPixelOf(const std::string &name)
    {
        if (name == "YUY2" || name == "YUYV" || name == "UYVY" || name == "YVYU" || name == "HDYC")
            return 2.0;
        if (name == "NV12" || name == "NV21" || name == "YV12" || name == "I420" || name == "IYUV")
            return 1.5;
        if (name == "RGB3" || name == "BGR3")
            return 3.0;
        if (name == "RGB4" || name == "BGR4" || name == "RGBA" || name == "BGRA" || name == "AR24")
            return 4.0;
        if (name == "GREY" || name == "Y800" || name == "Y8  ")
            return 1.0;
        return 0.0; // MJPG, H264, HEVC, MP4V, and anything we have not met
    }

    // The only way a code read back from a backend becomes words. Never returns an
    // empty name: a zero is NotReported and names itself, and a code that is not four
    // printable characters is spelled in hex rather than pasted into a sentence raw.
    inline NegotiatedFormat readFourCC(int code)
    {
        NegotiatedFormat format;
        format.code = code;

        if (code == 0)
        {
            format.report = FormatReport::NotReported;
            format.name = "none reported";
            return format;
        }

        char text[5];
        text[0] = (char)(code & 0xFF);
        text[1] = (char)((code >> 8) & 0xFF);
        text[2] = (char)((code >> 16) & 0xFF);
        text[3] = (char)((code >> 24) & 0xFF);
        text[4] = '\0';

        bool printable = true;
        for (int i = 0; i < 4; i++)
        {
            if ((unsigned char)text[i] < 0x20 || (unsigned char)text[i] > 0x7E)
                printable = false;
        }

        if (!printable)
        {
            char hex[16];
            std::snprintf(hex, sizeof(hex), "0x%08X", (unsigned)code);
            format.report = FormatReport::Unprintable;
            format.name = hex;
            return format;
        }

        format.report = FormatReport::Named;
        format.name = text;
        format.is_mjpg = (format.name == "MJPG");
        format.bytes_per_pixel = bytesPerPixelOf(format.name);
        return format;
    }

    // ---- #1319: the three findings, as values rather than as log calls ----
    //
    // The decision is here and the logging is at the call site, so every branch can be
    // exercised without a camera: a FOURCC of 0 renders as a named outcome in a test
    // that opens nothing. Severity is carried rather than chosen by the caller, because
    // which of these is a warning IS the finding.
    enum class Severity
    {
        Info,
        Warning
    };

    struct Finding
    {
        bool said = false; // false: there is nothing to say, and nothing is logged
        Severity severity = Severity::Info;
        std::string text;
    };

    inline std::string wholeNumber(double value)
    {
        return std::to_string((long long)(value + (value < 0 ? -0.5 : 0.5)));
    }

    inline std::string oneDecimal(double value)
    {
        char text[32];
        std::snprintf(text, sizeof(text), "%.1f", value);
        return text;
    }

    // What a camera's negotiated format is, said once per camera at open.
    inline Finding formatFinding(int camera_number, int code, int width, int height, double fps)
    {
        const NegotiatedFormat format = readFourCC(code);
        const std::string where = "Camera " + std::to_string(camera_number);
        const std::string mode = std::to_string(width) + "x" + std::to_string(height) +
                                 " @ " + wholeNumber(fps) + " fps";

        Finding finding;
        finding.said = true;

        if (format.report == FormatReport::NotReported)
        {
            finding.severity = Severity::Warning;
            finding.text = where + ": MJPG was requested and the backend reported no format at all " +
                           "(CAP_PROP_FOURCC read back as 0), so what it is running at " + mode +
                           " cannot be told from here";
            return finding;
        }

        if (format.is_mjpg)
        {
            finding.severity = Severity::Info;
            finding.text = where + " negotiated MJPG at " + mode;
            return finding;
        }

        finding.severity = Severity::Warning;
        finding.text = where + " negotiated " + format.name + " at " + mode +
                       ", not the MJPG that was requested";
        return finding;
    }

    // A rate granted is compared against the rate asked for. The tolerance is a whole
    // frame because 29.97 is 30 and a backend that rounds is not a backend that refused.
    inline bool rateWasGranted(double granted, double requested)
    {
        if (granted <= 0.0) // the backend did not say; nothing to compare
            return true;
        const double gap = granted - requested;
        return (gap < 0 ? -gap : gap) <= 1.0;
    }

    // Two callers, two different facts, one comparison.
    //
    //   a device -- the rate is the tell. Ten frames a second out of a camera asked for
    //   fifteen is the YUY2 mode answering, and it is a warning.
    //
    //   a file -- a clip has the rate it was recorded at and --fps paces nothing here,
    //   so the mismatch is worth saying and is not a fault. Said at INFO deliberately:
    //   the mock footage runs through this path and must stay free of new warnings.
    inline Finding rateFinding(int number, double granted, double requested, bool is_device)
    {
        Finding finding;
        if (rateWasGranted(granted, requested))
            return finding; // said == false

        finding.said = true;
        if (is_device)
        {
            finding.severity = Severity::Warning;
            finding.text = "Camera " + std::to_string(number) + " is running at " + wholeNumber(granted) +
                           " fps where " + wholeNumber(requested) +
                           " were requested — the backend granted a slower mode than the one asked for";
        }
        else
        {
            finding.severity = Severity::Info;
            finding.text = "Video " + std::to_string(number) + " runs at " + wholeNumber(granted) +
                           " fps where --fps says " + wholeNumber(requested) +
                           " — a file has the rate it was recorded at, so the request paces nothing here";
        }
        return finding;
    }

    // One opened device, as the bandwidth statement needs it.
    struct OpenedCamera
    {
        int code = 0;
        int width = 0;
        int height = 0;
        double fps = 0.0;
    };

    // Roughly what one USB 2.0 bus carries in practice, isochronous, with its own
    // overhead taken off the 60 MB/s the signalling rate suggests. It is a stated
    // threshold rather than a fact about a particular host, and the sentence prints it
    // beside the demand so a reader can disagree with the number.
    inline double usbTwoBusMegabytesPerSecond() { return 35.0; }

    // ---- #1319: bandwidth, said ONCE, where it can be known ----
    //
    // It used to be a clause on every per-camera warning -- "three cameras at this
    // resolution may not fit on one bus" -- which is a diagnosis attached to a
    // measurement that could not support it: one camera's warning knows nothing about
    // how many others were opened. open() knows, by the time its loop is done, how many
    // devices it opened and what each of them is running, so the claim is made there or
    // not at all. A camera whose format was never reported contributes nothing to the
    // sum and is counted as unknown, because a guess in a bandwidth figure is worse
    // than no figure.
    inline Finding busFinding(const std::vector<OpenedCamera> &cameras)
    {
        Finding finding;
        double megabytes_per_second = 0.0;
        int uncompressed = 0;
        int unknown = 0;
        std::string mode;

        for (const auto &camera : cameras)
        {
            const NegotiatedFormat format = readFourCC(camera.code);
            if (format.is_mjpg)
                continue;
            if (format.bytes_per_pixel <= 0.0)
            {
                unknown++;
                continue;
            }
            uncompressed++;
            megabytes_per_second += (double)camera.width * camera.height * format.bytes_per_pixel *
                                    camera.fps / 1000000.0;
            if (mode.empty())
                mode = std::to_string(camera.width) + "x" + std::to_string(camera.height) + " " +
                       format.name + " at " + wholeNumber(camera.fps) + " fps";
        }

        if (uncompressed == 0)
            return finding; // nothing measurable is uncompressed; no claim to make

        const double ceiling = usbTwoBusMegabytesPerSecond();
        const std::string who = std::to_string(uncompressed) +
                                (uncompressed == 1 ? " camera is" : " cameras are") + " running uncompressed";
        const std::string unknown_clause =
            unknown > 0 ? " (" + std::to_string(unknown) + " more reported no format and is not in this figure)" : "";

        finding.said = true;
        if (megabytes_per_second > ceiling)
        {
            finding.severity = Severity::Warning;
            finding.text = who + " — " + mode + " — demanding " + oneDecimal(megabytes_per_second) +
                           " MB/s against roughly " + oneDecimal(ceiling) +
                           " MB/s practical on one USB 2.0 bus" + unknown_clause +
                           ": the rate these cameras are giving is a bus limit, not a camera fault";
        }
        else
        {
            finding.severity = Severity::Info;
            finding.text = who + " — " + mode + " — demanding " + oneDecimal(megabytes_per_second) +
                           " MB/s, within roughly " + oneDecimal(ceiling) +
                           " MB/s practical on one USB 2.0 bus" + unknown_clause;
        }
        return finding;
    }

    // The seam. Everything above the capture layer talks to this and to Frame, and to
    // nothing platform-specific. One implementation per platform.
    class CaptureSource
    {
    public:
        virtual ~CaptureSource() = default;

        // Open every source. The backend and the pixel format are chosen here, per source.
        virtual bool open(const std::vector<std::string> &sources, int width, int height, int fps) = 0;

        // How many cameras there are. Every read() returns exactly this many slots.
        virtual size_t size() const = 0;

        // The frame rate the opened sources actually report, for anything that paces itself.
        virtual double nominalFps() const = 0;

        // One capture cycle: one slot per camera, in camera order, each carrying its own
        // acquisition instant.
        virtual std::vector<Frame> read() = 0;

        // numFrames cycles averaged per camera, for calibration. Each camera is divided by
        // what that camera actually contributed.
        virtual std::vector<Frame> readAveraged(int numFrames) = 0;

        // What this camera ended up being, in words, for the log.
        virtual std::string describe(size_t i) const = 0;
    };

    // The platform's implementation, chosen at build time.
    std::unique_ptr<CaptureSource> makeCaptureSource();
}
