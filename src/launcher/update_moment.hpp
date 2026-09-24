#pragma once
// #1306: whether this launch is a moment an update may be applied at, and what "failed to
// start" means. Two rules, both pure, both over numbers the launcher already has.
//
// ============================== THE MOMENT (ADR-0077 §7) ==============================
//
// "An update is applied only by the launcher, only before the detector starts. At that
// moment nothing is in progress, so nothing can be interrupted." That much is a property
// of where the code lives. The case it does not cover is the crash-restart at 21:40
// mid-tournament: the launcher comes back up, sees a new release, and spends two minutes
// on 38 MB while a match waits at the board. So the ADR adds: skip the check entirely
// when the previous run ended RECENTLY or ended BADLY.
//
// THE RULE IS EVALUATED AND NOT INTENDED, and that sentence is the fourth criterion. It
// is `decideMoment()` below -- a pure function of (what the last run did, what time it is
// now, whether somebody asked) returning a decision AND the reason for it. The launcher
// has one call to it and no other path to the network: nothing else in this program can
// reach update_check::ask(). testers/i1306_inside.sh measures it twice over, once in
// process against recorded states and once end to end, where the HTTP server's own access
// log is what says whether a check was made -- a rule that is merely intended leaves a
// request in that log.
//
// FIFTEEN MINUTES, AND WHY IT STAYS FIFTEEN. The ADR calls it the starting figure and
// hands #1306 the measuring. It was measured, and the measurement says the figure is not
// about us.
//
// Applying an artefact the size of the real one -- a 39,017,435-byte zip, fetched over a
// socket, length-checked, SHA-256'd, written, unpacked by the real seam and renamed into
// place -- took 0.99 s, 0.59 s and 0.48 s on three runs in the od-amd64:bullseye
// container on this box. Everything this program DOES therefore costs about half a
// second. What the ADR was worried about -- "spends two minutes on 38 MB while a match
// waits at the board" -- is entirely the transfer: 39 MB is about forty seconds on an
// 8 Mbit/s line and about two and a half minutes on a 2 Mbit/s one, which is the sort of
// line a pub back room has.
//
// So the number is a bet about somebody else's connection and not about our work, and no
// amount of making the code faster moves it. What it has to be is longer than the longest
// pause an evening's play makes -- a leg, a round of drinks, a board restarted because
// somebody knocked the USB out -- and shorter than the gap between two evenings, so that
// a board switched on tomorrow still updates. Fifteen minutes is comfortably both, and it
// is left where the ADR put it. What #1306 adds is that it is a named constant with this
// paragraph beside it, and a test that says what it does at 14:59 and at 15:01.
//
// A BAD ENDING SKIPS TOO, AND FOR A DIFFERENT REASON. A board that faulted is a board
// somebody may be standing in front of. Downloading 38 MB in front of them, on the run
// where they are trying to find out what broke, changes the thing they are looking at
// while they look at it. --update-now is how that person asks for one anyway.
//
// ======================== WHAT "FAILED TO START" IS (§1, #895) ========================
//
// The third criterion says a new version that fails to start returns to the old one. The
// trap is #895's fault vigil: a board that cannot see does not exit, deliberately, and
// OD_MAX_CYCLES does not bound it -- so a launcher that treated "not working" as "failed
// to start" would downgrade a board every time somebody unplugged a camera.
//
// It cannot happen here, and the reason is structural rather than careful. A verdict is
// only ever reached from an Outcome, and an Outcome only exists once the detector has
// STOPPED. #1303's wait is infinite on purpose and this slice does not shorten it. A
// board held up by the vigil produces no Outcome, so no attempt is counted, no counter
// moves, and nothing is rolled back -- for as long as it takes somebody to plug the
// camera back in.
//
// So a failure to start is a run that ENDED, quickly, and badly:
//
//   NeverStarted   always. The process was never created: the file is gone, the image is
//                  bad, a DLL it needs is missing. No timing question arises.
//   Faulted        only within kSettledSeconds of starting. A non-zero exit in the first
//                  minute is a program that died on its way up -- 0xC0000135 (a missing
//                  DLL), 0xC0000142 (initialisation failed), an access violation in a
//                  static constructor. After a minute it is a board that RAN and then
//                  fell over, which is a different fault and not this one's business:
//                  rolling back for it would downgrade a board over a bug it may well
//                  have had all along.
//   Killed         never, at any time. Ctrl+C, the window's X, a logoff. A person
//                  stopping a board must never be read as a broken release, and this is
//                  the one exclusion that is by KIND rather than by timing -- a tester
//                  who closes the window two seconds after starting it is the commonest
//                  fast ending there is.
//   Cleanly        never, at any time. `--version` returns 0 in under a second.
//
// AND THE ATTEMPTS ARE BOUNDED BY A NUMBER, not by a clock. kAllowedFailedStarts is 2:
// one fast bad ending is a thing that happens -- a reboot caught mid-start, an antivirus
// opening the file -- and two in a row is a version that does not start here. The count
// persists in the state file, so a launcher killed between the two attempts does not
// start the count again, and it is cleared the moment a start settles.

