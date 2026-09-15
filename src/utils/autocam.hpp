// utils/autocam.hpp  (updated)
#pragma once
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>
#include <cstdio>
#include <tuple>

#ifdef _WIN32

// --------------------------------------------------------------------------
// Windows: there is no analogue of the V4L2 middle step. V4L2 lets a second
// file descriptor set a device's format out of band with VIDIOC_S_FMT before
// OpenCV ever opens it; Windows has no such thing. The equivalent is to ask the
// capture session for the media type AT open, which means the negotiation and
// the opening are the same act.
//
// So --autocams here is: enumerate the video sources with Media Foundation,
// open each through OpenCV's MSMF backend, ask for MJPG at the wanted mode,
// keep the ones that accept it, and REPORT what each actually got — because a
// camera that quietly handed back YUY2 is the USB bandwidth failure arriving
// disguised as a timing one, and on Windows nothing else will say so.
//
// The device it returns is an index, not a path. Media Foundation has no
// filesystem name for a camera and OpenCV's MSMF backend opens by index.
// --------------------------------------------------------------------------

// windows.h and the Media Foundation headers arrive through od_platform_first.hpp,
// which MSVC force-includes into every translation unit. Including them here as
// well would be harmless but would also make this file look like the place the
// order is decided, and it is not.
#include "od_platform_first.hpp"
#include <opencv2/opencv.hpp>

namespace autocam
{
    struct WindowsDevice
    {
        int index = -1;
        std::string name;
        // #1258: Media Foundation's symbolic link, the identity a remembered camera is
        // found by when its index has moved. Empty when the source publishes none.
        std::string id;
    };

    inline std::string narrow(const WCHAR *wide, UINT32 length)
    {
        int bytes = ::WideCharToMultiByte(CP_UTF8, 0, wide, (int)length, nullptr, 0, nullptr, nullptr);
        std::string text(bytes > 0 ? bytes : 0, '\0');
        if (bytes > 0)
            ::WideCharToMultiByte(CP_UTF8, 0, wide, (int)length, &text[0], bytes, nullptr, nullptr);
        return text;
    }

    // The friendly names Media Foundation publishes, in MF's own order — which is
    // the order OpenCV's MSMF backend indexes them in.
    inline std::vector<WindowsDevice> enumerateDevices()
    {
        std::vector<WindowsDevice> devices;

        HRESULT hr = ::MFStartup(MF_VERSION, MFSTARTUP_NOSOCKET);
        const bool started = SUCCEEDED(hr);

        IMFAttributes *attributes = nullptr;
        if (SUCCEEDED(::MFCreateAttributes(&attributes, 1)))
        {
            attributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
                                MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);

            IMFActivate **found = nullptr;
            UINT32 count = 0;
            if (SUCCEEDED(::MFEnumDeviceSources(attributes, &found, &count)))
            {
                for (UINT32 i = 0; i < count; i++)
                {
                    WindowsDevice device;
                    device.index = (int)i;

                    WCHAR *wide = nullptr;
                    UINT32 length = 0;
                    if (SUCCEEDED(found[i]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &wide, &length)))
                    {
                        device.name = narrow(wide, length);
                        ::CoTaskMemFree(wide);
                    }
                    wide = nullptr;
                    length = 0;
                    if (SUCCEEDED(found[i]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK, &wide, &length)))
                    {
                        device.id = narrow(wide, length);
                        ::CoTaskMemFree(wide);
                    }
                    devices.push_back(device);
                    found[i]->Release();
                }
                if (found)
                    ::CoTaskMemFree(found);
            }
            attributes->Release();
        }

        if (started)
            ::MFShutdown();
        return devices;
    }

    // What one device ended up being, after the only negotiation Windows offers.
    struct WindowsMode
    {
        bool opened = false;
        int width = 0;
        int height = 0;
        int fps = 0;
        std::string fourcc;
    };

    inline std::string fourccToString(int code)
    {
        char text[5];
        text[0] = (char)(code & 0xFF);
        text[1] = (char)((code >> 8) & 0xFF);
        text[2] = (char)((code >> 16) & 0xFF);
        text[3] = (char)((code >> 24) & 0xFF);
        text[4] = 0;
        return std::string(text);
    }

    inline WindowsMode probe(int index, int width, int height, int fps)
    {
        WindowsMode mode;
        cv::VideoCapture capture;
        if (!capture.open(index, cv::CAP_MSMF))
            return mode;

        capture.set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('M', 'J', 'P', 'G'));
        capture.set(cv::CAP_PROP_FRAME_WIDTH, width);
        capture.set(cv::CAP_PROP_FRAME_HEIGHT, height);
        capture.set(cv::CAP_PROP_FPS, fps);

        mode.opened = true;
        mode.width = (int)capture.get(cv::CAP_PROP_FRAME_WIDTH);
        mode.height = (int)capture.get(cv::CAP_PROP_FRAME_HEIGHT);
        mode.fps = (int)capture.get(cv::CAP_PROP_FPS);
        mode.fourcc = fourccToString((int)capture.get(cv::CAP_PROP_FOURCC));
        capture.release();
        return mode;
    }

    inline std::vector<std::string>
    detectAndLock(int maxCams, int width, int height, int fps, bool verbose = true)
    {
        std::vector<std::string> found;
        std::vector<WindowsDevice> devices = enumerateDevices();

        if (verbose)
            std::fprintf(stderr, "Enumerating Media Foundation video sources...\n");

        for (const auto &device : devices)
        {
            WindowsMode mode = probe(device.index, width, height, fps);
            if (verbose)
            {
                std::string what = mode.opened
                                       ? (std::to_string(mode.width) + "x" + std::to_string(mode.height) +
                                          " @ " + std::to_string(mode.fps) + " fps " + mode.fourcc)
                                       : std::string("could not be opened through MSMF");
                std::fprintf(stderr, " %s [%d] %s: %s\n",
                             mode.opened ? "-" : "x",
                             device.index, device.name.c_str(), what.c_str());
            }
            if (!mode.opened)
                continue;

            // The check V4L2 made with VIDIOC_S_FMT, made here against what the open
            // actually granted. A camera that fell back to YUY2 is refused rather than
            // accepted quietly, because three of those do not fit on one USB bus.
            if (mode.fourcc != "MJPG")
            {
                if (verbose)
                    std::fprintf(stderr, "   rejected: negotiated %s, not MJPG\n", mode.fourcc.c_str());
                continue;
            }
            if (mode.width != width || mode.height != height)
            {
                if (verbose)
                    std::fprintf(stderr, "   rejected: %dx%d, asked for %dx%d\n", mode.width, mode.height, width, height);
                continue;
            }

            found.push_back(std::to_string(device.index));
            if ((int)found.size() == maxCams)
                break;
        }

        if ((int)found.size() < maxCams)
            throw std::runtime_error("[autocam] only " + std::to_string(found.size()) +
                                     " fully-compatible cameras detected; need " + std::to_string(maxCams) +
                                     " (" + std::to_string(devices.size()) + " video source(s) enumerated)");
        return found;
    }

} // namespace autocam

