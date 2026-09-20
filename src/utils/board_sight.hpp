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
#include <cstdlib>
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

    // ---------------------------------------------------------------------------------
    // #1474: how many of this board's cameras are scoring, as three machine counts.
    // ---------------------------------------------------------------------------------
    //
    // #1338 taught this program to COUNT its cameras and #1451 taught it to count the
    // right ones; both say the answer in a sentence, to a console in another room. Turnaus
    // has accepted, stored and published the count since #1343, and no board has ever sent
    // it -- `postBeat` posted `{"condition":"<word>"}` and nothing else, so every board in
    // the field read as unknown on a page built to draw the number. This is that number,
    // in the shape the heartbeat states it.
    //
    // WHY IT LIVES BESIDE THE CONDITION. It is the same claim, made by the same process
    // about the same fifteen seconds, and the server retires the two together -- past the
    // silence window a board's condition is forgotten and its camera count goes with it,
    // because a page saying *nothing is known about this machine* beside *all three
    // cameras are scoring* would be a page saying two things about one board. So the
    // census is written where the condition's facts are written, read where the word is
    // read, and there is no second path for the two to arrive by.
    //
    // ABSENT IS NOT NOUGHT, AND THAT IS THE WHOLE DESIGN OF THIS FILE'S DEFAULT. Nought
    // scoring cameras is a board that cannot score a dart; *nobody can say* is a board
    // that has not calibrated yet, a detector that does not count, or a start that faulted
    // before it had cameras to count. Turnaus distinguishes them and draws the first as a
    // fault and the second as silence, so sending nought for an unknown board would mark
    // every board in the country as broken. The stated bit below is therefore off until
    // something really counts, and `postBeat` omits the whole object while it is off.

    /** The three counts a beat may state. */
    struct Cameras
    {
        int fitted = 0;  // how many cameras this board has: the denominator
        int scoring = 0; // how many of them a dart is really scored from
        int dark = 0;    // how many of them delivered no frame at all
    };

    /**
     * More cameras than any board anybody has ever fitted.
     *
     * `App\Autoscoring\CameraReport::MOST_A_BOARD_HAS`, mirrored -- a beat naming more is
     * refused with a 422, and a refused beat costs the club the CONDITION as well as the
     * count. So the bound is asked on this side and an answer outside it is not stated at
     * all, which is the one failure mode this whole message exists to avoid.
     */
    inline constexpr int kMostABoardHas = 16;

    /**
     * Whether these three numbers can describe one board, and whether this door will take
     * them.
     *
     * The server's `isArithmeticallyPossible()` and its validation bounds, in one
     * question and asked HERE, before anything is sent. A camera is scoring, or dark, or
     * neither -- never two of those -- so the first two cannot together exceed what is
     * fitted. Turnaus drops such a triple rather than refusing the beat, which means a
     * board sending one would beat happily for ever while its page read unknown and
     * nothing anywhere failed. That is the state #1474 was filed about, one level down, so
     * it is refused here instead: a census this board cannot justify is not stated.
     */
    inline bool couldBeOneBoard(const Cameras &c)
    {
        return c.fitted >= 1 && c.fitted <= kMostABoardHas &&
               c.scoring >= 0 && c.dark >= 0 &&
               c.scoring + c.dark <= c.fitted;
    }

    /**
     * The census, packed into one word.
     *
     * ONE atomic rather than three plus a flag, and the reason is the reader. This is
     * written on the scoring thread and read on the beat thread, and the two never
     * synchronise on anything else; three separate stores can be read as a new `fitted`
     * beside an old `scoring`, which is a triple no board was ever in and which Turnaus
     * would silently drop -- a beat whose census vanished for a reason nothing logged.
     * Sixteen bits each is more than `kMostABoardHas` will ever need, the seventeenth word
     * bit says the census was really taken, and zero is *nothing is known*: the value the
     * program starts life holding, so absence is the default rather than a state something
     * has to remember to write.
     */
    inline std::atomic<uint64_t> &statedCameras()
    {
        static std::atomic<uint64_t> v{0};
        return v;
    }

    /** Bit 48: something counted. Below it, three sixteen-bit counts. */
    inline constexpr uint64_t kCamerasStated = (uint64_t)1 << 48;

    /**
     * Say how many cameras this board has and how many of them a dart is scored from.
     *
     * Written where the numbers are decided and nowhere else -- `GeometryDetector`'s
     * calibration census -- for the reason every other fact in this file is: a second copy
     * counted somewhere else is how the board's page and the board's console come to
     * disagree about the same evening.
     *
     * A triple no board can be in clears the census rather than storing it. Saying nothing
     * is always available and always safe; saying something impossible is not.
     */
    inline void countCameras(int fitted, int scoring, int dark)
    {
        const Cameras c{fitted, scoring, dark};

        if (!couldBeOneBoard(c))
        {
            statedCameras().store(0, std::memory_order_relaxed);
            return;
        }

        statedCameras().store(kCamerasStated |
                                  (uint64_t)(uint16_t)c.fitted |
                                  ((uint64_t)(uint16_t)c.scoring << 16) |
                                  ((uint64_t)(uint16_t)c.dark << 32),
                              std::memory_order_relaxed);
    }

    /** Nothing is known about this board's cameras. The state it starts in. */
    inline void forgetCameras()
    {
        statedCameras().store(0, std::memory_order_relaxed);
    }

    /**
     * The census this board would state right now, or false where it has none.
     *
     * False is the honest answer for a board that has not calibrated, one whose detector
     * does not count, and one that faulted before it had cameras to count -- and the beat
     * omits the object entirely rather than sending three noughts.
     */
    inline bool camerasCounted(Cameras &out)
    {
        const uint64_t packed = statedCameras().load(std::memory_order_relaxed);

        if ((packed & kCamerasStated) == 0)
        {
            return false;
        }

        out.fitted = (int)(uint16_t)(packed & 0xffff);
        out.scoring = (int)(uint16_t)((packed >> 16) & 0xffff);
        out.dark = (int)(uint16_t)((packed >> 32) & 0xffff);
        return true;
    }

    /**
     * OD_BEAT_CAMERAS=0: beat the pre-#1474 body, `{"condition":"<word>"}` and nothing
     * else, on this same binary.
     *
     * `od_fix`'s convention and `OD_CAMERA_QUORUM`'s spelling, for their reason: one
     * binary, the rule chosen at run time, so that "a different build" is never a confound
     * when a tester shows the server's answer moving with the message. A harness that
     * cannot make this board beat the old shape cannot show that the new shape is what
     * reached Turnaus -- and the old shape is also the fleet mid-upgrade, which the server
     * contract has to go on accepting.
     *
     * Read once for the life of the process: a board that changed its mind about what it
     * states halfway through a run would be a worse thing to debug than either shape.
     */
    inline bool beatSaysNothingAboutCameras()
    {
        static const bool chosen = []
        {
            const char *e = std::getenv("OD_BEAT_CAMERAS");
            return e != nullptr && std::string(e) == "0";
        }();
        return chosen;
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
