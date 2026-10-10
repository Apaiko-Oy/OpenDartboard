// #1797: the 06:00 scheduled stop, measured -- the clock rule and its one guard driven
// with a fake clock, and the launcher's side of the same stop.
//
// PREDICTION, STATED FIRST. Over src/utils/scheduled_stop.hpp:
//
//   a detector started at 05:00, polled at 05:59:59        NotYet
//   the same, polled at 06:00:00                           Stop, and the sentence says "at 06:00"
//   06:00:00 with a dart published at 05:56                Postponed to 06:10, the reason names the dart
//   06:00:00 with a round not yet taken out (no END)       Postponed to 06:10, the reason names the round
//   06:10:00 after that postponement, the board quiet      Stop
//   06:10:00 with another dart at 06:04                    Postponed to 06:20
//   a detector started at 06:30                            NotYet all day; Stop at the next day's 06:00
//   a detector started at 06:00:00 exactly                 waits for the next day (at-or-after is strict)
//   the pin off (Schedule(false, ...))                     NotYet at 06:00 and at every other instant
//
// Over the launcher headers, with `last_ending` = "scheduled" two seconds after the stop:
//
//   decideMoment()      Moment::Scheduled, may_check TRUE at a 2-second gap
//   control             "cleanly" at the same gap is QuickRestart and may NOT check
//   control             "faulted" is EndedBadly, unchanged
//   endingOf(code 60)   Ending::Scheduled; endingWord() "scheduled"; endedBadly("scheduled") false;
//                       failedToStart(Scheduled, 1 s) false
//
// MUTATION PROOFS, run by testers/i1797_check.sh on a scratch copy of src/:
//   --mutate-clock      kStopHour 6 -> 7: every clock case that expects a stop at 06:00
//                       goes red, and the FAIL lines name the hour the rule answered
//   --mutate-ending     the kScheduledStop branch of endingOf() deleted: the launcher
//                       case goes red (60 reads as Faulted, "faulted", endedBadly)
//
// The instants are built with mktime from local broken-down times, which is localtime's
// inverse on the same machine, so the check reads true in the container's zone (UTC) and
// on the maintainer's box (Europe/Helsinki) alike. A date in July is used so the DST
// question does not enter; the rule itself hands DST to mktime (tm_isdst = -1).
//
//   testers/unit_check.sh 1797      this file alone
//   testers/i1797_check.sh          this, the re-carry and the mutation proofs

#include "launcher/ending.hpp"
#include "launcher/update_moment.hpp"
#include "utils/scheduled_stop.hpp"

#include <cstdio>
#include <ctime>
#include <string>

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

/** A local instant on 2026-07-14, the day every case below lives on. */
static long long at(int hour, int minute, int second, int day = 14)
{
    std::tm local = {};
    local.tm_year = 2026 - 1900;
    local.tm_mon = 6; // July
    local.tm_mday = day;
    local.tm_hour = hour;
    local.tm_min = minute;
    local.tm_sec = second;
    local.tm_isdst = -1;
    return static_cast<long long>(std::mktime(&local));
}

static std::string hhmmss(long long unix_seconds)
{
    const std::tm local = scheduled_stop::localOf(unix_seconds);
    char text[16];
    std::snprintf(text, sizeof(text), "%02d:%02d:%02d", local.tm_hour, local.tm_min, local.tm_sec);
    return std::string(text);
}

static const char *word(scheduled_stop::Verdict verdict)
{
    switch (verdict)
    {
    case scheduled_stop::Verdict::NotYet:
        return "NotYet";
    case scheduled_stop::Verdict::Postponed:
        return "Postponed";
    case scheduled_stop::Verdict::Stop:
        return "Stop";
    default:
        return "?";
    }
}

/** One poll, with the answer spelled out so a red line says what the rule did answer. */
static scheduled_stop::Outcome polled(scheduled_stop::Schedule &schedule, long long now,
                                      const scheduled_stop::Facts &facts, const std::string &label)
{
    const scheduled_stop::Outcome outcome = schedule.poll(now, facts);
    std::printf("     %-44s at %s -> %-9s due %s%s%s\n", label.c_str(), hhmmss(now).c_str(), word(outcome.verdict),
                scheduled_stop::dateClockText(outcome.due_at).c_str(), outcome.reason.empty() ? "" : ": ",
                outcome.reason.c_str());
    return outcome;
}