#else

#include <dirent.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace autocam
{
    inline bool isVideoCapture(const std::string &dev)
    {
        int fd = ::open(dev.c_str(), O_RDWR | O_NONBLOCK);
        if (fd < 0)
            return false;

        v4l2_capability cap{};
        bool ok = (::ioctl(fd, VIDIOC_QUERYCAP, &cap) == 0) &&
                  (cap.device_caps & V4L2_CAP_VIDEO_CAPTURE) &&
                  (cap.device_caps & V4L2_CAP_STREAMING);
        ::close(fd);
        return ok;
    }

    inline std::vector<std::tuple<int, int, int>>
    listMjpgModes(int fd)
    {
        std::vector<std::tuple<int, int, int>> modes;

        v4l2_fmtdesc fmtDesc{};
        fmtDesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        while (::ioctl(fd, VIDIOC_ENUM_FMT, &fmtDesc) == 0)
        {
            if (fmtDesc.pixelformat == V4L2_PIX_FMT_MJPEG)
            {
                v4l2_frmsizeenum fs{};
                fs.pixel_format = fmtDesc.pixelformat;
                while (::ioctl(fd, VIDIOC_ENUM_FRAMESIZES, &fs) == 0)
                {
                    if (fs.type == V4L2_FRMSIZE_TYPE_DISCRETE)
                    {
                        int w = fs.discrete.width;
                        int h = fs.discrete.height;

                        v4l2_frmivalenum fi{};
                        fi.pixel_format = fmtDesc.pixelformat;
                        fi.width = w;
                        fi.height = h;
                        while (::ioctl(fd, VIDIOC_ENUM_FRAMEINTERVALS, &fi) == 0)
                        {
                            if (fi.type == V4L2_FRMIVAL_TYPE_DISCRETE)
                            {
                                int fps = fi.discrete.denominator / fi.discrete.numerator;
                                modes.emplace_back(w, h, fps);
                            }
                            fi.index++;
                        }
                    }
                    fs.index++;
                }
            }
            fmtDesc.index++;
        }
        return modes;
    }

    inline bool supportsMjpg(int fd, int w, int h, int fps)
    {
        auto modes = listMjpgModes(fd);
        for (auto &m : modes)
            if (std::get<0>(m) == w && std::get<1>(m) == h && std::get<2>(m) == fps)
                return true;
        return false;
    }

    inline bool configureCam(const std::string &dev, int w, int h, int fps, bool verbose)
    {
        int fd = ::open(dev.c_str(), O_RDWR);
        if (fd < 0)
            return false;

        if (!supportsMjpg(fd, w, h, fps))
        {
            if (verbose)
            {
                std::fprintf(stderr,
                             "[autocam] %s does NOT support %dx%d@%d MJPG. Supported modes:\n",
                             dev.c_str(), w, h, fps);
                for (auto &m : listMjpgModes(fd))
                    std::fprintf(stderr, "  %dx%d @ %d FPS\n",
                                 std::get<0>(m), std::get<1>(m), std::get<2>(m));
            }
            ::close(fd);
            return false;
        }

        v4l2_format fmt{};
        fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        fmt.fmt.pix.width = w;
        fmt.fmt.pix.height = h;
        fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
        fmt.fmt.pix.field = V4L2_FIELD_ANY;
        if (::ioctl(fd, VIDIOC_S_FMT, &fmt) < 0)
        {
            ::close(fd);
            return false;
        }

        v4l2_streamparm parm{};
        parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        parm.parm.capture.timeperframe = {1, static_cast<unsigned int>(fps)};
        ::ioctl(fd, VIDIOC_S_PARM, &parm);
        ::close(fd);
        return true;
    }

    inline std::tuple<int, int, int> getActualSettings(const std::string &dev)
    {
        int fd = ::open(dev.c_str(), O_RDWR);
        if (fd < 0)
            return {0, 0, 0};

        v4l2_format fmt{};
        fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (::ioctl(fd, VIDIOC_G_FMT, &fmt) < 0)
        {
            ::close(fd);
            return {0, 0, 0};
        }

        v4l2_streamparm parm{};
        parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        ::ioctl(fd, VIDIOC_G_PARM, &parm);

        int actualFps = parm.parm.capture.timeperframe.denominator /
                        parm.parm.capture.timeperframe.numerator;

        ::close(fd);
        return {fmt.fmt.pix.width, fmt.fmt.pix.height, actualFps};
    }

    inline std::vector<std::string>
    detectAndLock(int maxCams, int width, int height, int fps, bool verbose = true)
    {
        std::vector<std::string> found;
        DIR *d = ::opendir("/dev");
        if (!d)
            throw std::runtime_error("opendir /dev failed");

        if (verbose)
            std::fprintf(stderr, "Scanning /dev for video devices...\n");

        struct dirent *e;
        while ((e = ::readdir(d)))
        {
            if (std::strncmp(e->d_name, "video", 5) == 0)
            {
                std::string dev = "/dev/" + std::string(e->d_name);

                if (!isVideoCapture(dev))
                {
                    continue;
                }
                if (configureCam(dev, width, height, fps, verbose))
                {
                    found.push_back(dev);
                    if ((int)found.size() == maxCams)
                        break;
                }
            }
        }
        ::closedir(d);
        std::sort(found.begin(), found.end());

        if (verbose)
        {
            std::fprintf(stderr, "Found %d compatible camera(s) with actual settings:\n", (int)found.size());
            for (const auto &cam : found)
            {
                auto [w, h, fps] = getActualSettings(cam);
                std::fprintf(stderr, " ─ %s: %dx%d @ %d FPS\n", cam.c_str(), w, h, fps);

                // Show supported modes grouped by resolution
                int fd = ::open(cam.c_str(), O_RDWR);
                if (fd >= 0)
                {
                    auto modes = listMjpgModes(fd);
                    std::map<std::pair<int, int>, std::vector<int>> resolutionMap;

                    // Group fps by resolution
                    for (const auto &mode : modes)
                    {
                        std::pair<int, int> res = {std::get<0>(mode), std::get<1>(mode)};
                        resolutionMap[res].push_back(std::get<2>(mode));
                    }

                    // Print grouped modes
                    for (const auto &[resolution, fpsValues] : resolutionMap)
                    {
                        std::fprintf(stderr, " ─── [%dx%d @ ", resolution.first, resolution.second);
                        for (size_t i = 0; i < fpsValues.size(); ++i)
                        {
                            if (i > 0)
                                std::fprintf(stderr, "/");
                            std::fprintf(stderr, "%dfps", fpsValues[i]);
                        }
                        std::fprintf(stderr, "]\n");
                    }
                    ::close(fd);
                }
            }
        }

        if ((int)found.size() < maxCams)
            throw std::runtime_error("[autocam] only " + std::to_string(found.size()) +
                                     " fully-compatible cameras detected; need " + std::to_string(maxCams));
        return found;
    }

} // namespace autocam

#endif // _WIN32
