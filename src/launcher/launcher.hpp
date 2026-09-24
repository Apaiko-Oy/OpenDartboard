#pragma once
// #1303, #1306: what the launcher does, from the first line of main to its exit code.
//
// WHAT THIS IS (ADR-0077 §1). A board's program is two files. The launcher is what a
// shortcut points at and what a tester double-clicks; the detector is the 38 MB of OpenCV
// that watches the board. The reason recorded there is rollback -- catching an update
// that leaves a board unable to start needs a process still running three seconds after
// the new version died -- and the launcher was going to be alive anyway, for the reason
// below.
//
// SO IT IS INVISIBLE UNTIL SOMETHING GOES WRONG. It prints nothing before the detector
// starts, intercepts no argument, and hands the console straight down. What a tester sees
// while a board is working is what they saw before this file existed. The one moment it
// exists for is the one where the detector stops: today the window closes on the line
// that mattered, and the report that would have named the fault goes with it.
//
// #1306 ADDED THE UPDATING AND DID NOT CHANGE THAT. The check happens before the detector
// is started and says nothing at all when there is nothing to say: no manifest published,
// nothing newer, a Turnaus that cannot be reached -- a board in a pub sees the same blank
// screen it saw yesterday and starts. It speaks only when it is about to do something to
// this board, when it has just done something, or when it has refused to.
//
// AND IT ASKS NOTHING IT COULD WAIT FOR EVER ON. The window is held open by reading a
// line, and reading a line is a thing only somebody at a keyboard can answer. So the
// holding-open is gated on console_prompt::isInteractiveConsole() -- #1258's rule, in
// #1258's one place -- and a board started as a service, from a shortcut with no window,
// or with its input redirected says the same words and exits. It starts the detector
// either way and asks nothing either way; the only difference is whether anything waits
// afterwards.
//
// THE UPDATE PATH ASKS NOTHING EITHER, AND THAT IS DELIBERATE. #1258's rule decides
// whether a person is at the keyboard, and ADR-0077 §7 says in as many words that this is
// not that question: gating updates on somebody being there would mean a board started
// from a shortcut with no window -- most of the installs this is being built for -- never
// updates at all. So the launcher never asks whether to update. What it consults instead
// is whether this machine was doing something a minute ago, which is update_moment.hpp.
//
// ----------------------------------------------------------------------------------
// THE SHAPE OF ONE CARRY, so the loop below reads as a list rather than as control flow:
//
//   remember      read update\state.txt: what is installed, what is kept, what happened
//   decide        ADR-0077 §7 over that state and the clock. This is the only door to
//                 the network in the whole program.
//   check         fetch, verify, compare (#1305's ask(), unchanged)
//   apply         download, digest, unpack, swap (#1306's applyUpdate(), which is
//                 arranged so that stopping anywhere leaves a board that starts)
//   start         hand the console down and wait, however long it takes (#1303)
//   judge         did it fail to START, or did it run and then stop? update_moment.hpp
//   go back       after kAllowedFailedStarts fast bad endings, if a version that worked
//                 is kept, rename it back and start THAT, saying which and why
//   say           the ending, in both languages, and which versions these were
//   wait          only where somebody is there to press Enter
//
// The loop is bounded by kAttemptsInOneCarry and by nothing else. There is no timeout
// anywhere in it: #895's fault vigil keeps a board that cannot calibrate up on purpose,
// so a launcher with a clock on the detector is a launcher that kills a board waiting for
// somebody to plug a camera back in.

#include "../update/update_address.hpp"
#include "../update/update_channel.hpp"
#include "../update/update_check.hpp"
#include "../update/update_keys.hpp"
#include "../utils/console_prompt.hpp"
#include "apply_update.hpp"
#include "ending.hpp"
#include "install_layout.hpp"
#include "update_moment.hpp"

#include <chrono>
#include <ctime>
#include <functional>
#include <string>
#include <vector>

namespace launcher
{
    /** This build. A manifest's `minimumLauncher` is compared against it (ADR-0077 §1). */
    inline const char *version()
    {
#ifdef APP_VERSION
        return APP_VERSION;
#else
        return "0.0.0-dev";
#endif
    }

    /**
     * Start the detector and wait for it. A seam, so that a harness can hand in an ending
     * it could not otherwise arrange; detector_process.hpp's runAndWait is the real one.
     */
    typedef std::function<Outcome(const std::string &, const std::vector<std::string> &)> Runner;

    /** What one carry did, for a caller that has to return a number and for a test. */
    struct Report
    {
        Ending ending = Ending::NeverStarted;
        int exit_code = kNeverStarted;
        bool waited_for_a_person = false;
        std::vector<Text> said;

