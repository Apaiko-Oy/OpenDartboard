// turnaus#1912: the scheduling rule between a round's pushes and an END's duties, held on
// the very class the client uses (src/communication/duty_gate.hpp).
//
// THE RULE: a dart queued while an END's upload is in flight is POSTED while that upload is
// still in flight (the push thread never waits for a duty), and is SETTLED before the
// upload's next unit starts (the duty thread waits at the gate between units).
//
// No clock decides anything here. Every fake post and every fake upload unit blocks on a
// latch the scenario releases, so the transcript is an ORDER of events and a verdict is a
// question about that order. The only waits are bounded "did it happen yet" polls, and a
// loaded box can make one of those slower, never make a wrong schedule look right: a poll
// that times out in the right schedule is a FAIL to look at, not a PASS.
//
// The scenario, in every schedule:
//   1. an END is owed: the duty starts upload unit 1, which hangs until released;
//   2. a dart D is queued (gate.owed(1));
//   3. wait for D's post to start; D's post hangs until released;
//   4. release unit 1; look whether unit 2 starts while D is still in flight;
//   5. release D; D settles (gate.owed(0)); unit 2 runs; units 2 and 3 are released.
//
// THE MUTATIONS, with the prediction printed before each runs:
//   M1 "#1787": the duties run on the push thread when its queue is empty (the shape
//      before #1912). Predicted: D's post does not start until unit 1 has ended.
//   M2 "no gate": the duties run on their own thread but never ask the gate. Predicted:
//      unit 2 starts while D is still in flight.
// The real schedule must pass every assertion and each mutant must fail its predicted one,
// in the same process, every run (#1463).
//
//   g++ -std=c++17 -O1 -Wall -Wextra -I src testers/i1912_schedule_check.cpp -lpthread

