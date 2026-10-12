// #1797: the launcher follows a scheduled stop -- one look, one start with the same
// arguments -- measured against real child processes.
//
// PREDICTION, STATED FIRST. testers/i1797_stub is started through the launcher's own
// runAndWait by the launcher's own carry(), from a state file that says the last run
// ended "cleanly" two seconds ago -- the quiet rule's QuickRestart, so the FIRST start is
// not preceded by a look, and any look counted below is the one the scheduled stop
// earned. The stub returns scheduled_stop::kExitCode (60) on its first start and 0 on its
// second. The carry then:
//
//   starts it twice             report.starts == 2, the stub's own count says 2
//   with identical arguments    argv.1 and argv.2 are byte-for-byte the arguments given,
//                               argv[0] included
//   looks once, in between      the fetch_manifest hook is called exactly once, and the
//                               state file it reads at that moment says last_ending=scheduled
//   says the scheduled sentence the window names 06:00 and "started again", in both languages
//   ends on the second run      Ending::Cleanly, exit code 0, state file last_ending=cleanly,
//                               failed_starts 0
//
// THE CONTROL: the same state and the same stub with exits 0: one start, no look, and
// the scheduled sentence is not said. Without it, "looked once" is satisfied by a launcher
// that looks on every start.
//
// THE BOUND: the stub returns 60 on every start, at once. The launcher follows the first
// stop (a look, a second start) and refuses the second -- a `scheduled` ending inside a
// minute of the start that followed a scheduled stop is a bug, not a day -- so it starts
// it twice and not three times, says so, and exits kScheduledNotFollowed.
//
// AND A FRESH BOARD: no state file at all. MayCheck before the first start (as every
// board today), then the scheduled stop's look: two looks, two starts.
//
//   testers/i1797_carry_check.cpp <stub-path> <work-dir>

#include "launcher/detector_process.hpp"
#include "launcher/ending.hpp"
#include "launcher/launcher.hpp"
#include "update/update_check.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
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
    size_t reads = 0;

    void say(const console_prompt::Text &text) override { said.push_back(text); }
    void sayVerbatim(const std::string &line) override { verbatim.push_back(line); }
    bool readLine(std::string &) override
    {
        reads++;
        return false;
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

    void print() const
    {
        for (size_t i = 0; i < said.size(); i++)
        {
            std::printf("     | %s\n     | %s\n", said[i].fi.c_str(), said[i].en.c_str());
        }
        for (size_t i = 0; i < verbatim.size(); i++)
        {
            std::printf("     | %s\n", verbatim[i].c_str());
        }
    }
};

// ---------------------------------------------------------------- the stub, arranged

static std::string gStub;
static std::string gWork;

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

static std::string valueIn(const std::vector<std::string> &lines, const std::string &key)
{
    for (size_t i = 0; i < lines.size(); i++)
    {
        if (lines[i].rfind(key + "=", 0) == 0)
        {
            return lines[i].substr(key.size() + 1);
        }
    }
    return std::string();
}

/** What an install really carries, plus two shapes that would show a re-quoting. */
static std::vector<std::string> arguments()
{
    std::vector<std::string> given;
    given.push_back("--cams");
    given.push_back("0,1,2");
    given.push_back("--allow-plaintext");
    given.push_back("--log-file");
    given.push_back("C:\\Users\\Mikko\\Documents\\darts log.txt");
    given.push_back("--label");
    given.push_back("Pub \"back\" room");
    return given;
}

/** One case: its own directory, its own stub counters, its own state file. */
struct Case
{
    std::string dir;
    int manifest_requests = 0;
    std::string ending_seen_by_the_look; // last_ending in the state file when the hook ran
    launcher::Surroundings surroundings;

