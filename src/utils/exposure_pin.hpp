#pragma once

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include <opencv2/videoio.hpp>

/**
 * turnaus#1904: OD_EXPOSURE and OD_GAIN -- manual exposure and gain, set on every capture
 * DEVICE at open. Held pure here so the mapping onto OpenCV's properties can be checked
 * without a camera (testers/i1904_check.sh); capture_opencv.hpp is the only caller.
 *
 *   OD_EXPOSURE=<number>   manual exposure at that value, in the BACKEND's unit:
 *                            V4L2  exposure_absolute, 100 us steps (156 = 15.6 ms)
 *                            MSMF  log2 seconds (-6 = 1/64 s, -7 = 1/128 s)
 *   OD_EXPOSURE=auto       hand exposure back to the camera. A UVC camera keeps a manual
 *                          setting across process restarts on V4L2, so unsetting the pin
 *                          does not undo it -- this does
 *   OD_GAIN=<number>       gain at that value, the backend's raw unit
 *
 * Unset (or empty) is today's behaviour, unchanged: no property is set. A value that is not
 * a number is no pin at all, said once at WARN by the caller, never half-applied.
 *
 * The mode switch is where the two backends disagree (OpenCV 4.5.1 and 4.14.0 sources):
 *
 *   V4L2  CAP_PROP_AUTO_EXPOSURE is V4L2_CID_EXPOSURE_AUTO passed through raw (OpenCV's
 *         range normalisation is off unless OPENCV_VIDEOIO_V4L_RANGE_NORMALIZED is set):
 *         1 = V4L2_EXPOSURE_MANUAL, 3 = V4L2_EXPOSURE_APERTURE_PRIORITY, the UVC auto.
 *   MSMF  CAP_PROP_AUTO_EXPOSURE writes CameraControl_Exposure with the flag
 *         `value != 0 ? Auto : Manual` -- and writes `value` as the exposure too. So
 *         0 = manual, 1 = auto, and the mode must be set BEFORE CAP_PROP_EXPOSURE, or
 *         the 0 that asks for manual overwrites the exposure that was just set.
 *
 * Reading the mode back is V4L2's alone: MSMF's getProperty(CAP_PROP_AUTO_EXPOSURE)
 * compares the exposure VALUE with VideoProcAmp_Flags_Auto, not the flag, so it answers
 * "auto" exactly when the exposure happens to be 1. The start line says so rather than
 * report it.
 */
namespace exposure_pin
{
    enum class Backend
    {
        V4L2,
        MSMF,
        File // a video file: no camera, nothing to set
    };

    inline const char *backendName(Backend b)
    {
        switch (b)
        {
        case Backend::V4L2:
            return "V4L2";
        case Backend::MSMF:
            return "MSMF";
        default:
            return "file";
        }
    }

    struct Pin
    {
        enum Kind
        {
            Unset,
            Auto,
            Value,
            Refused
        } kind = Unset;
        double value = 0.0;
        std::string text;
    };

    /** OD_EXPOSURE's (or OD_GAIN's) text. `autoAllowed` is false for the gain. */
    inline Pin pinFrom(const char *text, bool autoAllowed)
    {
        Pin p;
        if (text == nullptr || *text == '\0')
        {
            return p;
        }
        p.text = text;
        if (autoAllowed && p.text == "auto")
        {
            p.kind = Pin::Auto;
            return p;
        }
        char *end = nullptr;
        const double v = std::strtod(text, &end);
        if (end == text || *end != '\0' || !std::isfinite(v))
        {
            p.kind = Pin::Refused;
            return p;
        }
        p.kind = Pin::Value;
        p.value = v;
        return p;
    }

    inline const Pin &exposurePin()
    {
        static const Pin p = pinFrom(std::getenv("OD_EXPOSURE"), true);
        return p;
    }