int main()
{
    using namespace scheduled_stop;
    const Facts quiet;

    std::printf("---- the clock rule, with a fake clock (local zone of this machine) ----\n");

    // ---- started at 05:00: due 06:00 today -------------------------------------------
    {
        Schedule schedule(true, at(5, 0, 0));
        check(schedule.dueAt() == at(6, 0, 0),
              "started at 05:00, the stop is due at 06:00:00 the same day (due " + hhmmss(schedule.dueAt()) + ")");

        const Outcome before = polled(schedule, at(5, 59, 59), quiet, "05:59:59, quiet");
        check(before.verdict == Verdict::NotYet, "05:59:59 does not stop (answered " + std::string(word(before.verdict)) + ")");

        const Outcome six = polled(schedule, at(6, 0, 0), quiet, "06:00:00, quiet");
        check(six.verdict == Verdict::Stop, "06:00:00 stops (answered " + std::string(word(six.verdict)) + ", due " +
                                                hhmmss(six.due_at) + ")");
        const std::string sentence = stopSentence(at(6, 0, 0));
        check(sentence == "I1797 SCHEDULED RESTART at 06:00: the launcher looks for an update and starts the board again",
              "and the sentence is the documented one: " + sentence);
    }

    // ---- 06:00 with a dart four minutes earlier: postponed to 06:10, and says why ---------
    {
        Schedule schedule(true, at(5, 0, 0));
        Facts facts;
        facts.last_dart_at = at(5, 56, 0);
        facts.round_open = false; // taken out since, so only the ten-minute guard speaks
        const Outcome guarded = polled(schedule, at(6, 0, 0), facts, "06:00:00, a dart at 05:56");
        check(guarded.verdict == Verdict::Postponed,
              "06:00:00 with a dart at 05:56 is postponed (answered " + std::string(word(guarded.verdict)) + ")");
        check(guarded.due_at == at(6, 10, 0), "  to 06:10:00 (due " + hhmmss(guarded.due_at) + ")");
        check(guarded.reason.find("dart") != std::string::npos && guarded.reason.find("240 s") != std::string::npos,
              "  and the reason names the dart and how long ago: " + guarded.reason);
        const std::string sentence = postponedSentence(guarded);
        check(sentence.rfind("I1797 SCHEDULED RESTART postponed to 06:10: ", 0) == 0,
              "  said as the documented sentence: " + sentence);

        const Outcome between = polled(schedule, at(6, 5, 0), facts, "06:05:00, the same dart");
        check(between.verdict == Verdict::NotYet, "  06:05 is before the new mark, so nothing is said again");

        const Outcome later = polled(schedule, at(6, 10, 0), facts, "06:10:00, the dart now 14 min old");
        check(later.verdict == Verdict::Stop, "  06:10:00 with the dart 14 minutes old stops (answered " +
                                                  std::string(word(later.verdict)) + ")");
        check(stopSentence(at(6, 10, 0)).find("at 06:10") != std::string::npos,
              "  and the stop sentence then says the time it really stopped at: " + stopSentence(at(6, 10, 0)));
    }

    // ---- 06:00 with a round not taken out: postponed to 06:10 --------------------------
    {
        Schedule schedule(true, at(5, 0, 0));
        Facts facts;
        facts.last_dart_at = at(5, 40, 0); // twenty minutes ago: the dart guard is silent
        facts.round_open = true;           // but no END has followed it
        const Outcome open = polled(schedule, at(6, 0, 0), facts, "06:00:00, a round still on the board");
        check(open.verdict == Verdict::Postponed && open.due_at == at(6, 10, 0),
              "06:00:00 with a round not taken out waits for 06:10 (answered " + std::string(word(open.verdict)) +
                  ", due " + hhmmss(open.due_at) + ")");
        check(open.reason.find("round") != std::string::npos && open.reason.find("END") != std::string::npos,
              "  and the reason names the round and the END it waits for: " + open.reason);

        facts.round_open = false; // END published at 06:03
        const Outcome taken_out = polled(schedule, at(6, 10, 0), facts, "06:10:00, taken out since");
        check(taken_out.verdict == Verdict::Stop, "  06:10:00 after the takeout stops");
    }

    // ---- the guard goes on postponing, mark by mark ---------------------------------------
    {
        Schedule schedule(true, at(5, 0, 0));
        Facts facts;
        facts.last_dart_at = at(5, 58, 0);
        const Outcome first = polled(schedule, at(6, 0, 0), facts, "06:00:00, a dart at 05:58");
        facts.last_dart_at = at(6, 4, 0);
        const Outcome second = polled(schedule, at(6, 10, 0), facts, "06:10:00, a dart at 06:04");
        check(first.due_at == at(6, 10, 0) && second.verdict == Verdict::Postponed && second.due_at == at(6, 20, 0),
              "a dart at 06:04 postpones the 06:10 stop to 06:20 (due " + hhmmss(second.due_at) + ")");
        facts.last_dart_at = at(6, 9, 59);
        const Outcome third = polled(schedule, at(6, 20, 0), facts, "06:20:00, a dart at 06:09:59");
        check(third.verdict == Verdict::Stop, "  and at 06:20 a dart 10:01 old is outside the guard: stop");
    }

    // ---- started at 06:30: the next day's 06:00 ---------------------------------------
    {
        Schedule schedule(true, at(6, 30, 0));
        check(schedule.dueAt() == at(6, 0, 0, 15),
              "started at 06:30, the stop is due at 06:00 the NEXT day (due " + dateClockText(schedule.dueAt()) + ")");
        const Outcome evening = polled(schedule, at(21, 40, 0), quiet, "21:40 the same day");
        const Outcome midnight = polled(schedule, at(0, 0, 0, 15), quiet, "00:00 the next day");
        const Outcome nearly = polled(schedule, at(5, 59, 59, 15), quiet, "05:59:59 the next day");
        check(evening.verdict == Verdict::NotYet && midnight.verdict == Verdict::NotYet && nearly.verdict == Verdict::NotYet,
              "  and nothing stops it through the evening, midnight and 05:59:59");
        const Outcome tomorrow = polled(schedule, at(6, 0, 0, 15), quiet, "06:00:00 the next day");
        check(tomorrow.verdict == Verdict::Stop, "  06:00:00 the next day stops (answered " +
                                                     std::string(word(tomorrow.verdict)) + ")");
    }

    // ---- started at 06:00:00 exactly: that is after the stop -----------------------------
    {
        Schedule schedule(true, at(6, 0, 0));
        check(schedule.dueAt() == at(6, 0, 0, 15),
              "started at 06:00:00 exactly -- the launcher's restart -- waits for the next day (due " +
                  dateClockText(schedule.dueAt()) + ")");
        Schedule just_before(true, at(5, 59, 59));
        check(just_before.dueAt() == at(6, 0, 0), "  while a start at 05:59:59 is due a second later");
    }

    // ---- the pin off: never ---------------------------------------------------------------
    {
        Schedule off(false, at(5, 0, 0));
        check(!off.enabled(), "the pin off: the schedule says it is not enabled");
        const Outcome six = polled(off, at(6, 0, 0), quiet, "06:00:00, pin off");
        const Outcome ten = polled(off, at(6, 10, 0), quiet, "06:10:00, pin off");
        const Outcome day = polled(off, at(6, 0, 0, 15), quiet, "06:00:00 next day, pin off");
        check(six.verdict == Verdict::NotYet && ten.verdict == Verdict::NotYet && day.verdict == Verdict::NotYet,
              "  and it never stops: 06:00, 06:10 and the next day's 06:00 all answer NotYet");
    }

    // ---- the pure function itself: the same arguments, the same answer --------------------
    {
        const Outcome a = poll(at(6, 0, 0), at(6, 0, 0), quiet);
        const Outcome b = poll(at(6, 0, 0), at(6, 0, 0), quiet);
        check(a.verdict == b.verdict && a.due_at == b.due_at && a.reason == b.reason,
              "poll() is pure: the same clock and facts give the same answer twice");
        check(nextMark(at(6, 0, 0)) == at(6, 10, 0) && nextMark(at(6, 0, 1)) == at(6, 10, 0) &&
                  nextMark(at(6, 9, 59)) == at(6, 10, 0) && nextMark(at(6, 10, 0)) == at(6, 20, 0),
              "nextMark: 06:00:00, 06:00:01 and 06:09:59 -> 06:10:00; 06:10:00 -> 06:20:00");
        check(nextMark(at(23, 55, 0)) == at(0, 0, 0, 15), "  and 23:55 -> 00:00 the next day, through mktime");
    }

    std::printf("---- the launcher's side ----\n");
    {
        using namespace launcher;
        const long long stopped_at = at(6, 0, 0);
        const long long now = stopped_at + 2;

        State scheduled;
        scheduled.last_stopped = stopped_at;
        scheduled.last_ending = "scheduled";
        const MomentDecision fourth = decideMoment(scheduled, now, false);
        check(fourth.moment == Moment::Scheduled && fourth.may_check,
              "decideMoment(): last_ending \"scheduled\" 2 s after the stop is Moment::Scheduled and MAY check");
        check(fourth.seconds_since_last_stop == 2, "  and the gap it saw really was 2 s");

        State cleanly = scheduled;
        cleanly.last_ending = "cleanly";
        const MomentDecision control = decideMoment(cleanly, now, false);
        check(control.moment == Moment::QuickRestart && !control.may_check,
              "CONTROL: \"cleanly\" at the same 2 s gap is QuickRestart and may NOT check");

        State faulted = scheduled;
        faulted.last_ending = "faulted";
        const MomentDecision bad = decideMoment(faulted, now, false);
        check(bad.moment == Moment::EndedBadly && !bad.may_check, "\"faulted\" is EndedBadly, unchanged");
        check(!endedBadly("scheduled") && endedBadly("faulted") && endedBadly("never-started"),
              "endedBadly(): \"scheduled\" is not a bad ending; \"faulted\" and \"never-started\" still are");

        const MomentDecision forced = decideMoment(faulted, now, true);
        check(forced.moment == Moment::Forced && forced.may_check, "--update-now still wins over everything");

        const Text text = momentText(fourth);
        check(!text.fi.empty() && !text.en.empty() && text.en.find("06:00") != std::string::npos,
              "Moment::Scheduled has its own sentence in both languages: " + text.en);

        Outcome outcome;
        outcome.started = true;
        outcome.code = static_cast<unsigned long>(scheduled_stop::kExitCode);
        check(endingOf(outcome) == Ending::Scheduled,
              "endingOf(): exit code " + std::to_string(scheduled_stop::kExitCode) + " is Ending::Scheduled (got " +
                  endingWord(endingOf(outcome)) + ")");
        check(endingWord(Ending::Scheduled) == "scheduled", "  and its word is \"scheduled\"");
        check(kScheduledStop == 60 && scheduled_stop::kExitCode == 60,
              "  the number is 60 on both sides, from the one header");
        check(!failedToStart(Ending::Scheduled, 1), "  a scheduled stop one second in is not a failure to start");
        check(endingOf(Outcome{true, 0, false, 0, ""}) == Ending::Cleanly &&
                  endingOf(Outcome{true, 1, false, 0, ""}) == Ending::Faulted &&
                  endingOf(Outcome{true, 75, false, 0, ""}) == Ending::Faulted,
              "  0 is still Cleanly; 1 and 75 (kCouldNotSee) are still Faulted");
        const std::vector<Text> lines = endingLines(outcome);
        bool both = !lines.empty();
        for (size_t i = 0; i < lines.size(); i++)
        {
            both = both && !lines[i].fi.empty() && !lines[i].en.empty();
        }
        check(both && lines[0].en.find("06:00") != std::string::npos,
              "  the ending's own lines name 06:00 in both languages: " + lines[0].en);
        check(exitCodeFor(Ending::Scheduled) != 0 && exitCodeFor(Ending::Scheduled) != kFaulted &&
                  exitCodeFor(Ending::Scheduled) != kKilled && exitCodeFor(Ending::Scheduled) != kNeverStarted,
              "  a scheduled stop the launcher refused to follow has an exit code of its own");
    }

    std::printf("%d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
