#pragma once
// #1258: the Windows half of choosing the cameras -- what camera_choice.hpp is handed.
//
// It lists Media Foundation's video sources by friendly name with their symbolic links,
// tries opening one through the same backend the capture layer uses, reads and writes
// the remembered choice beside the credential, and says what it decided in the log.
// Linux is untouched: there is no question there, and this file is not included.

#ifdef _WIN32

#include "autocam.hpp"
#include "camera_choice.hpp"
#include "capture.hpp"
#include "console_prompt.hpp"
#include "logging.hpp"

#include <opencv2/opencv.hpp>
#include <chrono>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

namespace camera_setup
{
    /**
     * ---- #1258 instrumentation, the OD_BLIND_AFTER shape, and equally NOT a feature. ----
     * OD_EXTRA_SOURCES appends named video files to the list Media Foundation gives, as
     * `name=path;name=path`, with the identity `file:<path>`. It exists because the
     * machine this was measured on has one video source, a virtual one, and the question
     * is about choosing three. A person at a board never sets it.
     */
    inline void appendExtraSources(std::vector<camera_choice::Source> &sources)
    {
        const char *raw = std::getenv("OD_EXTRA_SOURCES");
        if (!raw || !*raw)
        {
            return;
        }
        std::string all(raw);
        size_t start = 0;
        while (start <= all.size())
        {
            size_t end = all.find(';', start);
            std::string entry = all.substr(start, end == std::string::npos ? std::string::npos : end - start);
            size_t eq = entry.find('=');
            if (eq != std::string::npos && eq > 0 && eq + 1 < entry.size())
            {
                std::string path = entry.substr(eq + 1);
                sources.push_back(camera_choice::Source{entry.substr(0, eq), "file:" + path, path});
            }
            if (end == std::string::npos)
            {
                break;
            }
            start = end + 1;
        }
    }

    inline std::vector<camera_choice::Source> listSources()
    {
        std::vector<camera_choice::Source> sources;
        for (const autocam::WindowsDevice &device : autocam::enumerateDevices())
        {
            sources.push_back(camera_choice::Source{device.name.empty() ? "(no name)" : device.name, device.id,
                                                    std::to_string(device.index)});
        }
        appendExtraSources(sources);
        return sources;
    }

    /** Open once at the asked mode, read back what was granted, release. */
    inline camera_choice::Opened openOnce(const camera_choice::Source &source, int width, int height, int fps)
    {
        camera_choice::Opened opened;
        cv::VideoCapture capture;
        // The capture layer's own test for "an index" (capture_opencv.hpp's deviceIndexOf),
        // repeated rather than included, so main.cpp does not take in that header's statics.
        const bool is_index = !source.open.empty() &&
                              source.open.find_first_not_of("0123456789") == std::string::npos;
        if (camera::isVideoFile(source.open))
        {
            capture.open(source.open);
        }
        else if (is_index)
        {
            capture.open(std::atoi(source.open.c_str()), cv::CAP_MSMF);
            if (capture.isOpened())
            {
                capture.set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('M', 'J', 'P', 'G'));
                capture.set(cv::CAP_PROP_FRAME_WIDTH, width);
                capture.set(cv::CAP_PROP_FRAME_HEIGHT, height);
                capture.set(cv::CAP_PROP_FPS, fps);
            }
        }
        else
        {
            capture.open(source.open, cv::CAP_MSMF);
        }
        if (!capture.isOpened())
        {
            return opened;
        }
        opened.ok = true;
        opened.width = (int)capture.get(cv::CAP_PROP_FRAME_WIDTH);
        opened.height = (int)capture.get(cv::CAP_PROP_FRAME_HEIGHT);
        capture.release();
        return opened;
    }

    /**
     * The cameras this start opens, when --cams was not given and --autocams was not
     * asked for. `credentials_path` names the directory the choice is kept in.
     */
    inline std::vector<std::string> camerasAtStart(const std::string &credentials_path, int width, int height, int fps)
    {
        const std::vector<std::string> defaults = {"0", "1", "2"};
        const bool interactive = console_prompt::isInteractiveConsole();
        const std::string path = camera_choice::fileBeside(credentials_path);

        std::vector<camera_choice::Remembered> remembered;
        const bool have_remembered = camera_choice::load(path, remembered);

        camera_choice::Hardware hardware;
        hardware.list = listSources;
        hardware.open = [&](const camera_choice::Source &s) { return openOnce(s, width, height, fps); };
        hardware.wait = [] { std::this_thread::sleep_for(std::chrono::seconds(2)); };

        console_prompt::StdConsole console;
        camera_choice::Outcome outcome =
            camera_choice::choose(interactive, defaults, have_remembered, remembered, console, hardware);

        if (outcome.chosen)
        {
            if (camera_choice::save(path, outcome.remember))
            {
                console.say({"Valinta tallennettu: " + path + ". Seuraava käynnistys käyttää näitä kameroita kysymättä.",
                             "The choice is saved to " + path + ". The next start uses these cameras without asking."});
            }
            else
            {
                console.say({"Valintaa ei voitu tallentaa: " + path + ". Seuraava käynnistys kysyy uudelleen.",
                             "The choice could not be saved to " + path + ". The next start will ask again."});
            }
        }

        std::string how = outcome.chosen            ? "chosen at the console"
                          : outcome.from_remembered ? "the remembered choice in " + path
                          : outcome.asked           ? "the defaults; the question was not answered"
                          : interactive             ? "the defaults, which opened"
                                                    : "the defaults; not an interactive console, so nothing was asked";
        std::string list;
        for (size_t i = 0; i < outcome.cams.size(); i++)
        {
            list += (i ? "," : "") + outcome.cams[i];
        }
        log_info("CAMERAS: " + list + " (" + how + ")");
        return outcome.cams;
    }
}

#endif
