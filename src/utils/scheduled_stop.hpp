#pragma once
// #1797: a board still running at 06:00 local stops itself, so the launcher can look for
// an update and start it again. The rule, pure, over a clock and two facts.
//
// WHY A BOARD THAT WORKS HAS TO STOP. ADR-0077 §7 says a board updates before it starts
// and never after a quick restart or a fault, and update_moment.hpp honours that to the
// letter: the launcher asks Turnaus for the channel's manifest only on a start that
// follows a clean stop at least kQuietSeconds old. A board that is online around the
// clock therefore never starts, and a release published on its channel reaches it only
// when somebody restarts it by hand. The maintainer's decision of 2026-10-10: "no one is
// playing at 6am". A board still running at 06:00 local stops then, with an ending of its
// own, and the restart is what lets the launcher look. No push from Turnaus -- the beat
// answer carries the interval and the silence and stays that way -- and no idle
// heuristics beyond the one guard below; both were considered and refused.
//
// THE CLOCK IS LOCAL, by the machine's zone, through the same std::localtime that
// logging.hpp stamps every log line with. A reader of the log sees `06:00:00.xxx` on the
// stamp and `at 06:00` in the sentence, and they are the same clock. The pure half takes
// unix seconds and asks localtime for the wall; a check builds its instants with mktime,
// which is localtime's inverse on the same machine, so the check reads true in any zone.
//
// THE RULE. A detector started before 06:00 local is due to stop at 06:00:00 that day; a
// detector started at or after 06:00 is due the next day's 06:00, so a board restarted at
// 06:01 by the launcher does not stop again at once, and a board started at 09:00 runs
// the evening. When the due instant arrives, ONE guard, stated in the log each time it
// fires: a dart published in the last kGuardSeconds (ten minutes), or a round the board
// has not yet seen taken out (a dart published with no END since), postpones the stop to
// the next ten-minute mark -- 06:10, then 06:20, and so on until a poll finds the board
// quiet. The facts behind the guard are the scorer's own publishes: `SCORE: <x>` sets the
// dart's time and opens the round, `SCORE: END` closes it (scorer.cpp, sendResult).
//
// WHAT THE STOP IS. The ordinary clean shutdown, by the same flag SIGINT and SIGTERM set
// (#825's exit path), so every shutdown duty runs -- the announcement withdrawn, the
// score socket joined, #1787's final log post and `client stopped` -- and the ONLY
// difference is the exit status, kExitCode below, which the launcher reads as the word
// "scheduled" and not as a fault (ending.hpp, update_moment.hpp).
//
// WHAT THIS HEADER DOES NOT DO. It reads no environment and no real clock. The pin
// OD_SCHEDULED_RESTART=off and the test-only OD_SCHEDULED_CLOCK are read beside the
// other OD_* pins in scorer.cpp, which hands this header a `now` and the two facts and
// logs what it answers. testers/i1797_schedule_check.cpp drives it with a fake clock,
// and the launcher side of the same slice is measured in the same check.

#include <cstdio>
#include <ctime>
#include <string>

namespace scheduled_stop
{
    /**
     * The exit status of a scheduled stop. One number, read by two programs: the detector
     * returns it from main (Scorer::kScheduledStop is this constant) and the launcher maps
     * it to Ending::Scheduled (ending.hpp's kScheduledStop is this constant too).
     *
     * 60: above 0, below 128, and away from everything a reader could confuse it with --
     * 75 (Scorer::kCouldNotSee, EX_TEMPFAIL) and 78 (EX_CONFIG) which the detector already
     * returns, the sysexits range 64-78 generally, the launcher's own 40-42 (ending.hpp)
     * so a Task Scheduler reader is never looking at the child's number, and 124-127 which
     * a shell and `timeout` spell their own failures with. Sixty is also six o'clock said
     * as a number, which is a mnemonic and not a reason.
     */
    const int kExitCode = 60;

    /** The local hour and minute the board stops at. The header above says why six. */
    const int kStopHour = 6;
    const int kStopMinute = 0;

    /** The one guard: a dart published within this many seconds postpones the stop. */
    const long long kGuardSeconds = 10 * 60;

    /** A postponed stop is due again at the next mark this many seconds apart (06:10, 06:20, ...). */
    const int kStepSeconds = 10 * 60;

    /** The two facts the guard reads, kept by the scorer beside its publishes. */
    struct Facts
    {
        long long last_dart_at = 0; // unix seconds of the last `SCORE: <x>` that was not END; 0 when none
        bool round_open = false;    // a dart has been published and no END has followed it
    };

    /** What one poll of the rule answered. */
    enum class Verdict
    {
        NotYet,    // the due instant has not arrived
        Postponed, // it arrived, the guard fired, and `due_at` moved to the next mark
        Stop,      // it arrived and the board is quiet: stop now
    };

    struct Outcome
    {
        Verdict verdict = Verdict::NotYet;
        long long due_at = 0; // when the stop is (now) due, unix seconds
        std::string reason;   // why it was postponed; empty otherwise
    };

    // ------------------------------------------------------------------ the local clock

    /** std::localtime, as logging.hpp uses it. One place, so a zone question has one answer. */
    inline std::tm localOf(long long unix_seconds)
    {
        const std::time_t t = static_cast<std::time_t>(unix_seconds);
        std::tm out = {};
#if defined(_WIN32)
        localtime_s(&out, &t);
#else
        localtime_r(&t, &out);
#endif
        return out;
    }

