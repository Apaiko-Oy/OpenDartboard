// #1303, #1306: opendartboard-launcher. What a shortcut points at.
//
// Everything it decides is in launcher.hpp, and why it exists at all is at the top of
// that file. This is the entry point: the three lines that cannot be tested from a
// harness -- the console control handler, the real argv, the real console -- and the
// wiring that names the real network, the real clock and the real filesystem.
//
// EVERY ARGUMENT STILL BELONGS TO THE DETECTOR (#1303). Three of them are READ here --
// `--turnaus` and `--credentials` because ADR-0077 §5 says the launcher resolves the same
// address by the same rule, and `--update-now` because §7 names it -- and none of them is
// claimed: the vector handed to the detector is argv from 1 to argc-1, in order,
// untouched. `--update-now` reaches the detector too, where main.cpp asks for flags by
// name and has no unknown-argument path, so it means nothing and does nothing there.

#include "../update/update_address.hpp"
#include "../update/update_channel.hpp"
#include "../update/update_keys.hpp"
#include "../utils/console_prompt.hpp"
#include "../utils/od_paths.hpp"
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

    launcher::Surroundings surroundings;
    surroundings.layout = launcher::layoutFor(launcher::detectorPath());

    // The same four steps the detector follows, from the same header (ADR-0077 §5).
    std::string credentials = update_address::argumentAfter(arguments, "--credentials");
    if (credentials.empty())
    {
        credentials = od_paths::join(od_paths::configDir(), "credentials.json");
    }
    surroundings.address =
        update_address::resolve(update_address::argumentAfter(arguments, "--turnaus"), credentials).url;
    surroundings.channel = update_channel::load(update_channel::fileBeside(credentials));

    surroundings.anchors = update_keys::anchors();
    surroundings.fetch_manifest = update_check::fetchOverHttp;
    surroundings.fetch_artefact = launcher::fetchArtefactOverHttp;
    surroundings.unpack = launcher::unpackWithSystemTool;
    surroundings.now = launcher::wallClock();
    // ADR-0077 §7's "for somebody standing at the machine who wants one". OD_UPDATE_NOW is
    // the same thing for a start that has no command line -- a double-click -- exactly as
    // OD_TURNAUS_URL is --turnaus and OD_ALLOW_PLAINTEXT is --allow-plaintext (#1259).
    surroundings.forced =
        update_address::hasArgument(arguments, "--update-now") || od_paths::env("OD_UPDATE_NOW") == "1";

    console_prompt::StdConsole console;
    const launcher::Report report =
        launcher::carry(arguments, console, somebodyIsThere, launcher::runAndWait, surroundings);

    return report.exit_code;
}
