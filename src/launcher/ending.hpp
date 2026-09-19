#pragma once
// #1303: how a run of the detector ended, and what is said about it.
//
// WHY THIS IS ITS OWN HEADER, AND WHY IT HAS NO PLATFORM IN IT. The launcher exists for
// one moment: the one where the detector stops and the window closes on the line that
// mattered. Everything about that moment -- which of the three endings it was, what is
// said in Finnish and English, and what the launcher's own exit code then is -- is a
// decision over a handful of numbers. Kept here, free of <windows.h>, it can be compiled
// and RUN on Linux, which is the only place any of this repository's harnesses run
// (testers/i1303_check.sh). The platform half is detector_process.hpp and does nothing
// but fill in an Outcome.
//
// THREE ENDINGS, AND WINDOWS CAN ONLY TELL YOU TWO OF THEM APART. A process on Windows
// leaves one number behind and no record of how it came to leave it. So:
//
//   cleanly   exit code 0.
//   killed    STATUS_CONTROL_C_EXIT (0xC000013A) -- Ctrl+C, the window closed, a logoff.
//   faulted   everything else.
//
// `taskkill /F` leaves 1, and so does a program that returned 1, so a board ended that
// way is reported as faulted. That is a limit of the platform and not a thing to work
// around with a guess; the number is always printed, so a reader who knows they pressed
// End Task is not misled about anything but the word.
//
// POSIX really does tell them apart -- WIFSIGNALED, and then which signal -- and the
// Linux build uses that. The two platforms therefore disagree about `taskkill`-shaped
// endings and about nothing else, which is stated here rather than discovered.
//
// A NON-ZERO EXIT IS NOT ALWAYS A CRASH, and the words never say "crashed" without the
// number. #895's fault vigil means a board that cannot see does not exit at all, so the
// endings below are what happens when the detector really did stop.

#include "../utils/console_prompt.hpp"

#include <csignal>
#include <cstdio>
#include <string>
#include <vector>

namespace launcher
{
    using console_prompt::Text;

    /** STATUS_CONTROL_C_EXIT. Named here so the pure half needs no Windows header. */
    const unsigned long kControlCExit = 0xC000013AuL;

    /** How one run of the detector ended. */
    enum class Ending
    {
        Cleanly,      // it did what it was asked and returned 0
        Faulted,      // it stopped on its own and not well
        Killed,       // somebody or something stopped it
        NeverStarted, // the launcher could not start it at all
    };

    /**
     * What the platform observed. `started` false means no detector process ever existed,
     * and `detail` then carries what could not be started and why, verbatim.
     *
     * `signalled` is POSIX only and is always false on Windows, where there is no such
     * observation to make.
     */
    struct Outcome
    {
        bool started = false;
        unsigned long code = 0;
        bool signalled = false;
        int signal_number = 0;
        std::string detail;
    };

    /** A signal that means somebody stopped it, rather than that it broke. */
    inline bool isStoppingSignal(int number)
    {
        switch (number)
        {
#ifdef SIGHUP
        case SIGHUP:
#endif
#ifdef SIGKILL
        case SIGKILL:
#endif
#ifdef SIGQUIT
        case SIGQUIT:
#endif
        case SIGINT:
        case SIGTERM:
            return true;
        default:
            return false;
        }
    }

    /** The whole classification, over the numbers and nothing else. */
    inline Ending endingOf(const Outcome &outcome)
    {
        if (!outcome.started)
        {
            return Ending::NeverStarted;
        }
        if (outcome.signalled)
        {
            return isStoppingSignal(outcome.signal_number) ? Ending::Killed : Ending::Faulted;
        }
        if (outcome.code == 0)
        {
            return Ending::Cleanly;
        }
        if (outcome.code == kControlCExit)
        {
            return Ending::Killed;
        }
        return Ending::Faulted;
    }

    /**
     * The launcher's own exit code, so a scheduled start can be seen to have failed.
     *
     * Zero means one thing only: the detector ran and ended cleanly. The three failures
     * are three different numbers, because a scheduled task's history shows a number and
     * nothing else, and "could not start it" and "it crashed" are different mornings.
     * They are above 0 and below 128, and away from the small codes the detector itself
     * returns, so a reader of Task Scheduler is not looking at the child's number.
     */
    const int kRanCleanly = 0;
    const int kNeverStarted = 40;
    const int kFaulted = 41;
    const int kKilled = 42;

    inline int exitCodeFor(Ending ending)
    {
        switch (ending)
        {
        case Ending::Cleanly:
            return kRanCleanly;
        case Ending::NeverStarted:
            return kNeverStarted;
        case Ending::Killed:
            return kKilled;
        case Ending::Faulted:
        default:
            return kFaulted;
        }
    }

    /** True when the detector existed at all, whatever it then did. */
    inline bool detectorRan(Ending ending) { return ending != Ending::NeverStarted; }

    // ------------------------------------------------------------------ the words
    //
    // Two lines, Finnish then English, like every other thing this program says (#1258).
    // The word for the detector is `ohjelma` / `the program`, which is what
    // docs/board-tester-readme.md already calls it on both sides; a tester reading this
    // window has met that word and has not met "detector".

    /** A number as both a decimal and, when it looks like one, a Windows status code. */
    inline std::string codeAsText(unsigned long code)
    {
        char both[64];
        if (code >= 0xC0000000uL)
        {
            std::snprintf(both, sizeof(both), "%lu (0x%08lX)", code, code);
        }
        else
        {
            std::snprintf(both, sizeof(both), "%lu", code);
        }
        return std::string(both);
    }

