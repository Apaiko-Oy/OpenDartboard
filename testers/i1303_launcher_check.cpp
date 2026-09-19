// #1303: what the launcher decides, measured, with real child processes.
//
// No camera, no console, no network. It starts testers/i1303_stub -- a detector whose
// ending is arranged rather than hoped for -- through the launcher's own runAndWait, and
// asks the launcher's own carry() what it said, what it waited for and what it returned.
//
// WHAT THIS MEASURES AND WHAT IT CANNOT. The launcher is a Windows program and this runs
// on Linux, which is where every harness in this repository runs. Three of the six
// criteria are decisions over numbers and strings and are measured here outright: the
// three endings and their words, the arguments arriving unchanged, and the launcher's own
// exit code. The fourth -- asking nothing where nobody is at the keyboard -- is measured
// here for the decision and by testers/i1303_check.sh for the behaviour, by running the
// real launcher binary with its input redirected, with its input an open pipe nobody ever
// writes to, and (the control) on a pseudo-terminal where it must really wait.
//
// The Windows half that Linux cannot execute is the command line, because POSIX hands a
// child an argv array and Windows hands it one string. That is measured here instead by
// round-tripping: what buildCommandLine writes is split again by a reference
// implementation of the rules CommandLineToArgvW documents, and must come back byte for
// byte. testers/i1303_windows.sh then does it for real, on Windows, end to end.
//
// THE ONE THAT CARRIES THE SLICE is `nothing is said before the detector starts`, beside
// `an ending that is not clean still says so`. A launcher that printed a banner would
// pass everything else here and fail the first criterion -- a tester sees what they see
// today -- and a launcher that reported only a clean exit is the one a tester meets on
// the night it matters.
//
//   testers/i1303_check.sh [worktree] [--mutate]

#include "launcher/command_line.hpp"
#include "launcher/detector_process.hpp"
#include "launcher/ending.hpp"
#include "launcher/launcher.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

static int failures = 0;
static int checks = 0;

static void check(bool passed, const std::string &what)
{
    checks++;
    if (!passed)
    {
        failures++;
    }
    std::printf("%s %s\n", passed ? "ok  " : "FAIL", what.c_str());
}

// ---------------------------------------------------------------- a console that remembers

class RecordingConsole : public console_prompt::Console
{
public:
    std::vector<console_prompt::Text> said;
    std::vector<std::string> verbatim;
    std::vector<std::string> answers;
    size_t reads = 0;

    void say(const console_prompt::Text &text) override { said.push_back(text); }
    void sayVerbatim(const std::string &line) override { verbatim.push_back(line); }

    bool readLine(std::string &line) override
    {
        reads++;
        if (answers.empty())
        {
            return false; // a closed input, which is what a launcher must never loop on
        }
        line = answers.front();
        answers.erase(answers.begin());
        return true;
    }

    bool says(const std::string &needle) const
    {
        for (size_t i = 0; i < said.size(); i++)
        {
            if (said[i].fi.find(needle) != std::string::npos || said[i].en.find(needle) != std::string::npos)
            {
                return true;
            }
        }
        return false;
    }

    bool bothLanguagesEverywhere() const
    {
        for (size_t i = 0; i < said.size(); i++)
        {
            if (said[i].fi.empty() || said[i].en.empty())
            {
                return false;
            }
        }
        return !said.empty();
    }
};

// ---------------------------------------------------------------- the stub, arranged

static std::string gStub;
static std::string gWork;

/** Set one control variable for the next stub start. An empty value unsets it. */
static void arrange(const char *name, const std::string &value)
{
    if (value.empty())
    {
        ::unsetenv(name);
    }
    else
    {
        ::setenv(name, value.c_str(), 1);
    }
}

static void arrangeNothing()
{
    arrange("STUB_ARGV_TO", "");
    arrange("STUB_SAY", "");
    arrange("STUB_STDIN", "");
    arrange("STUB_SLEEP_MS", "");
    arrange("STUB_MODE", "");
    arrange("STUB_EXIT", "");
}

static std::vector<std::string> linesOf(const std::string &path)
{
    std::vector<std::string> lines;
    std::ifstream in(path.c_str(), std::ios::binary);
    std::string line;
    while (std::getline(in, line))
    {
        lines.push_back(line);
    }
    return lines;
}