#include "communication/duty_gate.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <iostream>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace
{
    /** A one-shot latch: wait() blocks until open(). */
    class Latch
    {
    public:
        void open()
        {
            {
                std::lock_guard<std::mutex> l(m_);
                open_ = true;
            }
            cv_.notify_all();
        }
        void wait()
        {
            std::unique_lock<std::mutex> l(m_);
            cv_.wait(l, [this] { return open_; });
        }

    private:
        std::mutex m_;
        std::condition_variable cv_;
        bool open_ = false;
    };

    /** The ordered record of what happened; seq() is the position of an event, or -1. */
    class Transcript
    {
    public:
        void say(const std::string &event)
        {
            {
                std::lock_guard<std::mutex> l(m_);
                order_[event] = next_++;
                lines_.push_back(event);
            }
            cv_.notify_all();
        }
        long seq(const std::string &event) const
        {
            std::lock_guard<std::mutex> l(m_);
            auto it = order_.find(event);
            return it == order_.end() ? -1 : it->second;
        }
        /** Bounded poll; true when the event happened within `ms`. */
        bool waitFor(const std::string &event, int ms)
        {
            std::unique_lock<std::mutex> l(m_);
            return cv_.wait_for(l, std::chrono::milliseconds(ms), [&] { return order_.count(event) != 0; });
        }
        std::string joined() const
        {
            std::lock_guard<std::mutex> l(m_);
            std::string out;
            for (const std::string &s : lines_)
            {
                out += (out.empty() ? "" : " < ") + s;
            }
            return out;
        }

    private:
        mutable std::mutex m_;
        std::condition_variable cv_;
        std::map<std::string, long> order_;
        std::vector<std::string> lines_;
        long next_ = 0;
    };

    enum class Schedule
    {
        Real,         // #1912: own duty thread, gate before every unit
        InlineOnPush, // M1: #1787's shape
        NoGate,       // M2: own thread, gate never asked
    };

    const char *name(Schedule s)
    {
        switch (s)
        {
        case Schedule::Real:
            return "real (#1912)";
        case Schedule::InlineOnPush:
            return "M1 #1787 inline";
        case Schedule::NoGate:
            return "M2 no gate";
        }
        return "?";
    }

    struct World
    {
        Schedule schedule;
        DutyGate gate;
        Transcript t;
        std::mutex m;
        std::condition_variable cv;
        std::deque<std::string> queue;
        bool end_owed = false;
        bool running = true;
        Latch release_unit[4];
        Latch release_dart;

        void upload(int unit)
        {
            t.say("unit" + std::to_string(unit) + "_start");
            release_unit[unit].wait();
            t.say("unit" + std::to_string(unit) + "_end");
        }

        /** The END's duty: three units, each behind the gate unless the schedule is M2. */
        void duty()
        {
            for (int unit = 1; unit <= 3; unit++)
            {
                if (schedule != Schedule::NoGate && schedule != Schedule::InlineOnPush && !gate.awaitClear())
                {
                    return;
                }
                upload(unit);
            }
            t.say("duty_done");
        }

        void pushThread()
        {
            for (;;)
            {
                std::string dart;
                bool run_duty = false;
                {
                    std::unique_lock<std::mutex> l(m);
                    cv.wait(l, [&] { return !running || !queue.empty() || (schedule == Schedule::InlineOnPush && end_owed); });
                    if (!running && queue.empty())
                    {
                        return;
                    }
                    if (queue.empty())
                    {
                        end_owed = false;
                        run_duty = true; // M1: the push thread does the END's duty itself
                    }
                    else
                    {
                        dart = queue.front();
                    }
                }
                if (run_duty)
                {
                    duty();
                    continue;
                }
                t.say(dart + "_post_start");
                release_dart.wait();
                // Said BEFORE the gate hears of it, as the client's 202 is taken before the
                // queue is popped: saying it after let the duty thread's unit2_start land in
                // between in the transcript, a race in this model and not in the rule.
                t.say(dart + "_settled");
                {
                    std::lock_guard<std::mutex> l(m);
                    queue.pop_front();
                    gate.owed(queue.size());
                }
            }
        }

        void dutyThread()
        {
            {
                std::unique_lock<std::mutex> l(m);
                cv.wait(l, [&] { return !running || (end_owed && queue.empty()); });
                if (!running)
                {
                    return;
                }
                end_owed = false;
            }
            duty();
        }

        void queueDart(const std::string &dart)
        {
            {
                std::lock_guard<std::mutex> l(m);
                queue.push_back(dart);
                gate.owed(queue.size());
            }
            t.say(dart + "_queued");
            cv.notify_all();
        }
    };

    struct Verdict
    {
        std::vector<std::string> failed; // names of the assertions that failed
        std::string transcript;
    };

    Verdict runScenario(Schedule schedule)
    {
        World w;
        w.schedule = schedule;
        std::thread pusher(&World::pushThread, &w);
        std::thread duties;
        if (schedule != Schedule::InlineOnPush)
        {
            duties = std::thread(&World::dutyThread, &w);
        }
        Verdict v;
        auto expect = [&](bool ok, const std::string &what)
        {
            if (!ok)
            {
                v.failed.push_back(what);
            }
        };

        {
            std::lock_guard<std::mutex> l(w.m);
            w.end_owed = true;
        }
        w.cv.notify_all();
        expect(w.t.waitFor("unit1_start", 3000), "the END's upload starts once nothing is owed");

        w.queueDart("D");
        const bool posted_mid_upload = w.t.waitFor("D_post_start", 2000) && w.t.seq("unit1_end") < 0;
        expect(posted_mid_upload, "posted-mid-upload: D's post starts while upload unit 1 is still in flight");

        w.release_unit[1].open();
        w.t.waitFor("unit1_end", 3000);
        // A wrong schedule shows itself here within milliseconds; 300 ms is slack for a box
        // under load, and can only make the real schedule's PASS slower, never a mutant's.
        const bool resumed_over_dart = w.t.waitFor("unit2_start", 300) && w.t.seq("D_settled") < 0;
        expect(!resumed_over_dart, "settled-before-resume: upload unit 2 does not start while D is in flight");

        w.release_dart.open();
        expect(w.t.waitFor("D_post_start", 3000), "D is posted at all");
        expect(w.t.waitFor("D_settled", 3000), "D settles");
        w.release_unit[2].open();
        w.release_unit[3].open();
        expect(w.t.waitFor("duty_done", 3000), "the END's log and frames still all arrive (units 1..3 done)");

        const long post = w.t.seq("D_post_start"), u1end = w.t.seq("unit1_end"), settled = w.t.seq("D_settled"),
                   u2 = w.t.seq("unit2_start");
        expect(post >= 0 && u1end >= 0 && post < u1end, "order: D_post_start < unit1_end");
        expect(settled >= 0 && u2 >= 0 && settled < u2, "order: D_settled < unit2_start");

        {
            std::lock_guard<std::mutex> l(w.m);
            w.running = false;
        }
        w.gate.close();
        w.cv.notify_all();
        pusher.join();
        if (duties.joinable())
        {
            duties.join();
        }
        v.transcript = w.t.joined();
        return v;
    }

    bool failedOn(const Verdict &v, const std::string &prefix)
    {
        for (const std::string &f : v.failed)
        {
            if (f.rfind(prefix, 0) == 0)
            {
                return true;
            }
        }
        return false;
    }

    void show(Schedule s, const Verdict &v)
    {
        std::cout << "  [" << name(s) << "] transcript: " << v.transcript << std::endl;
        for (const std::string &f : v.failed)
        {
            std::cout << "  [" << name(s) << "] assertion failed: " << f << std::endl;
        }
        if (v.failed.empty())
        {
            std::cout << "  [" << name(s) << "] every assertion held" << std::endl;
        }
    }
}

