#pragma once
// #815 measurement instrument. One binary, the three fixes selected at run time,
// so that "different build" is never a confound. Not a proposed design.
#include <cstdlib>
#include <string>

namespace od_fix
{
    inline bool has(const char *name)
    {
        const char *e = std::getenv("OD_MOTION_FIX");
        if (!e)
            return false;
        std::string s = e;
        if (s == "all")
            return true;
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
