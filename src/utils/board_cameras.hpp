#pragma once

#include <algorithm>
#include <cstdlib>
#include <string>
#include <vector>

#include <memory>

#include "autocam.hpp"
#include "camera.hpp"
#include "logging.hpp"
#include "detector/geometry/calibration/geometry_calibration.hpp"
#include "detector/geometry/calibration/board_look.hpp"

// #1318: which cameras a start with no --cams opens, decided by looking through each
// one rather than by counting devices.
//
// The defaults were 0,1,2 on Windows and /dev/video0,1,2 on Linux, and #1258 made a
// default index with no camera behind it into a question rather than a crash. That is
// the right behaviour for a camera that does not open. It says nothing about the case
// this file exists for: on a laptop, index 0 is the built-in webcam, so the three
// board cameras are 1, 2 and 3, all three defaults OPEN, and the detector calibrates the
// operator's face and never opens the third board camera at all.
//
// So the candidates are every video device the machine has, and the decision is a probe:
// open one, average a handful of frames, run the real calibration on it, and keep it if
// board_look says a dartboard was in the picture. One camera at a time, because the whole
// reason --autocams asks for MJPG is that three 1280x720 cameras do not fit uncompressed
// on one USB bus, and opening six of them at once to choose three would be the same
// mistake in a new place.
//
// The costs, said out loud. It is slower than trusting an index: one open, a few frames
// and one calibration per device on the machine, and the cameras that are kept are then
// opened and calibrated a second time by the Scorer. And it answers with what it can see
// NOW -- a board camera whose lens cap is on, or whose room is dark, is refused here
// exactly as a webcam is, which is the same answer #892's fault vigil would have given
// one beat later and is said at start, by name, instead.
namespace board_cameras
{
    /** One candidate's turn under the probe. */
    struct Probed
    {
        std::string source;
        bool opened = false;
        bool sees_board = false;
        std::string refusal;
        board_look::Evidence look;
    };

    /**
     * The devices to look through, in the order the platform indexes them. OD_CAM_CANDIDATES
     * names them instead when it is set -- a comma-separated list, file paths included, which
     * is how the probe is exercised on a machine with no cameras and on mock footage.
     */
    inline std::vector<std::string> candidates()
    {
        const char *named = std::getenv("OD_CAM_CANDIDATES");
        if (named && *named)
        {
            std::vector<std::string> sources;
            std::string item;
            for (const char *c = named;; c++)
            {
                if (*c == ',' || *c == '\0')
                {
                    if (!item.empty())
                        sources.push_back(item);
                    item.clear();
                    if (*c == '\0')
                        break;
                }
                else
                {
                    item += *c;
                }
            }
            return sources;
        }
        return autocam::listCandidates();
    }

    /** Open one device on its own, average a few frames, calibrate, and decide. */
    inline Probed probe(const std::string &source, int width, int height, int fps, int frames)
    {
        Probed result;
        result.source = source;

        std::unique_ptr<camera::CaptureSource> capture = camera::makeCaptureSource();
        if (!capture->open({source}, width, height, fps))
        {
            result.refusal = "will not open";
            return result;
        }
        result.opened = true;

        std::vector<camera::Frame> averaged = capture->readAveraged(frames);
        if (averaged.empty() || averaged[0].empty())
        {
            result.refusal = "opened but produced no frame";
            return result;
        }

        // The real calibration, not a cheaper imitation of it: the numbers this is
        // decided on are the ones the board will be scored with, so a camera that is
        // accepted here is a camera that has already calibrated once.
        DartboardCalibration calibration =
            geometry_calibration::calibrateSingleCamera(averaged[0].image, 0, false);
        result.look = calibration.look;
        result.sees_board = calibration.sees_board;
        result.refusal = board_look::refusal(calibration.look);
        return result;
    }

    /**
     * Up to `want` cameras that can see the dartboard, chosen by looking. Every candidate
     * is reported by name, kept or refused; the empty vector means nothing on this machine
     * can see a board, and the caller decides what to do about that.
     */
    inline std::vector<std::string> choose(int want, int width, int height, int fps, int frames = 8)
    {
        const std::vector<std::string> devices = candidates();
        std::vector<std::string> chosen;

        if (devices.empty())
        {
            log_error("CAMERAS: no video devices found on this machine");
            return chosen;
        }

        log_info("CAMERAS: looking through " + std::to_string(devices.size()) +
                 " video device(s) for the dartboard");

#ifdef _WIN32
        // A Windows board with exactly the three devices it needs has no alternative
        // source for this probe to distinguish.  Opening one MSMF reader at a time here
        // opens, configures, calibrates and tears down all three, only for Scorer to
        // open the same trio again.  On the live board that churn leaves the final
        // reader waiting indefinitely even though 0,1,2 open and calibrate together.
        // Open the known trio once instead; Scorer's normal calibration still refuses
        // any slot that cannot see the board, so this is not treating an opened camera
        // as a calibrated one.
        if ((int)devices.size() == want)
        {
            log_info("CAMERAS: exactly " + std::to_string(want) +
                     " Windows video devices are present; opening them together instead of "
                     "probing and reopening each Media Foundation reader.");
            return devices;
        }
#endif

        for (const std::string &source : devices)
        {
            Probed probed = probe(source, width, height, fps, frames);
            if (probed.sees_board)
            {
                log_info("CAMERA " + source + ": sees the dartboard (" + board_look::measured(probed.look) + ")");
                chosen.push_back(source);
                if ((int)chosen.size() == want)
                    break;
            }
            else
            {
                log_warning("CAMERA " + source + ": not used - " + probed.refusal);
            }
        }

        return chosen;
    }
}