        // #1306, for a harness. None of these changes what is printed.
        bool looked_for_an_update = false;
        bool applied_an_update = false;
        bool rolled_back = false;
        int starts = 0;
        std::string ran_version;
        Moment moment = Moment::MayCheck;
    };

    /**
     * Everything a carry needs that is not the arguments: where things are, who to trust,
     * how to reach the network, and what the clock says. Handed in so that a harness can
     * arrange a board that crashed four minutes ago without waiting four minutes.
     */
    struct Surroundings
    {
        Layout layout;
        std::string address;
        std::string channel;
        std::vector<update_manifest::Anchor> anchors;
        update_check::Fetch fetch_manifest;
        FetchArtefact fetch_artefact;
        Unpack unpack;
        /**
         * What time it is, asked TWICE in one carry -- once before the check and once when
         * the detector stops -- and therefore a function rather than a number.
         *
         * MEASURED RATHER THAN ANTICIPATED. It was a number, and the first run of
         * testers/i1306_check.sh never made a second request: the moment rule read a
         * `last_stopped` written by the real clock against a `now` the harness had chosen,
         * so every launch after the first looked like a restart thirty years early and the
         * clock-went-backwards guard skipped the check. A board's own clock is consistent
         * with itself; a harness's was not, and the seam is what makes it so.
         */
        std::function<long long()> clock;
        bool forced = false;
        long long quiet_seconds = kQuietSeconds;
    };

    /** Unix seconds. One place, so a harness replaces one thing. */
    inline long long wallClock() { return static_cast<long long>(std::time(NULL)); }

    /**
     * What the detector beside this launcher is. Empty state means a board that has never
     * updated, and the answer is then this launcher's own version -- true by construction
     * of the release zip, which release.yml builds from one tag and packages together.
     * install_layout.hpp says what makes it false and why that costs a download and not a
     * board.
     */
    inline std::string installedVersion(const State &state, const std::string &launcher_version)
    {
        return state.detector_version.empty() ? launcher_version : state.detector_version;
    }

    /** The one door to the network, and everything that follows from going through it. */
    inline void lookForAnUpdate(const Surroundings &surroundings, State &state, Report &report,
                                std::vector<Text> &said)
    {
        const update_check::Answer answer =
            update_check::ask(surroundings.address, surroundings.channel, installedVersion(state, version()),
                              surroundings.anchors, surroundings.fetch_manifest);

        // Silence is the normal answer. A board that is up to date, a deployment that
        // publishes nothing, a Turnaus that cannot be reached: none of those is news, and
        // a launcher that said so every evening would be a launcher nobody reads.
        if (answer.kind == update_check::Kind::Refused)
        {
            // This one IS news, and #1305 already has the sentence for it: a manifest that
            // will not verify must never read as "there is no update".
            said.push_back(update_check::refusalText(answer.refusal, answer.refusal_detail));
            said.push_back(update_check::stillRunning());
            return;
        }
        if (answer.kind != update_check::Kind::Available)
        {
            return;
        }

        said.push_back({"Kanava " + answer.channel + " julkaisee version " + answer.published_version +
                            ". Se haetaan ja asennetaan ennen käynnistystä.",
                        "The " + answer.channel + " channel publishes version " + answer.published_version +
                            ". It is being fetched and installed before starting."});

        const Application application = applyUpdate(surroundings.layout, answer, version(), state,
                                                    surroundings.fetch_artefact, surroundings.unpack);
        const std::vector<Text> lines = applicationLines(application);
        for (size_t i = 0; i < lines.size(); i++)
        {
            said.push_back(lines[i]);
        }
        report.applied_an_update = application.ok;
    }