/** One carry of the stub, with a console that remembers and nobody at the keyboard. */
static launcher::Report carryStub(RecordingConsole &console, const std::vector<std::string> &arguments,
                                  bool somebodyIsThere = false)
{
    return launcher::carry(gStub, arguments, console, somebodyIsThere, launcher::runAndWait);
}

// ---------------------------------------------------------------- the Windows splitter
//
// The rules CommandLineToArgvW documents, written out so that what buildCommandLine
// produces can be split again and compared. argv[0] is parsed by its own rule -- quotes
// delimit and backslashes are ordinary -- which is why the round-trip below is asserted
// over the arguments and the program separately.

static std::vector<std::string> splitCommandLine(const std::string &line)
{
    std::vector<std::string> out;
    size_t i = 0;

    while (i < line.size() && (line[i] == ' ' || line[i] == '\t'))
    {
        i++;
    }
    if (i < line.size())
    {
        std::string first;
        if (line[i] == '"')
        {
            i++;
            while (i < line.size() && line[i] != '"')
            {
                first.push_back(line[i++]);
            }
            if (i < line.size())
            {
                i++;
            }
        }
        else
        {
            while (i < line.size() && line[i] != ' ' && line[i] != '\t')
            {
                first.push_back(line[i++]);
            }
        }
        out.push_back(first);
    }

    for (;;)
    {
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t'))
        {
            i++;
        }
        if (i >= line.size())
        {
            break;
        }
        std::string argument;
        bool inQuotes = false;
        while (i < line.size())
        {
            if (!inQuotes && (line[i] == ' ' || line[i] == '\t'))
            {
                break;
            }
            if (line[i] == '\\')
            {
                size_t backslashes = 0;
                while (i < line.size() && line[i] == '\\')
                {
                    i++;
                    backslashes++;
                }
                if (i < line.size() && line[i] == '"')
                {
                    argument.append(backslashes / 2, '\\');
                    if (backslashes % 2 == 1)
                    {
                        argument.push_back('"');
                    }
                    else
                    {
                        inQuotes = !inQuotes;
                    }
                    i++;
                }
                else
                {
                    argument.append(backslashes, '\\');
                }
                continue;
            }
            if (line[i] == '"')
            {
                inQuotes = !inQuotes;
                i++;
                continue;
            }
            argument.push_back(line[i++]);
        }
        out.push_back(argument);
    }
    return out;
}

/** What an install really carries, plus the shapes that break a careless quoter. */
static std::vector<std::string> awkwardArguments()
{
    std::vector<std::string> arguments;
    arguments.push_back("--cams");
    arguments.push_back("0,1,2");
    arguments.push_back("--allow-plaintext");
    arguments.push_back("--turnaus");
    arguments.push_back("https://turnaus.apaiko.fi");
    arguments.push_back("--credentials");
    arguments.push_back("C:\\Program Files\\OpenDartboard\\credentials.json");
    arguments.push_back("C:\\od\\");                  // a trailing backslash, with nothing to quote around it
    arguments.push_back("C:\\Program Files\\od\\"); // and one that IS quoted: the shape that eats a closing quote
    arguments.push_back("a b");                       // a space
    arguments.push_back("say \"hello\"");             // an embedded quote
    arguments.push_back("");                          // an empty argument is still an argument
    arguments.push_back("--label");
    arguments.push_back("Pub\tback room");            // a tab separates as surely as a space
    return arguments;
}

// ---------------------------------------------------------------- the checks

