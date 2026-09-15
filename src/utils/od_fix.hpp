#pragma once
// #815 measurement instrument. One binary, the three fixes selected at run time,
// so that "different build" is never a confound. Not a proposed design.
#include <cstdlib>
#include <string>

namespace od_fix
{
    // #816: `member` ignores the "all" shorthand. #815's `all` names its own three
    // fixes and must go on naming exactly those, or its eight runs stop being
    // reproducible from this binary.
    inline bool member(const char *name)
    {
        const char *e = std::getenv("OD_MOTION_FIX");
        if (!e)
            return false;
        std::string s = e;
        std::string n = name;
        size_t p = 0;
        while ((p = s.find(n, p)) != std::string::npos)
        {
            bool left = (p == 0) || s[p - 1] == ',';
            size_t q = p + n.size();
            bool right = (q == s.size()) || s[q] == ',';
            if (left && right)
                return true;
            p = q;
        }
        return false;
    }

    inline bool has(const char *name)
    {
        const char *e = std::getenv("OD_MOTION_FIX");
        if (e && std::string(e) == "all")
            return true;
        return member(name);
    }

    inline bool stability()
    {
        static bool v = has("stability");
        return v;
    }
    inline bool spikewin()
    {
        static bool v = has("spikewin");
        return v;
    }
    inline bool warnsplit()
    {
        static bool v = has("warnsplit");
        return v;
    }
    // #816: max_event_duration_ms tested in STABILIZING as well as in SPIKE_DETECTED.
    inline bool safety()
    {
        static bool v = member("safety");
        return v;
    }

    // #816: the shutdown race in WebSocketService, behind its own variable because it
    // is not a motion fix and must be selectable on a run that changes nothing else.
    //
    // #825: on by default now, and the reason is #825 itself. #816 measured the double
    // stop on a path production never took -- the only way an unpatched build ever left
    // Scorer::run() was the cycle budget this harness added. Giving the program a real
    // exit path makes ~Scorer, and therefore both stop() call sites, run on every
    // ordinary shutdown, so the assertion #816 measured at 2-in-8 under load moves from
    // a harness curiosity to something an operator meets. The two are one change here.
    // OD_SHUTDOWN_FIX=0 is the hatch, so #816's 26 runs can still be reproduced.
    inline bool shutdownFix()
    {
        static bool v = []
        {
            const char *e = std::getenv("OD_SHUTDOWN_FIX");
            return !(e && std::string(e) == "0");
        }();
        return v;
    }

    // #816: the break of #815 defect 1 moves the whole of an event's life into
    // STABILIZING, which is the one state max_event_duration_ms is not tested in.
    // So this build refuses to run `stability` without `safety`. The hatch exists
    // only so #815's own eight runs can be reproduced byte for byte.
    inline bool unguardedBreakAllowed()
    {
        const char *e = std::getenv("OD_UNGUARDED_BREAK");
        return e && std::string(e) == "1";
    }
    // #816: log every server_->stop() call site with the state httplib asserts on,
    // so the window can be measured on runs that do not happen to crash.
    inline bool shutdownTrace()
    {
        static bool v = []
        {
            const char *e = std::getenv("OD_SHUTDOWN_TRACE");
            return e && std::string(e) == "1";
        }();
        return v;
    }

    inline bool breakIsUnguarded()
    {
        return stability() && !safety() && !unguardedBreakAllowed();
    }

    inline const char *selected()
    {
        const char *e = std::getenv("OD_MOTION_FIX");
        return e ? e : "(none)";
    }

    // Where the frame period comes from: the --fps flag (default) or the stream's own
    // CAP_PROP_FPS. Only read when spikewin() is on.
    inline bool fpsFromStream()
    {
        static bool v = []
        {
            const char *e = std::getenv("OD_FPS_SOURCE");
            return e && std::string(e) == "stream";
        }();
        return v;
    }
}
