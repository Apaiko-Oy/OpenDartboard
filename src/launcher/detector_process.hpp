#pragma once
// #1303: starting the detector and waiting for it. The only file with a platform in it.
//
// THE DETECTOR IS GIVEN THIS CONSOLE, NOT A PIPE. That is the single load-bearing choice
// in this file, and getting it wrong would be invisible until a tester tried to pair.
// #1258's rule asks the INPUT handle whether a person is at the keyboard; a launcher that
// captured the detector's output, or handed it a pipe for stdin so as to relay it, would
// make `isInteractiveConsole()` answer false inside the detector -- and a board
// double-clicked by a tester would never ask for a pairing code and never ask which three
// cameras. So no STARTF_USESTDHANDLES, no CREATE_NEW_CONSOLE, no redirection: the child
// is attached to the console the launcher was given, whatever that console is. A tester
// sees exactly what they see today, because it IS what they see today.
//
// The second consequence is wanted too: a launcher started with its input redirected
// (`< NUL`, a scheduled task) hands that same redirected input down, so the detector
// answers false to the same question for the same reason, and starts anyway.
//
// A DETECTOR THAT NEVER EXITS IS NOT A CASE HERE. #895's fault vigil keeps a board that
// cannot calibrate up on purpose, and OD_MAX_CYCLES does not bound it. The wait below is
// therefore INFINITE and deliberately so: the launcher's job is to still be there when
// the detector stops, and a launcher with a timeout is a launcher that kills a board
// waiting for somebody to plug a camera back in. While the detector runs, the launcher
// costs one waiting thread and says nothing.
//
// WHAT CTRL+C DOES. A console control event is delivered to every process attached to the
// console, so the detector receives it directly and #825's handler does what it always
// did. The launcher installs a handler of its own that records the event and returns
// TRUE, which stops the default handler from tearing the launcher down -- without it the
// launcher dies first and the tester is told nothing, which is the fault this whole slice
// is about. CTRL_CLOSE_EVENT (the window's X, a logoff) is different in kind: Windows
// allows a few seconds and then ends everything regardless, so a window closed by hand
// may still take the report with it. That is a platform limit, and it is why the report
// is written before anything is waited on.

#include "command_line.hpp"
#include "ending.hpp"

#include <cstdlib>
#include <string>
#include <vector>

