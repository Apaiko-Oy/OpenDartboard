#pragma once

// #1686: where a detection cycle's time goes.
//
// OD_CYCLE_COST=on times the stages of one scoring cycle -- the read, each piece of the
// motion diff, the dart window's accumulation and its close, the scoring -- and prints
// one `I1686COST` line per cycle with the milliseconds each took (testers/i1686_cost.py
// tables them). Off, which is the default, a stage costs one test of a cached bool and
// nothing is printed, so a default log does not grow and no figure moves.
//
// The timers are steady_clock reads, about 25 ns each, some thirty a cycle: well under
// a hundredth of a millisecond against cycles of 20 ms and more. The line itself is one
// log call per cycle.

#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <time.h>

namespace cycle_cost
{
    enum Stage
    {
        READ,         // capture->read(): decode on the capture clock, the wait under real time
        M_GRAY,       // motion: two BGR->gray conversions per camera
        M_BLUR,       // motion: two 5x5 Gaussian blurs per camera
        M_DIFF,       // motion: absdiff + threshold
        M_MORPH,      // motion: the morphological close
        M_REGION,     // motion: board mask, changed-pixel count, board level
        M_CLONE,      // motion: keeping this cycle's frames as the next cycle's previous
        M_STATE,      // motion: the state machine
        D_ACCUM,      // dart window: gray + float + add, per collecting cycle
        D_CUMUL,      // dart close: the cumulative diff and its cleanup
        D_FRESH,      // dart close: the fresh diff against the working background, and its cleanup
        D_AXIS,       // dart close: tip and shaft-axis fits
        D_CLOSE,      // dart close: all of it (includes the three above)
        SCORE,        // score_processing::processScore
        PROCESS,      // detector->process() as a whole
        N_STAGES
    };

    inline const char *name(int s)
    {
        static const char *names[N_STAGES] = {"read", "m_gray", "m_blur", "m_diff", "m_morph", "m_region",
                                              "m_clone", "m_state", "d_accum", "d_cumul", "d_fresh", "d_axis",
                                              "d_close", "score", "process"};
        return names[s];
    }

    // OD_CYCLE_COST=on reads the wall clock; =cpu reads each thread's own CPU time, so a
    // stage is not charged for the time another thread (a decoder, or the container's CPU
    // quota) kept it off the processor.
    inline int mode()
    {
        static const int v = []
        {
            const char *e = std::getenv("OD_CYCLE_COST");
            if (!e)
                return 0;
            if (std::strcmp(e, "on") == 0)
                return 1;
            if (std::strcmp(e, "cpu") == 0)
                return 2;
            return 0;
        }();
        return v;
    }
    inline bool on() { return mode() != 0; }

    inline std::array<double, N_STAGES> &acc()
    {
        static std::array<double, N_STAGES> a{};
        return a;
    }

    // Decoding done off the loop's thread (the real-time replay's feed threads), in
    // microseconds since the last line.
    inline std::atomic<long long> &offloopDecodeUs()
    {
        static std::atomic<long long> v{0};
        return v;
    }

    inline double nowMs()
    {
        if (mode() == 2)
        {
            struct timespec ts;
            clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
            return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
        }
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    struct Scope
    {
        int stage;
        double t0;
        bool stopped = false;
        explicit Scope(int s) : stage(s), t0(on() ? nowMs() : 0.0) {}
        void stop()
        {
            if (!stopped && on())
                acc()[stage] += nowMs() - t0;
            stopped = true;
        }
        ~Scope() { stop(); }
    };

    // What the cycle was: the motion state it ended in (DartEventState's value) and the
    // dart window's part in it (0 none, 1 collecting, 2 closed and decided).
    inline int &motionState()
    {
        static int v = -1;
        return v;
    }
    inline int &windowPart()
    {
        static int v = 0;
        return v;
    }

    // The line, and the slots cleared for the next cycle. `tag` says what the cycle was:
    // the motion state it ended in, and whether a dart window collected or closed on it.
    inline std::string takeLine(long cycle, double loop_ms)
    {
        static const char *states[] = {"IDLE", "SPIKE", "STABILIZING", "END", "COOLDOWN"};
        static const char *windows[] = {"-", "collect", "close"};
        const int ms = motionState();
        const int wp = windowPart();
        std::string s = "I1686COST clock=" + std::string(mode() == 2 ? "cpu" : "wall") + " cycle=" + std::to_string(cycle) +
                        " state=" + (ms >= 0 && ms < 5 ? states[ms] : "?") +
                        " window=" + (wp >= 0 && wp < 3 ? windows[wp] : "?");
        motionState() = -1;
        windowPart() = 0;
        char buf[48];
        std::snprintf(buf, sizeof(buf), " loop=%.2f", loop_ms);
        s += buf;
        for (int i = 0; i < N_STAGES; i++)
        {
            std::snprintf(buf, sizeof(buf), " %s=%.2f", name(i), acc()[i]);
            s += buf;
            acc()[i] = 0.0;
        }
        std::snprintf(buf, sizeof(buf), " decode_bg=%.2f", offloopDecodeUs().exchange(0) / 1000.0);
        s += buf;
        // The whole process's CPU time since the previous line: every thread, the decoders'
        // own worker threads included, which the per-thread figures above cannot see.
        {
            static double last = -1.0;
            struct timespec ts;
            clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts);
            const double now = ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
            std::snprintf(buf, sizeof(buf), " proc_cpu=%.2f", last < 0 ? 0.0 : now - last);
            s += buf;
            last = now;
        }
        return s;
    }
}
