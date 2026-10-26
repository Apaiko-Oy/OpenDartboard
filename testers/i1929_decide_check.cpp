// turnaus#1929: what a board asked to recalibrate decides, and what the launcher does with
// the stop. PURE: recalibration.hpp's rule and marker, and the launcher's carry() driven by
// a runner that returns numbers -- no detector, no network, no camera.
//
// PREDICTION, STATED FIRST. On this tree every line below is `ok` and the program exits 0.
// testers/i1929_check.sh then compiles it against four mutated scratch copies of src/, and
// each is predicted to turn exactly the named lines red:
//
//   --mutate-round     decide() loses its round-in-hand branch:
//                      FAIL "a round in hand is refused"   (accepted instead: a restart mid-leg)
//   --mutate-moved     decide() loses its Moved branch, and both Moved lines go red:
//                      FAIL "a held Moved verdict is refused" (accepted: a restart that
//                      would only meet the same refusal at the next start, said nowhere)
//                      FAIL "and Moved is the reason said when a round is in hand as well"
//   --mutate-answered  decide() loses its answered branch:
//                      FAIL "a reference answered already is ignored" (the restart repeats)
//   --mutate-ending    endingOf() no longer knows 61, which it then reads as a fault:
//                      FAIL "exit 61 is Ending::Recalibrating"
//                      FAIL "the launcher followed the recalibration stop with another start"
//                      FAIL "the window says why it stopped" -- and possibly more of the carry,
//                      which then runs #1306's failed-start path; only these three are named.
//
//   The three decide() mutations are predicted to turn red exactly the lines named and no
//   other, so the check counts the FAIL lines as well as naming them.
//
//   testers/i1929_decide_check   (run in a scratch directory: the marker is written to ./cache)

#include "launcher/ending.hpp"
#include "launcher/launcher.hpp"
#include "launcher/update_moment.hpp"
#include "utils/recalibration.hpp"
#include "utils/scheduled_stop.hpp"

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

static bool ascii(const std::string &text)
{
    for (size_t i = 0; i < text.size(); i++)
    {
        if ((unsigned char)text[i] < 0x20 || (unsigned char)text[i] > 0x7e)
        {
            return false;
        }
    }
    return true;
}

class RecordingConsole : public console_prompt::Console
{
public:
    std::vector<console_prompt::Text> said;
    void say(const console_prompt::Text &text) override { said.push_back(text); }
    void sayVerbatim(const std::string &) override {}
    bool readLine(std::string &) override { return false; }
    bool says(const std::string &needle_fi, const std::string &needle_en) const
    {
        for (size_t i = 0; i < said.size(); i++)
        {
            if (said[i].fi.find(needle_fi) != std::string::npos && said[i].en.find(needle_en) != std::string::npos)
            {
                return true;
            }
        }
        return false;
    }
};