    /** HH:MM of a unix instant, in local time, for the sentences. */
    inline std::string clockText(long long unix_seconds)
    {
        const std::tm local = localOf(unix_seconds);
        char text[8];
        std::snprintf(text, sizeof(text), "%02d:%02d", local.tm_hour, local.tm_min);
        return std::string(text);
    }

    /** YYYY-MM-DD HH:MM of a unix instant, in local time, for the line that says when the stop is due. */
    inline std::string dateClockText(long long unix_seconds)
    {
        const std::tm local = localOf(unix_seconds);
        char text[24];
        std::snprintf(text, sizeof(text), "%04d-%02d-%02d %02d:%02d", local.tm_year + 1900, local.tm_mon + 1,
                      local.tm_mday, local.tm_hour, local.tm_min);
        return std::string(text);
    }

    /**
     * The stop hour on the day `unix_seconds` falls in, as a unix instant, plus `days`.
     * mktime with tm_isdst = -1, so a clock change between the start and 06:00 is the
     * zone's business and not this file's.
     */
    inline long long stopHourOn(long long unix_seconds, int days = 0)
    {
        std::tm local = localOf(unix_seconds);
        local.tm_hour = kStopHour;
        local.tm_min = kStopMinute;
        local.tm_sec = 0;
        local.tm_mday += days;
        local.tm_isdst = -1;
        return static_cast<long long>(std::mktime(&local));
    }

    /**
     * When a detector started at `started_at` is first due to stop: 06:00 that day when it
     * started before 06:00, otherwise the next day's. "At or after" is strict: a start at
     * 06:00:00 exactly is after the stop and waits a day, so the board the launcher starts
     * again at 06:00:xx does not stop twice.
     */
    inline long long firstDue(long long started_at)
    {
        const long long today = stopHourOn(started_at);
        return started_at < today ? today : stopHourOn(started_at, 1);
    }

    /** The next kStepSeconds mark strictly after `now`, local: 06:00:00 -> 06:10:00, 06:10:00 -> 06:20:00. */
    inline long long nextMark(long long now)
    {
        std::tm local = localOf(now);
        const int step_minutes = kStepSeconds / 60;
        local.tm_min = (local.tm_min / step_minutes + 1) * step_minutes;
        local.tm_sec = 0;
        local.tm_isdst = -1;
        return static_cast<long long>(std::mktime(&local));
    }

    // ------------------------------------------------------------------ the rule

    /**
     * One poll. `due_at` is what the previous poll (or firstDue) left; the caller keeps
     * the Outcome's due_at for the next poll. Pure: the same arguments give the same
     * answer, which is what testers/i1797_schedule_check.cpp relies on.
     */
    inline Outcome poll(long long due_at, long long now, const Facts &facts)
    {
        Outcome outcome;
        outcome.due_at = due_at;
        if (now < due_at)
        {
            outcome.verdict = Verdict::NotYet;
            return outcome;
        }
        if (facts.last_dart_at > 0 && now - facts.last_dart_at < kGuardSeconds)
        {
            const long long ago = now - facts.last_dart_at;
            outcome.verdict = Verdict::Postponed;
            outcome.due_at = nextMark(now);
            outcome.reason = "a dart was published " + std::to_string(ago) + " s ago, inside the " +
                             std::to_string(kGuardSeconds / 60) + "-minute guard";
            return outcome;
        }
        if (facts.round_open)
        {
            outcome.verdict = Verdict::Postponed;
            outcome.due_at = nextMark(now);
            outcome.reason = "a round is still on the board (a dart was published and no END has followed it)";
            return outcome;
        }
        outcome.verdict = Verdict::Stop;
        return outcome;
    }

    // ------------------------------------------------------------------ the sentences

    /** Said once, at the stop. The census prefix is I1797. */
    inline std::string stopSentence(long long stopped_at)
    {
        return "I1797 SCHEDULED RESTART at " + clockText(stopped_at) +
               ": the launcher looks for an update and starts the board again";
    }

    /** Said once per postponement, with the mark it moved to and why. */
    inline std::string postponedSentence(const Outcome &outcome)
    {
        return "I1797 SCHEDULED RESTART postponed to " + clockText(outcome.due_at) + ": " + outcome.reason;
    }

    // ------------------------------------------------------------------ one board's schedule

    /**
     * The rule with its one piece of memory -- when the stop is due -- so a loop has one
     * call per cycle and says something only when the answer changed. `enabled` false is
     * the pin OD_SCHEDULED_RESTART=off: every poll answers NotYet and nothing is ever due.
     */
    class Schedule
    {
    public:
        Schedule() : enabled_(false), due_at_(0) {}
        Schedule(bool enabled, long long started_at) : enabled_(enabled), due_at_(firstDue(started_at)) {}

        bool enabled() const { return enabled_; }
        long long dueAt() const { return due_at_; }

        Outcome poll(long long now, const Facts &facts)
        {
            if (!enabled_)
            {
                Outcome quiet;
                quiet.verdict = Verdict::NotYet;
                quiet.due_at = due_at_;
                return quiet;
            }
            const Outcome outcome = scheduled_stop::poll(due_at_, now, facts);
            due_at_ = outcome.due_at;
            return outcome;
        }

    private:
        bool enabled_;
        long long due_at_;
    };
}
