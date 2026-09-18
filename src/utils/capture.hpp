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
            // #1319: `requested` here is the rate the DEVICE was asked for, which
            // captureRateRequest() below may have raised above the operator's --fps, so
            // a camera can now answer on either side of it. The sentence that used to
            // be printed here -- "granted a slower mode than the one asked for" -- was
            // true of every device mismatch that could happen before today and would be
            // a false statement about a camera doing better than asked, which is
            // exactly the kind of line this issue exists to stop printing.
            const bool slower = granted < requested;
            finding.severity = slower ? Severity::Warning : Severity::Info;
            finding.text = "Camera " + std::to_string(number) + " is running at " + wholeNumber(granted) +
                           " fps where " + wholeNumber(requested) + " were requested — the backend granted a " +
                           (slower ? std::string("slower") : std::string("faster")) +
                           " mode than the one asked for";
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

    // ---- #1319: the frame rate is the only lever MSMF gives over the wire format ----
    //
    // Read out of OpenCV 4.14.0's own source, which is the version .github/workflows/
    // release.yml builds this binary against (modules/videoio/src/cap_msmf.cpp; line
    // numbers are that tag's):
    //
    //   * `setProperty(CAP_PROP_FOURCC, v)` is one line -- `return
    //     configureVideoOutput(newFormat, (int)cvRound(value))` (2332) -- and
    //     configureVideoOutput's conversion switch (1153) accepts BGR3, RGB3, GREY and
    //     YUYV and answers `default: return false` to everything else. MJPG is
    //     everything else. So `cap.set(CAP_PROP_FOURCC, MJPG)` on MSMF is a no-op that
    //     returns false: it cannot ever have asked a camera for a compressed mode, in
    //     any order, before or after width and height.
    //
    //   * `getProperty(CAP_PROP_FOURCC)` is `captureVideoFormat.subType.Data1` (2172),
    //     and `captureVideoFormat` is assigned in initStream() (943) from the format
    //     the source reader was last set to -- which, with CAP_PROP_CONVERT_RGB on, is
    //     OpenCV's own RGB32/RGB24 conversion target rather than anything the camera
    //     transmits. MFVideoFormat_RGB32's Data1 is 22, which is the `0x00000016` the
    //     rig's log prints. The read-back is structurally incapable of naming the wire
    //     format, so no amount of reporting can verify this negotiation.
    //
    //   * What actually decides the camera's mode is findBestVideoFormat() (652),
    //     ranking every native mode with VideoIsBetterThan() (318): nearest resolution,
    //     then largest, then NEAREST FRAME RATE. The subtype is never scored at all.
    //
    // That last line is the whole of this issue. On a module offering
    // `mjpeg 1280x720@30` and `yuyv422 1280x720@10`, a request for 15 fps picks YUY2,
    // because |10-15| is 5 and |30-15| is 15 -- and `--fps` defaults to 15. The
    // detector asked for a rate nearer the uncompressed mode's and was given it,
    // faithfully.
    //
    // So the rate is the lever, and it is pulled ONCE, at open, before the camera has
    // ever been in the slow mode.
    //
    // ---- why once, and not by re-asking: measured on the rig, 2026-09-18 ----
    //
    // The first shape of this fix asked for --fps, read back the shortfall, and then
    // re-asked for a faster rate on the already-opened capture. It negotiated 30 fps
    // correctly on cameras 1 and 2 and then HUNG the board on camera 3, twice, killed
    // at 140s and at 300s, never reaching calibration. The same build given `--fps 30`
    // -- one set() of 30 at open and no re-ask -- works on all three.
    //
    // Why the re-ask is the half that hangs, from the same source: every
    // set(CAP_PROP_FPS) runs configureVideoOutput, which re-reads the whole native
    // format list off the live source and then calls initStream TWICE -- once for the
    // native type and once for the conversion type -- and each initStream is a
    // SetStreamSelection plus a SetCurrentMediaType (951) on a source reader already
    // bound to the device. For a UVC camera that is a renegotiation of the isochronous
    // bandwidth reservation on the USB bus. The re-ask did four of those on camera 3 --
    // eight SetCurrentMediaType calls -- while cameras 1 and 2 already held MJPG
    // reservations on the same controller, and the FIRST of them put camera 3
    // transiently into 1280x720 uncompressed, an 18.4 MB/s claim against a bus that has
    // about 35 and had already given most of it away.
    //
    // Whether it blocks in the driver's bandwidth arbitration is a hypothesis, and not
    // one this box can settle. What is measured is narrower and is enough: asking once
    // at open works on this hardware and re-asking after open does not. So the rate the
    // DEVICE is asked for is decided before the open, and the camera is never put into
    // the mode this issue is about, not even for an instant.

    // The rate a DEVICE is asked for, which is deliberately NOT the rate the operator
    // asked for. `--fps` says how fast the detector should see; on MSMF the same number
    // also, accidentally, chooses whether the camera compresses -- and those are two
    // different questions that had been answered with one number. The floor separates
    // them: it is what the mode chooser is handed, and it is high enough to score a
    // compressed mode nearer than an uncompressed one on every UVC module of this
    // class, because an uncompressed mode is slow precisely because it is uncompressed.
    //
    // A floor of 0 means no floor, and the operator's number goes through untouched.
    // That is the V4L2 case, where CAP_PROP_FOURCC really is the negotiation and has
    // always worked, and where raising the request would move a platform that has never
    // had this bug.
    inline double captureRateRequest(double operator_fps, double floor_rate)
    {
        return (floor_rate > operator_fps) ? floor_rate : operator_fps;
    }

    // Said once per camera, at INFO, when a device is being asked for a rate the
    // operator did not type. A number in a log that nobody typed and nothing explains
    // is the shape of defect this issue was filed about, so it explains itself.
    inline Finding rateFloorFinding(int number, double operator_fps, double asked, const std::string &backend)
    {
        Finding finding;
        if (asked <= operator_fps)
            return finding; // said == false: the operator's number went through untouched

        finding.said = true;
        finding.severity = Severity::Info;
        finding.text = "Camera " + std::to_string(number) + " is being asked for " + wholeNumber(asked) +
                       " fps rather than the " + wholeNumber(operator_fps) + " that --fps says, because on " +
                       backend + " the frame rate is the only thing the mode chooser scores: it takes the mode "
                       "whose rate is nearest the one requested and never looks at the format, so asking for " +
                       wholeNumber(operator_fps) +
                       " selects an uncompressed mode on a camera that has a faster compressed one. This is the "
                       "camera's mode, not the rate the detector runs its clock at";
        return finding;
    }

    // What the backend did with the MJPG request itself. cv::VideoCapture::set() answers
    // false when the backend refused it, and on MSMF it always does, for the reason in
    // the block above. Said so that a reader of the log is told the request failed BY
    // NAME -- the first acceptance criterion of #1319 -- rather than left to infer it
    // from a format read-back that cannot name a wire format either.
    inline Finding fourccRequestFinding(int number, bool accepted, const std::string &backend)
    {
        Finding finding;
        if (accepted)
            return finding; // said == false: V4L2 takes it, and there it is the negotiation

        finding.said = true;
        finding.severity = Severity::Warning;
        finding.text = "Camera " + std::to_string(number) + ": the " + backend +
                       " backend refused CAP_PROP_FOURCC outright, so MJPG cannot be asked for by name here. "
                       "On this backend that property names the format OpenCV converts INTO, not the one the "
                       "camera transmits, and the frame rate is the only thing its mode chooser scores — so the "
                       "rate is what asks for a compressed mode";
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
        {
            // Nothing measurable is uncompressed. On MSMF that is the ORDINARY case
            // rather than a happy one: the backend reports no format at all, so a
            // bandwidth figure cannot be computed from anything the program was told.
            // Saying nothing here is what #1319 is about, so the arithmetic is stated
            // instead -- explicitly as arithmetic about the mode these cameras really
            // are running, with the one unknown named as unknown. It is INFO because it
            // is a scale, not a measurement, and the two warnings above it carry the
            // alarm.
            if (unknown == 0 || cameras.empty())
                return finding; // every camera is compressed and accounted for

            const OpenedCamera &first = cameras.front();
            if (first.width <= 0 || first.height <= 0 || first.fps <= 0)
                return finding;

            const double each = (double)first.width * first.height * 2.0 * first.fps / 1000000.0;

            // #1319, second pass: the rate a camera negotiated is itself evidence about
            // its format, and once ONE camera's uncompressed demand does not fit the bus
            // the evidence is conclusive.
            //
            // Measured on the rig, 2026-09-18, on the build that fixed the negotiation:
            // three cameras at 1280x720 @ 30, all delivering, and this branch printed
            // "For scale: ... 55.3 MB/s per camera and 165.9 MB/s for 3, against roughly
            // 35.0 MB/s practical on one USB 2.0 bus" -- on a board that was working. It
            // reads as a warning about the problem this issue removed, which is the
            // defect this issue is about, one turn later.
            //
            // The sentence was not merely unhelpful, it was self-refuting: 55.3 MB/s is
            // ONE camera against a bus that carries about 35, so a camera running at that
            // rate and handing over frames cannot be uncompressed, whatever it declined to
            // say about its format. Before the fix the same arithmetic ran at 10 fps and
            // gave 18.4 MB/s each -- which one camera CAN do -- so the scale paragraph
            // described something the cameras really might have been doing, and it stays
            // for exactly that case.
            //
            // Note what this does NOT read: it asks nothing about whether a floor was
            // applied, or on which backend, or what anybody requested. It is an inference
            // from the rate the camera came back with, so a camera that reaches 30 fps by
            // some other route is read the same way.
            if (each > usbTwoBusMegabytesPerSecond())
            {
                finding.said = true;
                finding.severity = Severity::Info;
                finding.text = std::to_string(unknown) +
                               (unknown == 1 ? " camera reported" : " cameras reported") +
                               " no negotiated format, but the rate is evidence enough: " +
                               std::to_string(first.width) + "x" + std::to_string(first.height) +
                               " uncompressed at " + wholeNumber(first.fps) + " fps would be " +
                               oneDecimal(each) + " MB/s for a SINGLE camera, against roughly " +
                               oneDecimal(usbTwoBusMegabytesPerSecond()) +
                               " MB/s practical on one USB 2.0 bus — so a camera delivering frames at "
                               "this rate is compressing them, and there is no bandwidth problem to report";
                return finding;
            }

            finding.said = true;
            finding.severity = Severity::Info;
            finding.text = std::to_string(unknown) +
                           (unknown == 1 ? " camera reported" : " cameras reported") +
                           " no negotiated format, so no bandwidth figure can be measured. For scale: " +
                           std::to_string(first.width) + "x" + std::to_string(first.height) +
                           " uncompressed at " + wholeNumber(first.fps) + " fps is " + oneDecimal(each) +
                           " MB/s per camera and " + oneDecimal(each * unknown) + " MB/s for " +
                           std::to_string(unknown) + ", against roughly " + oneDecimal(usbTwoBusMegabytesPerSecond()) +
                           " MB/s practical on one USB 2.0 bus";
            return finding;
        }

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
