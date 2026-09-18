#pragma once
// #892: the five words a board may say about itself, and the three facts that decide
// which one is true right now.
//
// The beat exists because `autoscorer_conditions.checked_at` is written by a dart and by
// nothing else, so a healthy board at an idle Station reads as `silent` inside a minute
// (ADR-0055, ADR-0065). What the beat asserts is two claims and the server observes only
// one of them: *a program holding this board's credential is running and can reach us* is
// asserted by the request arriving at all; *it can see the board* is this word, and it is
// self-asserted. The only server-side proof of sight is a dart, and a dart is not a beat.
//
// So READY is not "the process is up". It is the claim that clears the `silent` state and
// therefore the claim that leaves a Match at this Station with no human Marker on Duty. A
// blind detector beating READY is exactly the failure ADR-0055 calls the one that must be
// impossible, arriving through an honest-looking message. The ladder below is what earns
// the word:
//
//   faulted            -> ERROR         a camera would not open, or calibration failed
//   cameras not open   -> INITIALISING  the program is up and the hardware is not
//   not calibrated     -> CALIBRATING   the recalibration that happens on every start
//   no frame ever read -> CALIBRATING   the loop has not produced its first frame
//   a frame since the  -> READY         cameras open, calibration valid, frames arriving
//     last beat
//   otherwise          -> ERROR         running, and blind
//
// The last rung is the one worth reading twice. "Frames are arriving" is measured against
// the previous beat rather than against a compiled-in freshness window: a board that has
// read no frame since it last said it could see has stopped seeing, whatever the interval
// happens to be. So the only number in the sentence is the server's own, and there is no
// second window to keep in step with a first.
//
// The facts are written where they are observed and nowhere else -- `Scorer`'s constructor
// for the cameras and the calibration, `Scorer`'s loop for the frames -- in the accessor
// shape `od_clock` and `od_fix` already use, which is #817's rule: one copy of the state,
// held in a function-local static, and not a header variable each translation unit gets
// its own of.

#include <atomic>
#include <cstdint>
#include <string>

namespace board_sight
{
    enum class Condition
    {
        Initialising,
        Calibrating,
        Ready,
        Error
    };

    /**
     * The word as the server spells it. `UNKNOWN` and `OFFLINE` are deliberately absent
     * and are not omissions: the first is the conclusion the silence window draws for
     * itself, and the second is a claim a board cannot make over a working connection --
     * a detector that cannot reach the server tells it so by not beating (ADR-0065).
     */
    inline const char *word(Condition condition)
    {
        switch (condition)
        {
        case Condition::Initialising:
            return "INITIALISING";
        case Condition::Calibrating:
            return "CALIBRATING";
        case Condition::Ready:
            return "READY";
        default:
            return "ERROR";
        }
    }

    /** The cameras were opened. Written once, by Scorer's constructor. */
    inline std::atomic<bool> &camerasOpen()
    {
        static std::atomic<bool> v{false};
        return v;
    }

    /** The detector calibrated on the frames it was handed. Written once, the same place. */
    inline std::atomic<bool> &calibrated()
    {
        static std::atomic<bool> v{false};
        return v;
    }

    /**
     * Something at the machine will not come up: a camera that would not open, or a
     * calibration that failed on the frames it got. Either one is a board that cannot
     * see, so both are ERROR rather than silence -- ERROR is the word that tells the pub
     * to go and look at the computer, and silence is the word that says the same thing
     * about a machine nobody can ask.
     *
     * #899 corrected the sentence that used to be in the middle of that one: "neither is
     * a state a retry improves". It was true of the two faults named above and it was
     * read as a fact about the flag, which it is not. An unplugged camera IS improved by
     * a retry, and since #899 a board that loses every camera mid-run retries until they
     * come back -- without setting this flag, because it has not failed, it has stopped.
     * What is still true is what this flag means: once it is set the board does not score
     * again in this process. The two constructor faults set it, and so does the one
     * finding a retry cannot improve -- the cameras came back and the board is not where
     * it was (scorer.cpp, attemptRecovery).
     */
    inline std::atomic<bool> &faulted()
    {
        static std::atomic<bool> v{false};
        return v;
    }

    /**
     * #1321: the sentence `BOARD FAULTED` says instead of offering two alternatives and
     * committing to neither. It is written where the fault is observed -- the camera
     * branch of Scorer's constructor names the sources it tried, the calibration path
     * names the camera and the count that fell short -- and read once, from the vigil,
     * on the same thread. First fault wins, because the first thing that could not come
     * up is the thing to go and look at; everything after it is a consequence.
     *
     * The string is a function-local static that is deliberately never destroyed, for
     * #817's reason in `logging::logFilePath()`: this process logs from threads that are
     * still running when exit() runs destructors, and a destroyed string read as a
     * message is a freed pointer.
     */
    inline std::string &faultDetail()
    {
        static std::string *detail = new std::string(); // owned for the life of the process
        return *detail;
    }

    /** Record what could not come up, if nothing has been recorded yet. */
    inline void recordFault(const std::string &detail)
    {
        if (faultDetail().empty())
        {
            faultDetail() = detail;
        }
    }

    /**
     * How many cycles of the scoring loop have read at least one valid frame. Monotonic,
     * incremented on the scoring thread with a relaxed store, and read from the beat
     * thread. It is a count rather than a timestamp because the question the beat asks is
     * "any frame since I last asked", which a count answers without either thread needing
     * a clock they agree about.
     */
    inline std::atomic<uint64_t> &framesSeen()
    {
        static std::atomic<uint64_t> v{0};
        return v;
    }

    /**
     * The word for this instant, given the frame count when the caller last asked.
     * `frames_at_last_ask` is updated in place, so the beat thread carries the whole of
     * its own state in one local.
     */
    inline Condition conditionSince(uint64_t &frames_at_last_ask, bool &asked_since_calibration)
    {
        const uint64_t now = framesSeen().load(std::memory_order_relaxed);
        const uint64_t before = frames_at_last_ask;
        frames_at_last_ask = now;

        if (faulted().load())
        {
            return Condition::Error;
        }
        if (!camerasOpen().load())
        {
            return Condition::Initialising;
        }
        if (!calibrated().load())
        {
            return Condition::Calibrating;
        }
        if (now == 0 && !asked_since_calibration)
        {
            // Calibration has just finished and the loop has not yet produced its first
            // frame. That is still coming up rather than blind, and it is a real
            // interval: the beat starts before the cameras do, so a beat can land in the
            // microseconds between the calibration flag and the first cycle.
            //
            // ONCE, though. A board still on nought frames when the next beat is due has
            // had a whole interval to read one and has not, and saying CALIBRATING for
            // ever about a machine that has never seen anything is the same lie as
            // saying READY about it -- both send a Station's screen on scoring itself.
            asked_since_calibration = true;
            return Condition::Calibrating;
        }
        asked_since_calibration = true;
        return now > before ? Condition::Ready : Condition::Error;
    }
}
