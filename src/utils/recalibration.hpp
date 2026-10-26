#pragma once
// turnaus#1929: somebody at the board asks it to recalibrate, between legs, and the board
// acts on that by STOPPING AND STARTING AGAIN. The rule, pure, over what the board knows.
//
// THE ASK. turnaus#1928 (API 60.1.0) puts a Recalibrate action on the marking screen. The
// tablet's press is a row on the server; this board finds it the way it finds a request for
// a dart's frames -- GET /api/v1/autoscorer/recalibrations under its own credential, at most
// one pending request, listed for ten minutes -- and answers it once with POST
// .../recalibrations/{reference} {"accepted": true} or {"accepted": false, "refusal": "<its
// own words>"}. Then its heartbeats say CALIBRATING and what it came back as, and the server
// draws that (Recalibrations::heard). Nothing on the wire is new on this side: the beat is
// the one #892 sends and the camera report is #1474's `fitted` / `scoring` / `dark`.
//
// WHY A RESTART AND NOT A RECALIBRATION IN PLACE. Both were open in the issue; the code
// decides it, three ways.
//
//   1. A START IS THE ONE PLACE THIS PROGRAM ADOPTS GEOMETRY, AND IT SAYS SO. ADR-0080 §2
//      and §3: nothing adopts fresh geometry while the board is running; a recalibration
//      "takes effect at the next start, never mid-run". That is not a comment -- the
//      scoring loop compares the geometry it is about to score with against the line sealed
//      at the end of `initialize` on every cycle and faults on a difference
//      (`geometryBreach`, "BOARD ADOPTED GEOMETRY MID-RUN"), and testers/phases1388 trips it
//      on purpose. A recalibration in place is a second adoption path that has to be let
//      through that seal; a restart goes through the front door every start already uses.
//   2. THE WHOLE CALIBRATION IS THE CONSTRUCTOR. Opening the cameras, #1445's further looks,
//      #1605's background, #1330's cache write, #1618's seek alignment, the camera census
//      the beat states, #1388's held Moved verdict refusing it all: every start exercises
//      that, every tester measures it. A second copy reached only from here would be the
//      least-measured path in the program doing the most dangerous thing in it.
//   3. THE RESTART ALREADY EXISTS. #1797's 06:00 stop is a board stopping itself between
//      rounds by the same flag SIGTERM sets, with an exit status of its own that the
//      launcher follows with another start and systemd's Restart=always restarts anyway.
//      This is that with a different number and a different reason -- and NO update look:
//      the launcher reads the status as `recalibrating`, which is not `scheduled`, so
//      ADR-0077 §7's quiet rule holds (the board stopped seconds ago) and it starts the
//      same build again (launcher/ending.hpp, update_moment.hpp).
//
//   What a restart costs is said rather than hidden: a few seconds with no board between
//   the confirmation and the first beat of the new start (RestartSec=2 plus the cameras),
//   and the score socket's subscribers reconnect. Between legs, both are nothing.
//
//   And #1330's one exception: `--reuse-calibration` makes a start READ the cached
//   geometry, so a plain restart under it would recalibrate nothing. The stop therefore
//   writes a marker (below) and the next start that finds it does not reuse the cache, once.
//
// WHEN THE BOARD SAYS YES. The server refuses the press while a Leg is in play
// (`Recalibrations::legInPlayAt`: a Leg nobody has won with a Visit in it, or the board
// holding darts of a round). The board cannot see a Leg -- it publishes darts and takeouts
// and knows nothing about who won -- so what it asks itself is the half of that it does
// know, at the moment it would act rather than at the moment somebody pressed:
//
//   - A ROUND IN HAND (a dart published and no END since; TurnausClient's own `round_`, the
//     same fact the server's second half reads off it) is a leg in play. REFUSED, in words
//     that say so. Not deferred to the END: an END closes a round, not a leg, so a board
//     that waited for it would restart between two rounds of one leg.
//   - A DART SINCE THE LAST POLL THAT ANSWERED means somebody threw after the request could
//     have been made -- the request was not there at that poll -- so a leg may have begun
//     between the press and now. Asked again at the next poll (WAIT), once: a board quiet
//     for one whole poll interval since is a board between legs; one that is thrown at
//     again is in a leg, and the request is REFUSED. This is the gap the server cannot
//     close and the board can.
//   - A HELD `Moved` VERDICT (ADR-0080 §4, geometry_fault.hpp) is REFUSED, always, in words
//     that say so: clearing it is an operator's act with --clear-geometry-fault, and a
//     restart would not clear it anyway -- the next start reads it and refuses to calibrate.
//     The refusal is the truth about what this board would do, said before doing it.
//   - SIGHT LOST AND BEING CONFIRMED (#899's suspended scoring, #1388's bump budget) is
//     REFUSED: ADR-0080 §2 says a board recovering from a knock resumes its HELD geometry,
//     and a start taken in the middle of a knock calibrates on the rig mid-knock.
//   - A REFERENCE ALREADY ANSWERED is never answered again and never acted on again -- the
//     confirmation is once (the server's first one stands anyway), and a start that was the
//     recalibration knows the reference it was started for, so a request the server goes on
//     listing (an answer lost on the way back) does not restart the board twice.
//
// THE CENSUS PREFIX is I1929, so a log can be asked what happened with one grep.

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#include "logging.hpp"

namespace recalibration
{
    /**
     * The exit status of a board that stopped itself to be started again and recalibrate.
     * One number, read by two programs, as scheduled_stop::kExitCode is: the detector returns
     * it from main and the launcher maps it to Ending::Recalibrating. 61: next to the 06:00
     * stop's 60 because it is the same kind of ending, and clear of everything that header
     * lists -- 75 and 78, sysexits 64-78, the launcher's own 40-44, and 124-127.
     */
    const int kExitCode = 61;