int main()
{
    int rc = 0;

    std::cout << "== the real schedule (src/communication/duty_gate.hpp as the client uses it)" << std::endl;
    std::cout << "PREDICTION: every assertion holds; D is posted inside unit 1 and settled before unit 2" << std::endl;
    const Verdict real = runScenario(Schedule::Real);
    show(Schedule::Real, real);
    if (!real.failed.empty())
    {
        std::cout << "FAIL the real schedule broke the rule" << std::endl;
        rc = 1;
    }
    else
    {
        std::cout << "PASS the real schedule keeps the rule" << std::endl;
    }

    std::cout << "== M1: the END's duties inline on the push thread (#1787's shape)" << std::endl;
    std::cout << "PREDICTION: posted-mid-upload fails -- D waits until unit 1 has ended" << std::endl;
    const Verdict m1 = runScenario(Schedule::InlineOnPush);
    show(Schedule::InlineOnPush, m1);
    if (failedOn(m1, "posted-mid-upload"))
    {
        std::cout << "PASS M1 is caught by posted-mid-upload" << std::endl;
    }
    else
    {
        std::cout << "FAIL M1 was not caught: the check cannot see a dart waiting behind an upload" << std::endl;
        rc = 1;
    }

    std::cout << "== M2: the END's duties on their own thread, gate never asked" << std::endl;
    std::cout << "PREDICTION: settled-before-resume fails -- unit 2 starts with D in flight" << std::endl;
    const Verdict m2 = runScenario(Schedule::NoGate);
    show(Schedule::NoGate, m2);
    if (failedOn(m2, "settled-before-resume"))
    {
        std::cout << "PASS M2 is caught by settled-before-resume" << std::endl;
    }
    else
    {
        std::cout << "FAIL M2 was not caught: the check cannot see an upload resuming over a dart" << std::endl;
        rc = 1;
    }

    // The gate's own refusal: a closed gate refuses the next unit at once.
    DutyGate g;
    g.owed(1);
    std::thread closer([&] { g.close(); });
    const bool refused = !g.awaitClear();
    closer.join();
    std::cout << (refused ? "PASS" : "FAIL") << " a closed gate refuses the next unit (stop/quiesce abandons the duty)"
              << std::endl;
    if (!refused)
    {
        rc = 1;
    }

    std::cout << "SCHEDULE_CHECK_RC=" << rc << std::endl;
    return rc;
}