#ifdef _WIN32
// windows.h arrives through od_platform_first.hpp, force-included on MSVC.
#include "../utils/od_platform_first.hpp"
#else
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace launcher
{
    /** The file the launcher starts, beside itself. */
#ifdef _WIN32
    const char *const kDetectorName = "opendartboard.exe";
#else
    const char *const kDetectorName = "opendartboard";
#endif

    /** Set by the console control handler; read once the wait is over. */
    inline volatile long &stopRequested()
    {
        static volatile long requested = 0;
        return requested;
    }

#ifdef _WIN32
    inline BOOL WINAPI onConsoleControl(DWORD)
    {
        stopRequested() = 1;
        return TRUE; // handled: the launcher is not torn down, and lives to say what happened
    }
#endif

    /** Install it, once, before the detector is started. False where there is none to install. */
    inline bool watchForConsoleControl()
    {
#ifdef _WIN32
        return ::SetConsoleCtrlHandler(onConsoleControl, TRUE) != 0;
#else
        return false;
#endif
    }

    /** The launcher's own file, as the operating system names it. Empty when unknowable. */
    inline std::string ownExecutablePath()
    {
#ifdef _WIN32
        char path[MAX_PATH * 4];
        const DWORD written = ::GetModuleFileNameA(NULL, path, static_cast<DWORD>(sizeof(path)));
        if (written == 0 || written >= sizeof(path))
        {
            return std::string();
        }
        return std::string(path, written);
#else
        char path[4096];
        const ssize_t written = ::readlink("/proc/self/exe", path, sizeof(path) - 1);
        if (written <= 0)
        {
            return std::string();
        }
        return std::string(path, static_cast<size_t>(written));
#endif
    }

    /**
     * Which detector this launcher starts: the one beside it, always.
     *
     * OD_DETECTOR names another, and exists so a harness can point the launcher at a stub
     * that ends the three ways on demand. It is read from the environment rather than
     * taken from the command line on purpose: every command-line argument belongs to the
     * detector and is passed through untouched, and a launcher that ate one of them would
     * be the thing this slice's fourth criterion is against.
     */
    inline std::string detectorPath()
    {
        const char *named = std::getenv("OD_DETECTOR");
        if (named != NULL && *named != '\0')
        {
            return std::string(named);
        }
        const std::string me = ownExecutablePath();
        if (me.empty())
        {
            return std::string(kDetectorName);
        }
        return beside(me, kDetectorName);
    }

    /**
     * Start it, wait for it however long it takes, and report what the platform observed.
     * Never throws and never prints: the words are ending.hpp's.
     */
    inline Outcome runAndWait(const std::string &program, const std::vector<std::string> &arguments)
    {
        Outcome outcome;
#ifdef _WIN32
        std::string line = buildCommandLine(program, arguments);
        std::vector<char> writable(line.begin(), line.end());
        writable.push_back('\0');

        STARTUPINFOA startup;
        ::ZeroMemory(&startup, sizeof(startup));
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process;
        ::ZeroMemory(&process, sizeof(process));

        // TRUE: an inherited redirection (`< NUL`, a scheduled task's handles) reaches the
        // detector as well, which is what makes the third criterion true of both of them.
        if (::CreateProcessA(program.c_str(), &writable[0], NULL, NULL, TRUE, 0, NULL, NULL, &startup, &process) == 0)
        {
            const DWORD error = ::GetLastError();
            outcome.started = false;
            outcome.code = error;
            outcome.detail = program + " (Windows " + std::to_string(static_cast<unsigned long>(error)) + ")";
            return outcome;
        }
        ::CloseHandle(process.hThread);
        ::WaitForSingleObject(process.hProcess, INFINITE);
        DWORD code = 0;
        ::GetExitCodeProcess(process.hProcess, &code);
        ::CloseHandle(process.hProcess);
        outcome.started = true;
        outcome.code = static_cast<unsigned long>(code);
        return outcome;
#else
        // argv is built before the fork: nothing between fork and exec allocates.
        std::vector<std::string> owned;
        owned.push_back(program);
        for (size_t i = 0; i < arguments.size(); i++)
        {
            owned.push_back(arguments[i]);
        }
        std::vector<char *> argv;
        for (size_t i = 0; i < owned.size(); i++)
        {
            argv.push_back(const_cast<char *>(owned[i].c_str()));
        }
        argv.push_back(NULL);

        // The pipe is how "it never started" is told from "it started and returned 127".
        // Closed on a successful exec, so a read of nothing means the exec worked.
        int told[2];
        if (::pipe(told) != 0)
        {
            outcome.started = false;
            outcome.detail = program + " (pipe: " + std::strerror(errno) + ")";
            return outcome;
        }
        ::fcntl(told[1], F_SETFD, FD_CLOEXEC);

        const pid_t child = ::fork();
        if (child < 0)
        {
            const int why = errno;
            ::close(told[0]);
            ::close(told[1]);
            outcome.started = false;
            outcome.detail = program + " (fork: " + std::strerror(why) + ")";
            return outcome;
        }
        if (child == 0)
        {
            ::close(told[0]);
            ::execv(program.c_str(), &argv[0]);
            const int why = errno;
            const ssize_t unused = ::write(told[1], &why, sizeof(why));
            (void)unused;
            ::_exit(127);
        }

        ::close(told[1]);
        int why = 0;
        const ssize_t heard = ::read(told[0], &why, sizeof(why));
        ::close(told[0]);

        int status = 0;
        while (::waitpid(child, &status, 0) < 0 && errno == EINTR)
        {
        }

        if (heard == static_cast<ssize_t>(sizeof(why)))
        {
            outcome.started = false;
            outcome.code = static_cast<unsigned long>(why);
            outcome.detail = program + " (" + std::strerror(why) + ")";
            return outcome;
        }
        outcome.started = true;
        if (WIFSIGNALED(status))
        {
            outcome.signalled = true;
            outcome.signal_number = WTERMSIG(status);
            outcome.code = static_cast<unsigned long>(128 + WTERMSIG(status));
        }
        else
        {
            outcome.code = static_cast<unsigned long>(WEXITSTATUS(status));
        }
        return outcome;
#endif
    }
}