    /**
     * The whole of it. Nothing is printed before the detector starts unless the launcher
     * is about to do something to this board; every line printed is in both languages; the
     * pause happens only where somebody could end it.
     *
     * `somebodyIsThere` is console_prompt::isInteractiveConsole(), asked once by the
     * caller before any of this, so that one start is one answer.
     */
    inline Report carry(const std::vector<std::string> &arguments, console_prompt::Console &console,
                        bool somebodyIsThere, const Runner &runner, const Surroundings &surroundings)
    {
        Report report;
        std::vector<Text> before;

        State state = readState(surroundings.layout.state_file);

        // A caller that named no clock gets the real one, rather than a crash in the one
        // program whose failure mode is a board that will not start.
        const std::function<long long()> clock = surroundings.clock ? surroundings.clock
                                                                    : std::function<long long()>(wallClock);

        // ADR-0077 §7, evaluated. There is no other path from here to the network.
        const long long started_at = clock();
        const MomentDecision moment =
            decideMoment(state, started_at, surroundings.forced, surroundings.quiet_seconds);
        report.moment = moment.moment;
        if (moment.may_check)
        {
            report.looked_for_an_update = true;
            lookForAnUpdate(surroundings, state, report, before);
        }
        else if (surroundings.forced)
        {
            // Unreachable by construction -- forced is always may_check -- and kept so
            // that a change to decideMoment() which stopped honouring --update-now would
            // say so on the screen rather than silently.
            before.push_back(momentText(moment));
        }

        for (size_t i = 0; i < before.size(); i++)
        {
            console.say(before[i]);
            report.said.push_back(before[i]);
        }

        // ---- start it, judge what came back, and go back if it will not start ----------
        Outcome outcome;
        int attempt = 0;
        for (attempt = 1; attempt <= kAttemptsInOneCarry; attempt++)
        {
            report.starts = attempt;
            report.ran_version = installedVersion(state, version());
            state.last_started = clock();
            state.last_ending.clear();
            writeState(surroundings.layout.state_file, state);

            const std::chrono::steady_clock::time_point began = std::chrono::steady_clock::now();
            outcome = runner(surroundings.layout.detector, arguments);
            const long long ran_for =
                std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - began).count();

            const Ending ending = endingOf(outcome);
            state.last_stopped = clock();
            state.last_ending = endingWord(ending);

            if (!failedToStart(ending, ran_for))
            {
                // It started. Whatever it then did is #1303's business, and a version that
                // has started is a version with nothing against it.
                state.failed_starts = 0;
                writeState(surroundings.layout.state_file, state);
                break;
            }

            state.failed_starts++;
            writeState(surroundings.layout.state_file, state);

            const bool attempts_left = state.failed_starts < kAllowedFailedStarts;
            const bool can_go_back = hasSomethingToGoBackTo(state) && fileExists(surroundings.layout.previous_exe);
            if (!attempts_left && !can_go_back)
            {
                break; // nothing to go back to: #1303's report is the whole answer
            }
            if (attempt == kAttemptsInOneCarry)
            {
                break; // the bound, so that nothing here can loop
            }

            const Text tried = {"Versio " + report.ran_version + " ei käynnistynyt (" + std::to_string(ran_for) +
                                    " s, paluukoodi " + codeAsText(outcome.code) + ").",
                                "Version " + report.ran_version + " did not start (" + std::to_string(ran_for) +
                                    " s, exit code " + codeAsText(outcome.code) + ")."};
            console.say(tried);
            report.said.push_back(tried);

            if (attempts_left)
            {
                const Text again = {"Yritetään vielä kerran.", "Trying once more."};
                console.say(again);
                report.said.push_back(again);
                continue;
            }

            const std::string going_back_to = state.previous_version;
            if (rollBack(surroundings.layout, state))
            {
                report.rolled_back = true;
                writeState(surroundings.layout.state_file, state);
                const Text went = {"Palataan versioon " + going_back_to + ", joka toimi. Uusi versio poistettiin "
                                                                          "käytöstä.",
                                   "Going back to version " + going_back_to + ", which worked. The new version has "
                                                                              "been taken out of use."};
                console.say(went);
                report.said.push_back(went);
                continue;
            }
            const Text stuck = {"Edellistä versiota ei saatu palautettua, joten taulu jää tähän versioon.",
                                "The previous version could not be restored, so the board stays on this one."};
            console.say(stuck);
            report.said.push_back(stuck);
            break;
        }

        // ---- and what the ending was (#1303, unchanged) --------------------------------
        report.ending = endingOf(outcome);
        report.exit_code = exitCodeFor(report.ending);
        const std::vector<Text> ending_lines = endingLines(outcome);
        for (size_t i = 0; i < ending_lines.size(); i++)
        {
            console.say(ending_lines[i]);
            report.said.push_back(ending_lines[i]);
        }
        // Which versions these were. A tester's report then names both without being
        // asked, which is what a manifest's `minimumLauncher` is read against.
        console.sayVerbatim(std::string("opendartboard ") + report.ran_version);
        console.sayVerbatim(std::string("opendartboard-launcher ") + version());

        if (somebodyIsThere)
        {
            console.say(closingText());
            std::string ignored;
            // One line, whatever it is, and no loop: readLine is false on a closed input,
            // and a launcher that asked again would be the thing #1258's rule forbids.
            (void)console.readLine(ignored);
            report.waited_for_a_person = true;
        }
        return report;
    }
}