    /** What the board knows when it looks at a pending request. */
    struct Facts
    {
        std::string moved;           // geometry_fault::held(): the held Moved account, or empty
        bool sight_suspended = false; // #899: scoring suspended, the held geometry being confirmed
        bool round_in_hand = false;  // a dart published and no END since (TurnausClient::round_)
        bool darts_since_last_poll = false; // a dart offered since the last poll that answered
        bool waited_once = false;    // this reference has already been put off by one poll
        bool already_answered = false; // this reference was confirmed, this run or the one before
    };

    enum class Verdict
    {
        Accept, // confirm accepted, then stop for the restart
        Wait,   // say nothing to the server; it is still listed at the next poll
        Refuse, // confirm refused, with `words`
        Ignore, // answered already: neither confirm nor act
    };

    struct Decision
    {
        Verdict verdict = Verdict::Wait;
        std::string words; // the refusal, as the server keeps it (at most 200 characters)
    };

    /** At most what confirmRecalibration keeps (`refusal`, maxLength 200). */
    const size_t kMostRefusal = 200;

    /** The refusals, word for word. ASCII, under 200 characters, and never translated (#1928). */
    inline std::string movedRefusal()
    {
        return "This board holds a Moved verdict: its frame measured somewhere other than where it was "
               "calibrated. An operator clears that after looking at the rig; a recalibration does not.";
    }
    inline std::string roundRefusal()
    {
        return "A leg is in play: this board holds darts of a round that has not been taken out. Ask "
               "between legs.";
    }
    inline std::string throwingRefusal()
    {
        return "A leg is in play: darts were thrown at this board after the request was made. Ask "
               "between legs.";
    }
    inline std::string sightRefusal()
    {
        return "This board lost sight of the board and is confirming the calibration it holds. Ask again "
               "once it is scoring.";
    }

    /**
     * The rule. Order is the answer to "which reason is said when two hold": an answered
     * reference first (nothing is said twice), then the verdict that no restart can change
     * (Moved), then the board's own sight, then the leg -- and a round in hand before the
     * darts-since-the-poll, because it is the certain one of the two.
     */
    inline Decision decide(const Facts &facts)
    {
        Decision decision;
        if (facts.already_answered)
        {
            decision.verdict = Verdict::Ignore;
            return decision;
        }
        if (!facts.moved.empty())
        {
            decision.verdict = Verdict::Refuse;
            decision.words = movedRefusal();
            return decision;
        }
        if (facts.sight_suspended)
        {
            decision.verdict = Verdict::Refuse;
            decision.words = sightRefusal();
            return decision;
        }
        if (facts.round_in_hand)
        {
            decision.verdict = Verdict::Refuse;
            decision.words = roundRefusal();
            return decision;
        }
        if (facts.darts_since_last_poll)
        {
            if (facts.waited_once)
            {
                decision.verdict = Verdict::Refuse;
                decision.words = throwingRefusal();
                return decision;
            }
            decision.verdict = Verdict::Wait;
            return decision;
        }
        decision.verdict = Verdict::Accept;
        return decision;
    }

    // ------------------------------------------------------------------ the marker

    /**
     * Written by the stop, read and removed by the next start. Beside geometry_moved.txt and
     * named by the working directory for that file's reason (geometry_fault.hpp): what it
     * can cost is one start that calibrates afresh where it would have reused the cache,
     * which is what was asked for anyway.
     *
     * Three lines: the reference, then the census and the sealed geometry the board was
     * scoring with when it stopped -- so the start after can log BEFORE and AFTER for each
     * camera in one place (#1929's rig criterion).
     */
    inline std::string markerPath()
    {
        return "cache/recalibration_asked.txt";
    }

    struct Marker
    {
        std::string reference;
        std::string census_before;
        std::string geometry_before;
    };

    inline bool writeMarker(const Marker &marker)
    {
        (void)odfs::ensureDirectory("cache");
        std::ofstream file(markerPath(), std::ios::trunc);
        if (!file)
        {
            return false;
        }
        file << marker.reference << "\n" << marker.census_before << "\n" << marker.geometry_before << "\n";
        return (bool)file;
    }

    /** The marker a previous stop left, removed as it is read. Empty reference: none. */
    inline Marker takeMarker()
    {
        Marker marker;
        std::ifstream file(markerPath());
        if (!file)
        {
            return marker;
        }
        std::getline(file, marker.reference);
        std::getline(file, marker.census_before);
        std::getline(file, marker.geometry_before);
        file.close();
        std::remove(markerPath().c_str());
        while (!marker.reference.empty() && (marker.reference.back() == '\r' || marker.reference.back() == ' '))
        {
            marker.reference.pop_back();
        }
        return marker;
    }

    // ------------------------------------------------------------------ the sentences

    inline std::string verdictWord(Verdict verdict)
    {
        switch (verdict)
        {
        case Verdict::Accept:
            return "ACCEPTED";
        case Verdict::Refuse:
            return "REFUSED";
        case Verdict::Ignore:
            return "ALREADY ANSWERED";
        case Verdict::Wait:
        default:
            return "PUT OFF";
        }
    }

    /** The stop, said once, with what the board was scoring on. */
    inline std::string stopSentence(const std::string &reference, const std::string &census)
    {
        return "I1929 RECALIBRATION " + reference + ": stopping so the board starts again and calibrates afresh "
               "(scoring with " + (census.empty() ? std::string("an unknown census") : census) + ")";
    }
}