int main()
{
    using recalibration::Facts;
    using recalibration::Verdict;
    using recalibration::decide;

    std::printf("---- the rule ----\n");
    {
        Facts quiet;
        check(decide(quiet).verdict == Verdict::Accept, "a quiet board between legs accepts");

        Facts round;
        round.round_in_hand = true;
        const recalibration::Decision d = decide(round);
        check(d.verdict == Verdict::Refuse && d.words == recalibration::roundRefusal(),
              "a round in hand is refused, in words that say a leg is in play");

        Facts thrown;
        thrown.darts_since_last_poll = true;
        check(decide(thrown).verdict == Verdict::Wait,
              "a dart since the last poll puts the request off one poll, unanswered");
        thrown.waited_once = true;
        const recalibration::Decision again = decide(thrown);
        check(again.verdict == Verdict::Refuse && again.words == recalibration::throwingRefusal(),
              "and darts again at the next poll refuse it: the leg has begun");

        Facts moved;
        moved.moved = "camera 2 bull moved 25.00 px";
        const recalibration::Decision m = decide(moved);
        check(m.verdict == Verdict::Refuse && m.words == recalibration::movedRefusal(),
              "a held Moved verdict is refused, in words that say an operator clears it");
        moved.round_in_hand = true;
        moved.darts_since_last_poll = true;
        check(decide(moved).words == recalibration::movedRefusal(),
              "and Moved is the reason said when a round is in hand as well");

        Facts sight;
        sight.sight_suspended = true;
        const recalibration::Decision s = decide(sight);
        check(s.verdict == Verdict::Refuse && s.words == recalibration::sightRefusal(),
              "a board confirming its geometry after losing sight refuses");

        Facts answered;
        answered.already_answered = true;
        answered.moved = "anything";
        check(decide(answered).verdict == Verdict::Ignore,
              "a reference answered already is ignored: never confirmed or acted on twice");

        const std::string words[] = {recalibration::movedRefusal(), recalibration::roundRefusal(),
                                     recalibration::throwingRefusal(), recalibration::sightRefusal()};
        bool fit = true;
        for (const std::string &w : words)
        {
            fit = fit && !w.empty() && w.size() <= recalibration::kMostRefusal && ascii(w);
        }
        check(fit, "every refusal is ASCII and at most the 200 characters confirmRecalibration keeps");
        check(recalibration::movedRefusal().find("Moved") != std::string::npos,
              "the Moved refusal names the verdict");
    }

    std::printf("---- the marker ----\n");
    {
        std::remove(recalibration::markerPath().c_str());
        check(recalibration::takeMarker().reference.empty(), "no marker reads as no recalibration");
        recalibration::Marker written;
        written.reference = "01M5X9DFQHF5TCYN3GWWFBP1F6";
        written.census_before = "3 of 3 cameras";
        written.geometry_before = "camera 1 index=0 scoring=1 bull=671,309 | camera 2 index=1 scoring=1 bull=626,292";
        check(recalibration::writeMarker(written), "the stop writes its marker");
        const recalibration::Marker read = recalibration::takeMarker();
        check(read.reference == written.reference && read.census_before == written.census_before &&
                  read.geometry_before == written.geometry_before,
              "the next start reads the reference, the census and the geometry back");
        check(recalibration::takeMarker().reference.empty(), "and reading it spends it: a later start reads none");
    }

    std::printf("---- the exit status and the launcher ----\n");
    {
        check(recalibration::kExitCode == 61 && recalibration::kExitCode != scheduled_stop::kExitCode &&
                  recalibration::kExitCode != 75 && recalibration::kExitCode != 78,
              "the recalibration stop is 61: not 60, 75 or 78");
        check(launcher::endingOf(launcher::Outcome{true, 61, false, 0, ""}) == launcher::Ending::Recalibrating,
              "exit 61 is Ending::Recalibrating");
        check(launcher::endingOf(launcher::Outcome{true, 60, false, 0, ""}) == launcher::Ending::Scheduled,
              "exit 60 is still Ending::Scheduled");
        check(launcher::endingWord(launcher::Ending::Recalibrating) == "recalibrating",
              "the state file's word is \"recalibrating\", not \"scheduled\"");
        check(!launcher::failedToStart(launcher::Ending::Recalibrating, 1),
              "a recalibration stop seconds after a start is not a failure to start");
        launcher::State state;
        const long long now = 1800000000LL;
        state.last_started = now - 600;
        state.last_stopped = now - 2;
        state.last_ending = "recalibrating";
        const launcher::MomentDecision moment = launcher::decideMoment(state, now, false);
        check(moment.moment == launcher::Moment::QuickRestart && !moment.may_check,
              "the start after it is a QuickRestart: no look for an update mid-evening");
    }

    std::printf("---- carry(): 61, then 0 ----\n");
    {
        std::system("rm -rf carry && mkdir -p carry");
        launcher::Surroundings surroundings;
        surroundings.layout = launcher::layoutFor("carry/opendartboard");
        surroundings.address = "http://127.0.0.1:1";
        surroundings.channel = "stable";
        int looks = 0;
        surroundings.fetch_manifest = [&looks](const std::string &, const std::string &)
        {
            looks++;
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
        launcher::State state;
        state.last_started = launcher::wallClock() - 60;
        state.last_stopped = launcher::wallClock() - 2;
        state.last_ending = "cleanly";
        launcher::writeState(surroundings.layout.state_file, state);

        std::vector<unsigned long> exits;
        exits.push_back(61);
        exits.push_back(61);
        exits.push_back(0);
        size_t started = 0;
        std::vector<std::vector<std::string>> argvs;
        launcher::Runner runner = [&](const std::string &, const std::vector<std::string> &args)
        {
            launcher::Outcome outcome;
            outcome.started = true;
            outcome.code = started < exits.size() ? exits[started] : 0;
            argvs.push_back(args);
            started++;
            return outcome;
        };
        std::vector<std::string> args;
        args.push_back("--autocams");
        args.push_back("--reuse-calibration");
        RecordingConsole console;
        const launcher::Report report = launcher::carry(args, console, false, runner, surroundings);
        check(report.starts == 3 && started == 3 && report.recalibration_stops == 2,
              "the launcher followed the recalibration stop with another start, twice, and ended on the third (" +
                  std::to_string(report.starts) + " starts)");
        check(looks == 0 && report.looks == 0, "and looked for an update never (" + std::to_string(looks) + ")");
        check(argvs.size() == 3 && argvs[1] == args && argvs[2] == args, "each start carried the same arguments");
        check(report.ending == launcher::Ending::Cleanly && report.exit_code == 0 && !report.refused_to_follow,
              "the carry ended on the clean run, exit 0");
        check(console.says("kalibroimaan uudelleen", "asked to recalibrate"),
              "the window says why it stopped, in Finnish and English");
    }

    std::printf("checks %d   failed %d\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
