// #1303: a detector that ends the three ways on demand, and says what it was given.
//
// The launcher is a supervisor, so measuring it needs a supervised thing whose ending is
// arranged rather than hoped for. The real detector opens cameras, and a board that
// cannot calibrate never exits at all (#895's fault vigil) -- so it can be made to do
// exactly one of the three endings this slice is about: none of them.
//
// EVERY CONTROL COMES FROM THE ENVIRONMENT AND NOT FROM argv. That is the point of the
// file: argv is the thing under test (the fourth criterion), so the stub must not claim a
// single element of it. What it does instead is write every element it received, one per
// line, and let the check compare byte for byte.
//
//   STUB_ARGV_TO=<file>   write argv there, one element per line, argv[0] first
//   STUB_SAY=<line>       print it to stdout, so a caller can see the console passed down
//   STUB_STDIN=read       read one line from stdin and print STDIN:<line> or STDIN:none
//   STUB_SLEEP_MS=<n>     stay alive this long before ending
//   STUB_MODE=crash       die on a real SIGSEGV / access violation
//   STUB_MODE=stopped     die of SIGTERM with the default disposition (POSIX)
//   STUB_EXIT=<n>         otherwise, return this

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>

#ifdef _WIN32
#include <windows.h>
#else
#include <csignal>
#include <unistd.h>
#endif

static std::string env(const char *name)
{
    const char *value = std::getenv(name);
    return value ? std::string(value) : std::string();
}

int main(int argc, char **argv)
{
    const std::string argvTo = env("STUB_ARGV_TO");
    if (!argvTo.empty())
    {
        std::ofstream out(argvTo.c_str(), std::ios::binary);
        for (int i = 0; i < argc; i++)
        {
            out << argv[i] << "\n";
        }
    }

    const std::string say = env("STUB_SAY");
    if (!say.empty())
    {
        std::cout << say << std::endl;
    }

    if (env("STUB_STDIN") == "read")
    {
        std::string line;
        if (std::getline(std::cin, line))
        {
            if (!line.empty() && line[line.size() - 1] == '\r')
            {
                line.erase(line.size() - 1);
            }
            std::cout << "STDIN:" << line << std::endl;
        }
        else
        {
            std::cout << "STDIN:none" << std::endl;
        }
    }

    const std::string sleepMs = env("STUB_SLEEP_MS");
    if (!sleepMs.empty())
    {
        const long milliseconds = std::strtol(sleepMs.c_str(), NULL, 10);
#ifdef _WIN32
        ::Sleep(static_cast<DWORD>(milliseconds));
#else
        ::usleep(static_cast<useconds_t>(milliseconds) * 1000);
#endif
    }

    const std::string mode = env("STUB_MODE");
    if (mode == "crash")
    {
#ifdef _WIN32
        // A real access violation, so GetExitCodeProcess answers 0xC0000005 and the
        // launcher's classification is reading what Windows really wrote.
        volatile int *nowhere = (volatile int *)0;
        *nowhere = 1;
#else
        std::signal(SIGSEGV, SIG_DFL);
        ::raise(SIGSEGV);
#endif
        return 99; // not reached
    }
    if (mode == "stopped")
    {
#ifdef _WIN32
        // Windows has no signals to speak of. The nearest arranged equivalent is the
        // status a Ctrl+C leaves behind, which is what the launcher classifies on.
        ::ExitProcess(0xC000013A);
#else
        std::signal(SIGTERM, SIG_DFL);
        ::raise(SIGTERM);
#endif
        return 99; // not reached
    }

    const std::string code = env("STUB_EXIT");
    return code.empty() ? 0 : static_cast<int>(std::strtol(code.c_str(), NULL, 10));
}