    explicit Case(const std::string &name)
    {
        dir = gWork + "/" + name;
        std::system(("rm -rf '" + dir + "' && mkdir -p '" + dir + "'").c_str());
        std::system(("cp '" + gStub + "' '" + dir + "/opendartboard'").c_str());

        arrange("OD_STUB_RUNS", dir + "/runs.txt");
        arrange("OD_STUB_ARGV_TO", dir);
        arrange("OD_STUB_LINGER_MS", "");

        surroundings.layout = launcher::layoutFor(dir + "/opendartboard");
        surroundings.address = "http://127.0.0.1:1";
        surroundings.channel = "stable";
        surroundings.fetch_manifest = [this](const std::string &, const std::string &)
        {
            manifest_requests++;
            ending_seen_by_the_look = valueIn(linesOf(surroundings.layout.state_file), "last_ending");
            odhttp::Response response;
            response.transport_error = "this check reaches no network at all";
            return response;
        };
        surroundings.fetch_artefact = [](const std::string &)
        {
            odhttp::Response response;
            response.transport_error = "this check reaches no network at all";
            return response;
        };
        surroundings.unpack = [](const std::string &, const std::string &) { return false; };
        surroundings.clock = launcher::wallClock;
    }

    /** The quiet rule's QuickRestart: a clean stop two seconds ago. */
    void stoppedCleanlyTwoSecondsAgo()
    {
        launcher::State state;
        state.last_started = launcher::wallClock() - 60;
        state.last_stopped = launcher::wallClock() - 2;
        state.last_ending = "cleanly";
        launcher::writeState(surroundings.layout.state_file, state);
    }

    launcher::Report carry(RecordingConsole &console)
    {
        return launcher::carry(arguments(), console, false, launcher::runAndWait, surroundings);
    }

    int starts() const { return static_cast<int>(linesOf(dir + "/runs.txt").size()); }
    std::vector<std::string> argvOf(int start) const { return linesOf(dir + "/argv." + std::to_string(start)); }
    std::string finalEnding() const { return valueIn(linesOf(surroundings.layout.state_file), "last_ending"); }
    std::string finalFailedStarts() const { return valueIn(linesOf(surroundings.layout.state_file), "failed_starts"); }
};

static bool identicalArguments(const std::vector<std::string> &arrived, const std::string &program)
{
    const std::vector<std::string> given = arguments();
    if (arrived.size() != given.size() + 1 || arrived[0] != program)
    {
        return false;
    }
    for (size_t i = 0; i < given.size(); i++)
    {
        if (arrived[i + 1] != given[i])
        {
            std::printf("     argument %zu: gave [%s], got [%s]\n", i, given[i].c_str(), arrived[i + 1].c_str());
            return false;
        }
    }
    return true;
}