int main(int argc, char **argv)
{
    if (argc < 3)
    {
        std::printf("usage: %s <stub-path> <work-dir>\n", argv[0]);
        return 2;
    }
    gStub = argv[1];
    gWork = argv[2];

    // ---- the rule the redirected-input runs rest on ------------------------------------
    check(!console_prompt::isInteractiveConsole(),
          "this harness's own input is not a keyboard, so nothing below inherits a console");

    // ---- the words --------------------------------------------------------------------
    {
        launcher::Outcome clean;
        clean.started = true;
        clean.code = 0;
        launcher::Outcome faulted;
        faulted.started = true;
        faulted.code = 3;
        launcher::Outcome crashed;
        crashed.started = true;
        crashed.code = 0xC0000005uL;
        launcher::Outcome missingDll;
        missingDll.started = true;
        missingDll.code = 0xC0000135uL;
        launcher::Outcome controlC;
        controlC.started = true;
        controlC.code = launcher::kControlCExit;
        launcher::Outcome never;
        never.detail = "C:\\od\\opendartboard.exe (Windows 2)";

        const launcher::Outcome all[] = {clean, faulted, crashed, missingDll, controlC, never};
        bool both = true;
        bool saidSomething = true;
        for (size_t i = 0; i < sizeof(all) / sizeof(all[0]); i++)
        {
            const std::vector<launcher::Text> lines = launcher::endingLines(all[i]);
            saidSomething = saidSomething && !lines.empty();
            for (size_t j = 0; j < lines.size(); j++)
            {
                both = both && !lines[j].fi.empty() && !lines[j].en.empty();
            }
        }
        check(saidSomething, "every ending says something");
        check(both, "every line of every ending carries Finnish and English");
        check(!launcher::closingText().fi.empty() && !launcher::closingText().en.empty(),
              "the line that holds the window open carries both languages");

        const std::vector<launcher::Text> dll = launcher::endingLines(missingDll);
        bool named = false;
        for (size_t j = 0; j < dll.size(); j++)
        {
            named = named || dll[j].en.find("DLL") != std::string::npos;
        }
        check(named, "a missing DLL is named as one, and not only as 3221225781");

        const std::vector<launcher::Text> gone = launcher::endingLines(never);
        bool carriesPath = false;
        for (size_t j = 0; j < gone.size(); j++)
        {
            carriesPath = carriesPath || gone[j].en.find("opendartboard.exe") != std::string::npos;
        }
        check(carriesPath, "a detector that could not be started is named in the window");
    }

    // ---- the classification, over the numbers -----------------------------------------
    {
        launcher::Outcome outcome;
        outcome.started = true;

        outcome.code = 0;
        check(launcher::endingOf(outcome) == launcher::Ending::Cleanly, "exit code 0 is a clean ending");

        outcome.code = 1;
        check(launcher::endingOf(outcome) == launcher::Ending::Faulted, "exit code 1 is a fault");

        outcome.code = 0xC0000005uL;
        check(launcher::endingOf(outcome) == launcher::Ending::Faulted, "an access violation is a fault");

        outcome.code = launcher::kControlCExit;
        check(launcher::endingOf(outcome) == launcher::Ending::Killed, "STATUS_CONTROL_C_EXIT is a killing");

        outcome.started = false;
        check(launcher::endingOf(outcome) == launcher::Ending::NeverStarted, "no process at all is its own ending");

        outcome.started = true;
        outcome.signalled = true;
        outcome.signal_number = SIGTERM;
        check(launcher::endingOf(outcome) == launcher::Ending::Killed, "SIGTERM is a killing");
        outcome.signal_number = SIGSEGV;
        check(launcher::endingOf(outcome) == launcher::Ending::Faulted, "SIGSEGV is a fault");
    }

    // ---- the exit code, which is a claim of its own -------------------------------------
    {
        check(launcher::exitCodeFor(launcher::Ending::Cleanly) == 0, "only a clean run exits 0");
        check(launcher::exitCodeFor(launcher::Ending::NeverStarted) != 0 &&
                  launcher::exitCodeFor(launcher::Ending::Faulted) != 0 &&
                  launcher::exitCodeFor(launcher::Ending::Killed) != 0,
              "every other ending exits non-zero");
        check(launcher::exitCodeFor(launcher::Ending::NeverStarted) != launcher::exitCodeFor(launcher::Ending::Faulted) &&
                  launcher::exitCodeFor(launcher::Ending::Faulted) != launcher::exitCodeFor(launcher::Ending::Killed) &&
                  launcher::exitCodeFor(launcher::Ending::NeverStarted) != launcher::exitCodeFor(launcher::Ending::Killed),
              "the three failures are three different numbers");
        check(!launcher::detectorRan(launcher::Ending::NeverStarted) &&
                  launcher::detectorRan(launcher::Ending::Cleanly) && launcher::detectorRan(launcher::Ending::Faulted) &&
                  launcher::detectorRan(launcher::Ending::Killed),
              "the exit code says whether the detector ran at all");
    }

    // ---- a real detector, ending the three ways ----------------------------------------
    {
        arrangeNothing();
        RecordingConsole console;
        const launcher::Report report = carryStub(console, std::vector<std::string>());
        check(report.ending == launcher::Ending::Cleanly && report.exit_code == 0,
              "a real child that returns 0 is a clean ending and the launcher exits 0");
        check(console.bothLanguagesEverywhere(), "and it is said in both languages");
        check(!report.waited_for_a_person && console.reads == 0,
              "and nobody being there means nothing was asked");
    }
    {
        arrangeNothing();
        arrange("STUB_EXIT", "3");
        RecordingConsole console;
        const launcher::Report report = carryStub(console, std::vector<std::string>());
        check(report.ending == launcher::Ending::Faulted && report.exit_code == launcher::kFaulted,
              "a real child that returns 3 is a fault, and the launcher says so in its own exit code");
        check(console.says("3"), "and the window carries the number it returned");
    }
    {
        arrangeNothing();
        arrange("STUB_MODE", "crash");
        RecordingConsole console;
        const launcher::Report report = carryStub(console, std::vector<std::string>());
        check(report.ending == launcher::Ending::Faulted && report.exit_code == launcher::kFaulted,
              "a real child that dies on a segmentation fault is a fault");
        check(console.says("SIGSEGV"), "and the window names what killed it");
    }
    {
        arrangeNothing();
        arrange("STUB_MODE", "stopped");
        RecordingConsole console;
        const launcher::Report report = carryStub(console, std::vector<std::string>());
        check(report.ending == launcher::Ending::Killed && report.exit_code == launcher::kKilled,
              "a real child that is stopped is a killing, and not a fault");
        check(console.says("SIGTERM"), "and the window says which");
    }
    {
        arrangeNothing();
        RecordingConsole console;
        const std::string missing = gWork + "/there-is-no-detector-here";
        const launcher::Report report =
            launcher::carry(missing, std::vector<std::string>(), console, false, launcher::runAndWait);
        check(report.ending == launcher::Ending::NeverStarted && report.exit_code == launcher::kNeverStarted,
              "a detector that could not be started at all is its own ending and its own exit code");
        check(console.says("there-is-no-detector-here"), "and the window names the file it looked for");
    }

    // ---- the arguments an install carries ----------------------------------------------
    {
        arrangeNothing();
        const std::string written = gWork + "/argv.txt";
        arrange("STUB_ARGV_TO", written);
        const std::vector<std::string> given = awkwardArguments();
        RecordingConsole console;
        const launcher::Report report = carryStub(console, given);
        check(report.ending == launcher::Ending::Cleanly, "the stub ran with the awkward arguments");

        const std::vector<std::string> arrived = linesOf(written);
        check(arrived.size() == given.size() + 1,
              "every argument arrived, and so did argv[0]: " + std::to_string(arrived.size()) + " of " +
                  std::to_string(given.size() + 1));
        bool identical = arrived.size() == given.size() + 1;
        if (identical)
        {
            check(arrived[0] == gStub, "argv[0] is the file that is really running");
            for (size_t i = 0; i < given.size(); i++)
            {
                if (arrived[i + 1] != given[i])
                {
                    identical = false;
                    std::printf("     argument %zu: gave [%s], got [%s]\n", i, given[i].c_str(),
                                arrived[i + 1].c_str());
                }
            }
        }
        check(identical, "and every one of them arrived unchanged, byte for byte");
    }

    // ---- nothing is said before the detector starts --------------------------------------
    {
        arrangeNothing();
        RecordingConsole console;
        size_t saidBefore = 999;
        size_t verbatimBefore = 999;
        launcher::Runner watching = [&console, &saidBefore, &verbatimBefore](
                                        const std::string &program, const std::vector<std::string> &arguments)
        {
            saidBefore = console.said.size();
            verbatimBefore = console.verbatim.size();
            return launcher::runAndWait(program, arguments);
        };
        launcher::carry(gStub, std::vector<std::string>(), console, false, watching);
        check(saidBefore == 0 && verbatimBefore == 0,
              "the launcher prints nothing before the detector starts, so a working board looks as it did");
        check(!console.said.empty(), "and the control: it does print once the detector has stopped");
    }

    // ---- somebody is there, and the window is held open ---------------------------------
    {
        arrangeNothing();
        RecordingConsole console;
        console.answers.push_back(""); // they pressed Enter
        const launcher::Report report = carryStub(console, std::vector<std::string>(), true);
        check(report.waited_for_a_person && console.reads == 1,
              "somebody at the keyboard is asked once, and the window stays until they answer");
        check(console.says("Enter"), "and is told what to press");
        check(report.exit_code == 0, "and waiting for them changed no exit code");
    }
    {
        arrangeNothing();
        arrange("STUB_EXIT", "7");
        RecordingConsole console; // no answers: the input is closed under them
        const launcher::Report report = carryStub(console, std::vector<std::string>(), true);
        check(console.reads == 1, "an input that ends is read once and never asked again");
        check(report.exit_code == launcher::kFaulted, "and a closed input does not change what is reported");
    }

    // ---- a detector that takes its time -------------------------------------------------
    {
        arrangeNothing();
        arrange("STUB_SLEEP_MS", "1500");
        RecordingConsole console;
        const std::chrono::steady_clock::time_point began = std::chrono::steady_clock::now();
        const launcher::Report report = carryStub(console, std::vector<std::string>());
        const double seconds =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - began).count();
        check(report.ending == launcher::Ending::Cleanly, "a detector that takes its time still ends cleanly");
        check(seconds >= 1.4,
              "and the launcher waited for it rather than giving up: " + std::to_string(seconds) + "s");
        // Unboundedness cannot be measured by waiting: what this shows is that there is no
        // timeout under a second and a half. The wait really is unbounded -- INFINITE on
        // Windows, waitpid with no alarm on POSIX -- and #895's fault vigil is why.
    }

    // ---- the Windows command line, round-tripped -----------------------------------------
    {
        const std::string program = "C:\\Program Files\\OpenDartboard\\opendartboard.exe";
        const std::vector<std::string> given = awkwardArguments();
        const std::string line = launcher::buildCommandLine(program, given);
        const std::vector<std::string> back = splitCommandLine(line);

        check(back.size() == given.size() + 1, "the command line splits back into the same number of arguments: " +
                                                   std::to_string(back.size()) + " of " +
                                                   std::to_string(given.size() + 1));
        bool identical = back.size() == given.size() + 1;
        if (identical)
        {
            check(back[0] == program, "and the program's own path survives its quoting");
            for (size_t i = 0; i < given.size(); i++)
            {
                if (back[i + 1] != given[i])
                {
                    identical = false;
                    std::printf("     argument %zu: wrote [%s], read back [%s]\n", i, given[i].c_str(),
                                back[i + 1].c_str());
                }
            }
        }
        check(identical, "and every argument comes back byte for byte through Windows' own splitting rules");
        std::printf("     command line: %s\n", line.c_str());

        check(launcher::quoteArgument("--cams") == "--cams", "an ordinary argument is not quoted at all");
        check(launcher::quoteArgument("") == "\"\"", "an empty argument is still one argument");
        check(launcher::quoteArgument("C:\\od\\") == "C:\\od\\",
              "a trailing backslash with nothing else awkward about it is not quoted at all");
        check(launcher::quoteArgument("C:\\Program Files\\od\\") == "\"C:\\Program Files\\od\\\\\"",
              "but inside quotes it is doubled, so it cannot eat the closing quote");
    }

    // ---- where the detector is looked for -------------------------------------------------
    {
        check(launcher::directoryOf("C:\\od\\opendartboard-launcher.exe") == "C:\\od",
              "a Windows path's directory");
        check(launcher::directoryOf("/usr/local/bin/x") == "/usr/local/bin", "a POSIX path's directory");
        check(launcher::directoryOf("opendartboard-launcher.exe").empty(), "a bare name is in no directory");
        check(launcher::beside("/a/b/launcher", "opendartboard") == "/a/b/opendartboard",
              "the detector is looked for beside the launcher");
        check(launcher::beside("launcher", "opendartboard") == "opendartboard",
              "and a bare name stays a bare name");
        check(!launcher::ownExecutablePath().empty(), "the launcher can find its own file");
    }

    std::printf("\n%d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}

// THE MUTATION. `i1303_check.sh --mutate` gates the window's holding-open on nothing
// instead of on console_prompt::isInteractiveConsole(), on a copy of the sources, and
// expects the harness to go red. Measured: it goes red in two places and they are worth
// telling apart. This file catches it as `nobody being there means nothing was asked`,
// because RecordingConsole counts its reads -- which is the decision. The harness catches
// it as the open-pipe run taking the whole timeout -- which is the HARM: a board started
// as a service, waiting for ever on a question nobody will answer. The first says the
// rule was broken; only the second says what breaking it costs, and it is the one that
// needed a real binary and an input that never answers.