    /**
     * What a Windows status code is, when it is one this program can name. An empty Text
     * means "no idea", and the caller then says nothing rather than guessing.
     *
     * STATUS_DLL_NOT_FOUND is the one that earns this table on its own: a shared-OpenCV
     * build started without opencv_world4140.dll beside it dies before printing a
     * character, and today that is a window that opens and shuts.
     */
    inline Text faultNameText(unsigned long code)
    {
        switch (code)
        {
        case 0xC0000005uL:
            return {"Muistin käyttörikkomus.", "Access violation."};
        case 0xC000001DuL:
            return {"Laiton konekäsky.", "Illegal instruction."};
        case 0xC0000094uL:
            return {"Kokonaislukujako nollalla.", "Integer divide by zero."};
        case 0xC0000135uL:
            return {"DLL-tiedosto puuttuu ohjelman vierestä.", "A DLL the program needs is missing beside it."};
        case 0xC0000139uL:
            return {"DLL-tiedostosta puuttuu aloituskohta.", "An entry point is missing from a DLL."};
        case 0xC00000FDuL:
            return {"Pinon ylivuoto.", "Stack overflow."};
        case 0xC0000374uL:
            return {"Kekomuisti rikkoutui.", "The heap was corrupted."};
        case 0xC0000409uL:
            return {"Pinon puskurin ylitys.", "Stack buffer overrun."};
        case 0xC0000142uL:
            return {"Ohjelman alustus epäonnistui.", "The program failed to initialise."};
        default:
            return Text{};
        }
    }

    /** POSIX only: the signal, said as a word where this program has one. */
    inline Text signalNameText(int number)
    {
        switch (number)
        {
        case SIGINT:
            return {"Ctrl+C.", "Ctrl+C."};
        case SIGTERM:
            return {"Lopetuspyyntö (SIGTERM).", "A stop request (SIGTERM)."};
        case SIGSEGV:
            return {"Muistin käyttörikkomus (SIGSEGV).", "Access violation (SIGSEGV)."};
        case SIGABRT:
            return {"Ohjelma keskeytti itsensä (SIGABRT).", "The program aborted itself (SIGABRT)."};
        case SIGFPE:
            return {"Laskuvirhe (SIGFPE).", "An arithmetic fault (SIGFPE)."};
        case SIGILL:
            return {"Laiton konekäsky (SIGILL).", "Illegal instruction (SIGILL)."};
#ifdef SIGKILL
        case SIGKILL:
            return {"Ohjelma lopetettiin väkisin (SIGKILL).", "The program was killed outright (SIGKILL)."};
#endif
#ifdef SIGHUP
        case SIGHUP:
            return {"Pääte suljettiin (SIGHUP).", "The terminal closed (SIGHUP)."};
#endif
#ifdef SIGBUS
        case SIGBUS:
            return {"Väylävirhe (SIGBUS).", "A bus error (SIGBUS)."};
#endif
        default:
        {
            const std::string n = std::to_string(number);
            return {"Signaali " + n + ".", "Signal " + n + "."};
        }
        }
    }

    /**
     * Everything said about one ending: the headline first, then whatever detail this
     * ending has. Never empty, and every line carries both languages.
     */
    inline std::vector<Text> endingLines(const Outcome &outcome)
    {
        std::vector<Text> lines;
        const Ending ending = endingOf(outcome);

        switch (ending)
        {
        case Ending::NeverStarted:
            lines.push_back({"Ohjelma ei käynnistynyt.", "The program did not start."});
            if (!outcome.detail.empty())
            {
                lines.push_back({outcome.detail, outcome.detail});
            }
            lines.push_back({"Taulu ei siis ole käynnissä. Ilmoita tämä ikkuna sellaisenaan.",
                             "So the board is not running. Report this window as it is."});
            break;

        case Ending::Cleanly:
            lines.push_back({"Ohjelma päättyi normaalisti (paluukoodi 0).",
                             "The program ended cleanly (exit code 0)."});
            break;

        case Ending::Killed:
            lines.push_back({"Ohjelma pysäytettiin.", "The program was stopped."});
            if (outcome.signalled)
            {
                lines.push_back(signalNameText(outcome.signal_number));
            }
            else
            {
                lines.push_back({"Paluukoodi " + codeAsText(outcome.code) + ".",
                                 "Exit code " + codeAsText(outcome.code) + "."});
            }
            break;

        case Ending::Faulted:
        default:
            lines.push_back({"Ohjelma päättyi virheeseen.", "The program ended with a fault."});
            if (outcome.signalled)
            {
                lines.push_back(signalNameText(outcome.signal_number));
            }
            else
            {
                lines.push_back({"Paluukoodi " + codeAsText(outcome.code) + ".",
                                 "Exit code " + codeAsText(outcome.code) + "."});
                const Text named = faultNameText(outcome.code);
                if (!named.fi.empty())
                {
                    lines.push_back(named);
                }
            }
            lines.push_back({"Kopioi tämän ikkunan teksti ja kerro mihin aikaan tämä tapahtui.",
                             "Copy the text in this window and say what time this happened."});
            break;
        }
        return lines;
    }

    /** The last line, said only where somebody is there to read it. */
    inline Text closingText()
    {
        return {"Paina Enter, niin ikkuna sulkeutuu.", "Press Enter to close this window."};
    }
}
