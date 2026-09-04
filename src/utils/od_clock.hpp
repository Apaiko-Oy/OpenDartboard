#pragma once
// #811 measurement instrument. Not a proposed design; see the document.
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <string>

namespace od_clock
{
    enum class Mode
    {
        Wall,
        Cycle,
        Capture
    };

    inline Mode mode()
    {
        static Mode m = []
        {
            const char *e = std::getenv("OD_MOTION_CLOCK");
            std::string s = e ? e : "wall";
            if (s == "cycle")
                return Mode::Cycle;
            if (s == "capture")
                return Mode::Capture;
            return Mode::Wall;
        }();
        return m;
    }

    inline const char *mode_name()
    {
        switch (mode())
        {
        case Mode::Cycle:
            return "cycle";
        case Mode::Capture:
            return "capture";
        default:
            return "wall";
        }
    }

    inline std::atomic<long long> &capture_ms()
    {
        static std::atomic<long long> v{0};
        return v;
    }
    inline std::atomic<long long> &cycles()
    {
        static std::atomic<long long> v{0};
        return v;
    }
    inline double &frame_period_ms()
    {
        static double p = 1000.0 / 15.0;
        return p;
    }

    // #815: which of the two branches behind the one warning string actually fired.
    // index 0 = max_event_duration_ms safety timeout, 1 = spike window.
    inline std::atomic<long long> &timeouts(int which)
    {
        static std::atomic<long long> safety{0};
        static std::atomic<long long> window{0};
        return which == 0 ? safety : window;
    }

    inline long long wall_ms()
    {
        static auto t0 = std::chrono::steady_clock::now();
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now() - t0)
            .count();
    }

    // The clock the motion state machine is judged on.
    inline long long now_ms()
    {
        switch (mode())
        {
        case Mode::Cycle:
            return (long long)(cycles().load() * frame_period_ms());
        case Mode::Capture:
            return capture_ms().load();
        default:
            return wall_ms();
        }
    }
}