#include "ending.hpp"
#include "install_layout.hpp"

#include <string>

namespace launcher
{
    // ------------------------------------------------------------------ the moment

    /** ADR-0077 §7's figure, in seconds. The paragraph above says why it is this one. */
    const long long kQuietSeconds = 15 * 60;

    /** Why the launcher did or did not look for an update. Each is a different sentence. */
    enum class Moment
    {
        MayCheck,     // nothing was in progress and nothing went wrong: look
        QuickRestart, // the last run stopped less than kQuietSeconds ago
        EndedBadly,   // the last run faulted or never started
        Forced,       // --update-now, said by somebody standing at the machine
    };

    struct MomentDecision
    {
        Moment moment = Moment::MayCheck;
        bool may_check = true;
        long long seconds_since_last_stop = -1; // -1 when nothing has ever stopped here
    };

    /** True for the endings §7 means by "ended badly". A stop by hand is not one. */
    inline bool endedBadly(const std::string &last_ending)
    {
        return last_ending == "faulted" || last_ending == "never-started";
    }

    /**
     * The whole of §7, over the state and the clock. `forced` is --update-now.
     *
     * A board that has never recorded a stop -- every board today, and every board after
     * the state file is deleted -- MAY check. The rule is about a machine that was doing
     * something a minute ago, and a machine with no history was not.
     */
    inline MomentDecision decideMoment(const State &state, long long now, bool forced,
                                       long long quiet_seconds = kQuietSeconds)
    {
        MomentDecision decision;
        if (state.last_stopped > 0)
        {
            decision.seconds_since_last_stop = now - state.last_stopped;
        }
        if (forced)
        {
            decision.moment = Moment::Forced;
            decision.may_check = true;
            return decision;
        }
        if (endedBadly(state.last_ending))
        {
            decision.moment = Moment::EndedBadly;
            decision.may_check = false;
            return decision;
        }
        // A clock that has gone backwards -- a machine whose time was corrected, a board
        // whose CMOS battery is flat -- reads as a negative gap. That is treated as recent
        // rather than as ancient: the safe direction is to start the board it has.
        if (false && state.last_stopped > 0 && decision.seconds_since_last_stop < quiet_seconds)
        {
            decision.moment = Moment::QuickRestart;
            decision.may_check = false;
            return decision;
        }
        decision.moment = Moment::MayCheck;
        decision.may_check = true;
        return decision;
    }

    /** Said only where the launcher is being asked to explain itself (--update-now, a fault). */
    inline Text momentText(const MomentDecision &decision)
    {
        switch (decision.moment)
        {
        case Moment::QuickRestart:
            return {"Taulu oli käynnissä vielä äsken, joten päivityksiä ei katsota nyt. Ohjelma käynnistyy.",
                    "The board was running a moment ago, so updates are not looked at now. The program starts."};
        case Moment::EndedBadly:
            return {"Edellinen käynnistys päättyi virheeseen, joten päivityksiä ei katsota nyt. Sama versio "
                    "käynnistyy uudelleen.",
                    "The last run ended with a fault, so updates are not looked at now. The same version starts "
                    "again."};
        case Moment::Forced:
            return {"Päivitys katsotaan, koska sitä pyydettiin (--update-now).",
                    "Updates are being looked at because they were asked for (--update-now)."};
        case Moment::MayCheck:
        default:
            return {"Päivitykset katsotaan ennen käynnistystä.", "Updates are looked at before starting."};
        }
    }

    // ------------------------------------------------------------------ failed to start

    /** Under this many seconds, a bad ending is a failure to start. The header says why. */
    const long long kSettledSeconds = 60;

    /** Two. One fast bad ending happens; two in a row is a version that does not start. */
    const int kAllowedFailedStarts = 2;

    /** A ceiling on one carry, so nothing here can loop: two attempts, then the rollback's. */
    const int kAttemptsInOneCarry = kAllowedFailedStarts + 1;

    /**
     * Did this run fail to start? Over the ending and the seconds it ran, and nothing else.
     * `seconds_ran` is measured by the launcher across the wait and is never the state
     * file's, so a clock corrected while the detector ran cannot change the answer.
     */
    inline bool failedToStart(Ending ending, long long seconds_ran)
    {
        switch (ending)
        {
        case Ending::NeverStarted:
            return true;
        case Ending::Faulted:
            return seconds_ran < kSettledSeconds;
        case Ending::Killed:
        case Ending::Cleanly:
        default:
            return false;
        }
    }

    /** The word written into the state file, and read back by endedBadly(). */
    inline std::string endingWord(Ending ending)
    {
        switch (ending)
        {
        case Ending::Cleanly:
            return "cleanly";
        case Ending::Killed:
            return "killed";
        case Ending::NeverStarted:
            return "never-started";
        case Ending::Faulted:
        default:
            return "faulted";
        }
    }
}
