#pragma once

#include "frame.hpp"
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

    // Simple function to decote fourcc code to a human-readable string
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
