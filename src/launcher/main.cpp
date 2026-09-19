// #1303: opendartboard-launcher. What a shortcut points at.
//
// Everything it does is in launcher.hpp, and why it exists at all is at the top of that
// file. This is the entry point and the three lines that cannot be tested from a harness:
// the console control handler, the real argv, and the real console.

#include "../utils/console_prompt.hpp"
#include "detector_process.hpp"
#include "launcher.hpp"

#include <string>
#include <vector>

int main(int argc, char **argv)
{
    // Before anything is started: a Ctrl+C that arrives while the detector runs must not
    // tear the launcher down before it has said what happened.
    launcher::watchForConsoleControl();

    // Asked once, before the detector exists, so one start is one answer.
    const bool somebodyIsThere = console_prompt::isInteractiveConsole();

    // Every argument, in order, untouched. The launcher claims none of them.
    std::vector<std::string> arguments;
    for (int i = 1; i < argc; i++)
    {
        arguments.push_back(std::string(argv[i]));
    }

    console_prompt::StdConsole console;
    const launcher::Report report =
        launcher::carry(launcher::detectorPath(), arguments, console, somebodyIsThere, launcher::runAndWait);

    return report.exit_code;
}