int main(int argc, char **argv)
{
    if (argc < 3)
    {
        std::printf("usage: %s <stub-path> <work-dir>\n", argv[0]);
        return 2;
    }
    gStub = argv[1];
    gWork = argv[2];

    const std::string code = std::to_string(scheduled_stop::kExitCode);

    // ---- the carry: 60, then 0 ----------------------------------------------------------
    {
        std::printf("---- a scheduled stop, then a clean run ----\n");
        Case one("scheduled-then-clean");
        one.stoppedCleanlyTwoSecondsAgo();
        arrange("OD_STUB_EXITS", code + ",0");
        RecordingConsole console;
        const launcher::Report report = one.carry(console);
        console.print();

        check(report.starts == 2 && one.starts() == 2,
              "the launcher started the detector twice (report " + std::to_string(report.starts) + ", the stub counted " +
                  std::to_string(one.starts()) + ")");
        check(identicalArguments(one.argvOf(1), one.surroundings.layout.detector),
              "the first start carried the arguments byte for byte");
        check(identicalArguments(one.argvOf(2), one.surroundings.layout.detector),
              "and so did the second: the same arguments, argv[0] included");
        check(one.manifest_requests == 1,
              "it looked for an update exactly once, between the two (fetch_manifest called " +
                  std::to_string(one.manifest_requests) + " times)");
        check(one.ending_seen_by_the_look == "scheduled",
              "and when it looked, the state file said last_ending=" + one.ending_seen_by_the_look);
        check(report.scheduled_stops == 1 && report.looks == 1 && !report.refused_to_follow,
              "the report counts one scheduled stop followed and one look");
        check(report.moment == launcher::Moment::Scheduled,
              "the look was through the fourth door, Moment::Scheduled, two seconds after the stop");
        check(console.says("06:00") && console.says("started again") && console.says("uudelleen"),
              "the window said the scheduled sentence, naming 06:00, in both languages");
        check(console.bothLanguagesEverywhere(), "and every line carries Finnish and English");
        check(report.ending == launcher::Ending::Cleanly && report.exit_code == 0,
              "the carry ended on the second run's clean ending, exit code 0");
        check(one.finalEnding() == "cleanly" && one.finalFailedStarts() == "0",
              "and the state file ends saying last_ending=" + one.finalEnding() + " failed_starts=" +
                  one.finalFailedStarts());
        check(console.verbatim.size() == 2, "the versions are said once, at the end, not per round");
    }

    // ---- the control: 0 ------------------------------------------------------------------
    {
        std::printf("---- CONTROL: a clean run from the same state ----\n");
        Case control("clean");
        control.stoppedCleanlyTwoSecondsAgo();
        arrange("OD_STUB_EXITS", "0");
        RecordingConsole console;
        const launcher::Report report = control.carry(console);
        console.print();
        check(report.starts == 1 && control.starts() == 1, "CONTROL: a clean ending is one start");
        check(control.manifest_requests == 0 && report.looks == 0,
              "CONTROL: and no look at all two seconds after a clean stop (the quiet rule, unchanged)");
        check(!console.says("06:00") && report.scheduled_stops == 0, "CONTROL: and the scheduled sentence is not said");
        check(report.ending == launcher::Ending::Cleanly && report.exit_code == 0 && control.finalEnding() == "cleanly",
              "CONTROL: Cleanly, exit code 0, state file cleanly -- byte for byte #1303's");
    }

    // ---- the bound: 60, 60, 60, ... -------------------------------------------------------
    {
        std::printf("---- THE BOUND: a detector that comes back scheduled again at once ----\n");
        Case bound("scheduled-again");
        bound.stoppedCleanlyTwoSecondsAgo();
        arrange("OD_STUB_EXITS", code);
        RecordingConsole console;
        const launcher::Report report = bound.carry(console);
        console.print();
        check(report.starts == 2 && bound.starts() == 2,
              "the first scheduled stop is followed and the second is not: two starts, not three (" +
                  std::to_string(bound.starts()) + ")");
        check(bound.manifest_requests == 1, "one look, for the stop that was followed");
        check(report.refused_to_follow && report.scheduled_stops == 1, "the report says the second was refused");
        check(console.says("should not happen") && console.says("ei pitäisi tapahtua"),
              "and the window says so, in both languages");
        check(report.ending == launcher::Ending::Scheduled && report.exit_code == launcher::kScheduledNotFollowed,
              "the launcher's own exit code is kScheduledNotFollowed (" + std::to_string(report.exit_code) + ")");
        check(bound.finalEnding() == "scheduled", "the state file says scheduled, so the NEXT launch may look");
    }

    // ---- a fresh board ------------------------------------------------------------------
    {
        std::printf("---- a board with no history ----\n");
        Case fresh("fresh");
        std::remove(fresh.surroundings.layout.state_file.c_str());
        arrange("OD_STUB_EXITS", code + ",0");
        RecordingConsole console;
        const launcher::Report report = fresh.carry(console);
        check(report.starts == 2 && fresh.manifest_requests == 2 && report.looks == 2,
              "no state file: a look before the first start (as today) and one after the scheduled stop (" +
                  std::to_string(fresh.manifest_requests) + " looks, " + std::to_string(report.starts) + " starts)");
        check(report.ending == launcher::Ending::Cleanly && report.exit_code == 0, "and it ends cleanly");
    }

    std::printf("%d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
