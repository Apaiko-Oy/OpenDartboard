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

    // #1685: the detection windows (the motion settle, the exposure stillness history and
    // hold, the dart window) are lengths of the motion clock below, in milliseconds, so a
    // window is the same time on a board whose cycle takes 33 ms and on one whose cycle
    // takes 300. Until #1685 they were counts of cycles, and at 200 ms a cycle one dart's
    // event outlasted the ~2 s between darts, so two darts became one window (#1683).
    // OD_WINDOW_UNIT=cycles pins the counts (`windows_in_ms()` false).
    inline bool windows_in_ms()
    {
        static bool v = []
        {
            const char *e = std::getenv("OD_WINDOW_UNIT");
            return !(e && std::string(e) == "cycles");
        }();
        return v;
    }

    // #1685: is a window of `window_ms` reached, when the cycles in it add up to
    // `elapsed_ms` and the cycle being judged took `span_ms`? Reached once what is still
    // missing is less than half that cycle: the window ends on the cycle nearest its
    // length. At the rig's 33.3 ms a cycle N cycles add up to N x 33.3 ms (+-1 ms of the
    // footage's integer positions) and N-1 fall 16 ms short, so a window of N x 33.3 ms is
    // exactly the N cycles it was, on the capture clock by construction. A cycle longer
    // than the whole window reaches it alone: a window always holds the cycle it opened on.
    inline bool window_reached(long long elapsed_ms, long long span_ms, long long window_ms)
    {
        return 2 * elapsed_ms + span_ms >= 2 * window_ms;
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
