#pragma once
// turnaus#1886: a Windows board asks the system not to sleep while the detector runs.
//
// WHAT WENT WRONG. Device/2's log of 2026-10-17 jumps from 05:54:22.577 to 13:40:59.470
// inside one two-second camera-orientation step, and then finishes the step as if no time
// had passed. That is the machine asleep, not a slow detector. Nothing in this program
// told Windows otherwise, so the power plan was free to sleep a board PC mid-session; a
// sleeping board stops beating, its Station reads `silent`, the next darts go unscored,
// and #1797's 06:00 stop never happens because nothing runs at 06:00.
//
// WHAT IT DOES. SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED) on the thread
// that runs the scorer -- main's thread, which lives from before calibration to the last
// line of main -- and ES_CONTINUOUS alone when the Request is destroyed, which is also
// what Windows does by itself when that thread ends. ES_SYSTEM_REQUIRED only: the display
// may still turn off, and nothing asks for away mode. It does not stop a sleep somebody
// chooses (the Start menu, a lid, a power button) or an update's restart; those are a
// person's or the club's decision, and the log says the request was made, not that the
// machine cannot sleep.
//
// WHY THE DETECTOR AND NOT THE LAUNCHER. The detector is the process that must be awake to
// score, it is alive for the whole of a session however it was started (the launcher, a
// shortcut straight at opendartboard.exe, a tester's console), and its log is the one that
// is posted (#1787) -- so "said once in the log" is a line somebody can read back. The
// launcher prints nothing while a board works and keeps no log, and between two detectors
// it is busy for seconds, not hours. A request held by the launcher would also keep a PC
// awake while it sits on a dead board's "press Enter", which is nobody's session.
//
// THE PIN. OD_KEEP_AWAKE=off, exactly, leaves sleep to the machine's power plan -- for a
// club that runs its own. Anything else keeps the default, so a typo keeps the board
// awake, which is OD_SCHEDULED_RESTART's rule (#1797). Read once, said once.
//
// POSIX. Nothing. The Pi package (distributions/debian_arm64, templates/*.service.template)
// installs on Raspberry Pi OS Lite: no desktop, so no idle power manager, and logind's
// IdleAction ships as `ignore`. A packaged Pi does not suspend on its own, so there is
// nothing to inhibit; docs/rig.md says so.
//
// The decision and its sentences are pure and are what testers/i1886_check.cpp drives in
// the Linux container. The system call is the five lines under _WIN32 and cannot be run
// there; it compiles out.

#include <string>

namespace keep_awake
{
    /** What this start does about sleep. */
    enum class Decision
    {
        Ask,             // Windows, pin unset (or anything but "off"): ask not to sleep
        PinnedOff,       // Windows, OD_KEEP_AWAKE=off: leave it to the power plan
        NotThisPlatform, // POSIX: nothing to ask, nothing to say
    };

    /** The pin's name, one spelling for the code, the log and the documentation. */
    inline const char *pinName() { return "OD_KEEP_AWAKE"; }

    /** Pure: the pin's value as getenv answers it (nullptr when unset) and the platform. */
    inline Decision decide(const char *pin, bool windows)
    {
        if (!windows)
        {
            return Decision::NotThisPlatform;
        }
        if (pin != nullptr && std::string(pin) == "off")
        {
            return Decision::PinnedOff;
        }
        return Decision::Ask;
    }

    /** The one line said at start; empty when nothing is said. */
    inline std::string sentence(Decision decision)
    {
        switch (decision)
        {
        case Decision::Ask:
            return "I1886 KEEP AWAKE: this board asks Windows not to sleep while the detector runs "
                   "(the display may still turn off); OD_KEEP_AWAKE=off leaves it to the power plan";
        case Decision::PinnedOff:
            return "OD_KEEP_AWAKE=off is set: this board does not ask Windows to stay awake, so the "
                   "power plan may sleep it mid-session and its Station goes silent (#1886)";
        case Decision::NotThisPlatform:
            break;
        }
        return "";
    }

    /** Said instead of sentence(Ask) when Windows refused the request. */
    inline std::string refusedSentence()
    {
        return "I1886 KEEP AWAKE: Windows refused the request not to sleep (SetThreadExecutionState "
               "returned 0); the power plan may sleep this board mid-session";
    }

    /** Whether this build is the Windows one. */
    inline bool thisIsWindows()
    {
#ifdef _WIN32
        return true;
#else
        return false;
#endif
    }
} // namespace keep_awake

#ifdef _WIN32
#include <windows.h>
#endif
#include "logging.hpp"
#include <cstdlib>

namespace keep_awake
{
    /**
     * The request, held for as long as the object lives. Construct it on the thread that
     * runs the scorer, before the cameras calibrate, and let it go out of scope after the
     * Scorer: the request is per-thread and ends with the thread anyway.
     */
    class Request
    {
    public:
        Request()
        {
            const Decision decision = decide(std::getenv(pinName()), thisIsWindows());
            if (decision == Decision::PinnedOff)
            {
                log_warning(sentence(decision));
                return;
            }
            if (decision != Decision::Ask)
            {
                return;
            }
#ifdef _WIN32
            held_ = ::SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED) != 0;
#endif
            if (held_)
            {
                log_info(sentence(decision));
            }
            else
            {
                log_warning(refusedSentence());
            }
        }

        ~Request()
        {
#ifdef _WIN32
            if (held_)
            {
                ::SetThreadExecutionState(ES_CONTINUOUS);
            }
#endif
        }

        Request(const Request &) = delete;
        Request &operator=(const Request &) = delete;

        bool held() const { return held_; }

    private:
        bool held_ = false;
    };
} // namespace keep_awake
