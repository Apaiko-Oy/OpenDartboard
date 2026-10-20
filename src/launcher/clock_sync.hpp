#pragma once
// #1900: a board that is not yet on the clock waits for it, bounded, before it looks.
//
// MEASURED ON A PI, 2026-10-20. A Pi has no battery clock. At the boot the issue records,
// fake-hwclock had restored 2026-10-19 23:17 while it was 12:21, timesyncd's first sync
// came a few seconds AFTER the launcher's look, and the look's artefact request was refused
// (HTTP 403). Whether or not the clock caused that refusal, a signed manifest's
// `minimumLauncher` and any later expiry field are read against a clock that may be a day
// wrong, so the launcher gives the clock a short, bounded chance first.
//
// THE FLAG IS THE ONE systemd-timesyncd KEEPS: the file `synchronized` in its runtime
// directory, /run/systemd/timesync, which it touches when the kernel clock is first
// synchronised (the same fact `timedatectl` reads as NTPSynchronized). The rule:
//
//   the directory is not there   the flag cannot be read -- Windows, a container, a board
//                                running chrony or no time daemon at all -- so NO WAIT,
//                                and nothing is said: this launcher cannot know.
//   the file is there            synchronised; no wait.
//   the directory is, the file   timesyncd is running and has not synchronised yet: poll
//   is not                       once a second for up to kClockSyncWaitSeconds.
//
// OD_TIMESYNC_DIR replaces the directory, FOR A HARNESS: testers/i1900_check.sh points it
// at a directory of its own and touches the file when it chooses. Set to an empty string
// it turns the wait off. A board at a venue has it unset.

#include "../utils/console_prompt.hpp"
#include "apply_update.hpp"

#include <cstdlib>
#include <string>

#ifndef _WIN32
#include <sys/stat.h>
#endif

namespace launcher
{
    const int kClockSyncWaitSeconds = 30;

    /** What the clock was when a look began. */
    struct ClockAtLook
    {
        bool readable = false;     // the flag could be read at all
        bool synchronised = false; // and it said so when the look began
        int waited_s = 0;          // seconds spent waiting for it
    };

    /** Where timesyncd keeps its flag on this board; empty where there is no such thing. */
    inline std::string timesyncDirectory()
    {
        const char *pinned = std::getenv("OD_TIMESYNC_DIR");
        if (pinned != NULL)
        {
            return std::string(pinned);
        }
#ifdef _WIN32
        return std::string();
#else
        return "/run/systemd/timesync";
#endif
    }

    inline bool isADirectory(const std::string &path)
    {
#ifdef _WIN32
        (void)path;
        return false; // never asked: timesyncDirectory() is empty on Windows unless pinned
#else
        struct stat info;
        return ::stat(path.c_str(), &info) == 0 && S_ISDIR(info.st_mode);
#endif
    }

    inline bool flagIsSet(const std::string &directory)
    {
#ifdef _WIN32
        (void)directory;
        return false;
#else
        struct stat info;
        return ::stat((directory + "/synchronized").c_str(), &info) == 0;
#endif
    }

    /** The bounded wait, by the rule at the top of this file. `wait` is asked one second at a time. */
    inline ClockAtLook waitForTheClock(const std::string &directory, const Wait &wait,
                                       int bound = kClockSyncWaitSeconds)
    {
        ClockAtLook clock;
        if (directory.empty() || !isADirectory(directory))
        {
            return clock;
        }
        clock.readable = true;
        for (;;)
        {
            if (flagIsSet(directory))
            {
                clock.synchronised = true;
                return clock;
            }
            if (clock.waited_s >= bound)
            {
                return clock;
            }
            if (wait)
            {
                wait(1);
            }
            else
            {
                waitForReal(1);
            }
            clock.waited_s++;
        }
    }

    /**
     * The sentence about the clock. Said whenever the launcher WAITED, and otherwise only
     * when the look goes on to fetch something -- a synchronised clock on a board with
     * nothing to install is not news, and this launcher prints nothing it does not have to.
     */
    inline Text clockText(const ClockAtLook &clock)
    {
        if (clock.synchronised && clock.waited_s == 0)
        {
            return {"Kello oli synkronoitu, kun päivitysten katsominen alkoi.",
                    "The clock was synchronised when the look for updates began."};
        }
        if (clock.synchronised)
        {
            return {"Kello ei ollut vielä synkronoitu; se synkronoitui " + std::to_string(clock.waited_s) +
                        " sekunnissa, ennen kuin päivitysten katsominen alkoi.",
                    "The clock was not yet synchronised; it was after " + std::to_string(clock.waited_s) +
                        " s, before the look for updates began."};
        }
        return {"Kello ei synkronoitunut " + std::to_string(clock.waited_s) +
                    " sekunnissa, joten päivityksiä katsotaan kellolla, joka voi olla väärässä.",
                "The clock was not synchronised after " + std::to_string(clock.waited_s) +
                    " s, so the look for updates begins on a clock that may be wrong."};
    }
}
