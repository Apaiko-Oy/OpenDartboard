// #1306: a detector that KNOWS WHICH VERSION IT IS, and behaves accordingly.
//
// #1303's stub takes its behaviour from the environment, which is right for a slice about
// how a run ended. This slice is about which of several versions is installed, and one
// carry can start two of them -- the new one that will not start, and the old one it goes
// back to -- in one process, with one environment. So a stub whose behaviour came from an
// environment variable could not be bad on the first start and good on the second, and
// the rollback could not be measured end to end at all.
//
// Every control below therefore names VERSIONS rather than runs. The version is compiled
// in with -DAPP_VERSION, which is how the real artefact carries its own (#1299), so a zip
// built around this stub is a zip whose contents really do differ between releases.
//
//   OD_STUB_RAN_TO=<file>        append this version, one line per start. The harness
//                                reads it as the sequence of versions that really ran.
//   OD_STUB_BAD=<v,v>            these versions exit 3 at once: a release that will not
//                                start, which is what the rollback exists for
//   OD_STUB_KILLED=<v,v>         these die of SIGTERM: somebody stopped the board, which
//                                must NEVER read as a failure to start
//   OD_STUB_ARGV_TO=<file>       write argv there, so "the arguments still pass through"
//                                is a file on disk and not an inference
//
// It prints the same first line the real detector's --version does, so a harness reading
// a console sees the version that is running rather than the version somebody recorded.

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>

#ifndef APP_VERSION
#define APP_VERSION "0.0.0-stub"
#endif

#ifndef _WIN32
#include <csignal>
#endif

static std::string env(const char *name)
{
    const char *value = std::getenv(name);
    return value ? std::string(value) : std::string();
}

/** True when this stub's own version is one of the comma-separated names in `list`. */
static bool namesMe(const std::string &list)
{
    const std::string me = APP_VERSION;
    size_t at = 0;
    while (at <= list.size())
    {
        const size_t comma = list.find(',', at);
        const std::string one = list.substr(at, comma == std::string::npos ? std::string::npos : comma - at);
        if (one == me)
        {
            return true;
        }
        if (comma == std::string::npos)
        {
            break;
        }
        at = comma + 1;
    }
    return false;
}

int main(int argc, char **argv)
{
    const std::string ranTo = env("OD_STUB_RAN_TO");
    if (!ranTo.empty())
    {
        std::ofstream out(ranTo.c_str(), std::ios::binary | std::ios::app);
        out << APP_VERSION << "\n";
    }

    const std::string argvTo = env("OD_STUB_ARGV_TO");
    if (!argvTo.empty())
    {
        std::ofstream out(argvTo.c_str(), std::ios::binary);
        for (int i = 0; i < argc; i++)
        {
            out << argv[i] << "\n";
        }
    }

    std::cout << "OpenDartboard runtime version: " << APP_VERSION << std::endl;

    if (namesMe(env("OD_STUB_BAD")))
    {
        return 3;
    }
    if (namesMe(env("OD_STUB_KILLED")))
    {
#ifdef _WIN32
        ::exit(3);
#else
        std::signal(SIGTERM, SIG_DFL);
        ::raise(SIGTERM);
#endif
        return 99; // not reached
    }
    return 0;
}