    inline const Pin &gainPin()
    {
        static const Pin p = pinFrom(std::getenv("OD_GAIN"), false);
        return p;
    }

    /** True when either pin asks the cameras for something. */
    inline bool asksAnything(const Pin &exposure, const Pin &gain)
    {
        return exposure.kind == Pin::Auto || exposure.kind == Pin::Value || gain.kind == Pin::Value;
    }

    /** CAP_PROP_AUTO_EXPOSURE's value meaning MANUAL on this backend. */
    inline double manualModeValue(Backend b)
    {
        return b == Backend::V4L2 ? 1.0 : 0.0;
    }

    /** CAP_PROP_AUTO_EXPOSURE's value meaning AUTO on this backend. */
    inline double autoModeValue(Backend b)
    {
        return b == Backend::V4L2 ? 3.0 : 1.0;
    }

    struct Set
    {
        int prop;
        double value;
        const char *name;
    };

    /** The set() calls to make at open, in order. Empty for a file and for no pin. */
    inline std::vector<Set> plan(Backend b, const Pin &exposure, const Pin &gain)
    {
        std::vector<Set> out;
        if (b == Backend::File)
        {
            return out;
        }
        if (exposure.kind == Pin::Auto)
        {
            out.push_back({cv::CAP_PROP_AUTO_EXPOSURE, autoModeValue(b), "auto_exposure"});
        }
        else if (exposure.kind == Pin::Value)
        {
            out.push_back({cv::CAP_PROP_AUTO_EXPOSURE, manualModeValue(b), "auto_exposure"});
            out.push_back({cv::CAP_PROP_EXPOSURE, exposure.value, "exposure"});
        }
        if (gain.kind == Pin::Value)
        {
            out.push_back({cv::CAP_PROP_GAIN, gain.value, "gain"});
        }
        return out;
    }

    /** What CAP_PROP_AUTO_EXPOSURE read back says, in words. */
    inline std::string modeRead(Backend b, double readBack)
    {
        if (b == Backend::MSMF)
        {
            return "mode not readable on MSMF";
        }
        if (readBack < 0)
        {
            return "mode unreadable";
        }
        if (readBack == 1.0)
        {
            return "mode reads manual";
        }
        char buf[64];
        std::snprintf(buf, sizeof(buf), "mode reads auto (V4L2 %g)", readBack);
        return buf;
    }

    inline std::string num(double v)
    {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%g", v);
        return buf;
    }

    /** What the camera said back to one property; -1 is OpenCV's "could not read it". */
    inline std::string got(double v)
    {
        return v == -1.0 ? "unreadable" : num(v);
    }

    /**
     * The per-start line for one camera: the mode it was put in, what was asked and what
     * the camera reports. `readExposure`, `readGain` and `readMode` are cap.get() of
     * CAP_PROP_EXPOSURE, CAP_PROP_GAIN and CAP_PROP_AUTO_EXPOSURE after the plan ran.
     */
    inline std::string startLine(int camera, Backend b, const Pin &exposure, const Pin &gain, double readExposure,
                                 double readGain, double readMode)
    {
        std::string s = "Camera " + std::to_string(camera) + " exposure: ";
        if (exposure.kind == Pin::Value)
        {
            s += "manual (OD_EXPOSURE), asked " + num(exposure.value) + ", got " + got(readExposure);
        }
        else if (exposure.kind == Pin::Auto)
        {
            s += "auto (OD_EXPOSURE=auto), reads " + got(readExposure);
        }
        else
        {
            s += "camera's own (OD_EXPOSURE unset), reads " + got(readExposure);
        }
        s += "; " + modeRead(b, readMode);
        if (gain.kind == Pin::Value)
        {
            s += "; gain asked " + num(gain.value) + ", got " + got(readGain);
        }
        else
        {
            s += "; gain reads " + got(readGain);
        }
        s += " (" + std::string(backendName(b)) + ")";
        return s;
    }
} // namespace exposure_pin
