// #1797: a detector that ends one way on its first start and another on its second.
//
// #1303's stub takes its ending from the environment and #1306's from its own version,
// and neither can do what this slice is about: ONE carry that starts the SAME file twice
// and gets a scheduled stop (exit 60) the first time and a clean ending the second. So
// this stub counts its own starts in a file and reads its exit code off a list, one per
// start, the last one repeating. Every control still comes from the environment and none
// from argv, for #1303's reason: argv is the thing under test -- the second start must
// carry the first start's arguments byte for byte -- so the stub claims no element of it
// and writes every element it was given to a file named after the start.
//
//   OD_STUB_RUNS=<file>        append one line per start, the start's ordinal (1, 2, ...);
//                              the ordinal is the number of lines already there plus one
//   OD_STUB_ARGV_TO=<dir>      write argv to <dir>/argv.<ordinal>, one element per line
//   OD_STUB_EXITS=<n,n,...>    the exit code of start 1, 2, ...; the last repeats
//   OD_STUB_LINGER_MS=<ms>     stay alive this long before ending
//
// It prints the same first line the real detector's --version does.

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#ifndef APP_VERSION
#define APP_VERSION "0.0.0-stub"
#endif

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

static std::string env(const char *name)
{
    const char *value = std::getenv(name);
    return value ? std::string(value) : std::string();
}

static int countLines(const std::string &path)
{
    std::ifstream in(path.c_str(), std::ios::binary);
    int lines = 0;
    std::string line;
    while (std::getline(in, line))
    {
        lines++;
    }
    return lines;
}

static std::vector<int> numbersIn(const std::string &list)
{
    std::vector<int> out;
    size_t at = 0;
    while (at <= list.size())
    {
        const size_t comma = list.find(',', at);
        const std::string one = list.substr(at, comma == std::string::npos ? std::string::npos : comma - at);
        if (!one.empty())
        {
            out.push_back(static_cast<int>(std::strtol(one.c_str(), NULL, 10)));
        }
        if (comma == std::string::npos)
        {
            break;
        }
        at = comma + 1;
    }
    return out;
}

int main(int argc, char **argv)
{
    int ordinal = 1;
    const std::string runs = env("OD_STUB_RUNS");
    if (!runs.empty())
    {
        ordinal = countLines(runs) + 1;
        std::ofstream out(runs.c_str(), std::ios::binary | std::ios::app);
        out << ordinal << "\n";
    }

    const std::string argvTo = env("OD_STUB_ARGV_TO");
    if (!argvTo.empty())
    {
        std::ofstream out((argvTo + "/argv." + std::to_string(ordinal)).c_str(), std::ios::binary);
        for (int i = 0; i < argc; i++)
        {
            out << argv[i] << "\n";
        }
    }

    std::cout << "OpenDartboard runtime version: " << APP_VERSION << " (start " << ordinal << ")" << std::endl;

    const std::string linger = env("OD_STUB_LINGER_MS");
    if (!linger.empty())
    {
        const long milliseconds = std::strtol(linger.c_str(), NULL, 10);
#ifdef _WIN32
        ::Sleep(static_cast<DWORD>(milliseconds));
#else
        ::usleep(static_cast<useconds_t>(milliseconds) * 1000);
#endif
    }

    const std::vector<int> exits = numbersIn(env("OD_STUB_EXITS"));
    if (exits.empty())
    {
        return 0;
    }
    const size_t index = static_cast<size_t>(ordinal - 1);
    return index < exits.size() ? exits[index] : exits.back();
}
