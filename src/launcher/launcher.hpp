#pragma once
// #1303: what the launcher does, from the first line of main to its exit code.
//
// WHAT THIS IS (ADR-0077 §1). A board's program is two files. The launcher is what a
// shortcut points at and what a tester double-clicks; the detector is the 38 MB of OpenCV
// that watches the board. The reason recorded there is rollback -- catching an update
// that leaves a board unable to start needs a process still running three seconds after
// the new version died -- and the launcher was going to be alive anyway, for the reason
// below. THERE IS NO UPDATING IN THIS FILE and there is deliberately none: #1306 puts it
// here, and working out how a process is started is not a thing to be doing at that
// moment.
//
// SO IT IS INVISIBLE UNTIL SOMETHING GOES WRONG. It prints nothing before the detector
// starts, intercepts no argument, and hands the console straight down. What a tester sees
// while a board is working is what they saw before this file existed. The one moment it
// exists for is the one where the detector stops: today the window closes on the line
// that mattered, and the report that would have named the fault goes with it.
//
// AND IT ASKS NOTHING IT COULD WAIT FOR EVER ON. The window is held open by reading a
// line, and reading a line is a thing only somebody at a keyboard can answer. So the
// holding-open is gated on console_prompt::isInteractiveConsole() -- #1258's rule, in
// #1258's one place -- and a board started as a service, from a shortcut with no window,
// or with its input redirected says the same words and exits. It starts the detector
// either way and asks nothing either way; the only difference is whether anything waits
// afterwards.
//
// The version is said in the failure window and nowhere else. A tester's report then
// names which launcher it was, which #1306 needs (a manifest states the oldest launcher
// that can install it), without a banner appearing where a board is working normally.

#include "../utils/console_prompt.hpp"
#include "ending.hpp"

#include <functional>
#include <string>
#include <vector>

namespace launcher
{
    /** This build. #1306 compares a manifest's `minimumLauncher` against it. */
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
    };

    /**
     * The whole of it. Nothing is printed before the detector starts; every line printed
     * afterwards is in both languages; the pause happens only where somebody could end it.
     *
     * `somebodyIsThere` is console_prompt::isInteractiveConsole(), asked once by the
     * caller before any of this, so that one start is one answer.
     */
    inline Report carry(const std::string &program, const std::vector<std::string> &arguments,
                        console_prompt::Console &console, bool somebodyIsThere, const Runner &runner)
    {
        const Outcome outcome = runner(program, arguments);

        Report report;
        report.ending = endingOf(outcome);
        report.exit_code = exitCodeFor(report.ending);
        report.said = endingLines(outcome);

        for (size_t i = 0; i < report.said.size(); i++)
        {
            console.say(report.said[i]);
        }
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
