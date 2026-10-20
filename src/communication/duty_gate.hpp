#pragma once
// turnaus#1912: the scheduling rule between what a round owes and what an END owes.
//
// #1787 put an END's log upload and its frame answers on the push thread, once the queue
// was empty. That is correct about ORDER -- nothing of an END's duties goes before the
// darts and the takeout it follows -- and wrong about everything after: once a 1.6 MB
// frames upload has started, a dart thrown during it waits behind the whole of it, because
// the one thread that could post the dart is the one inside the upload. On the Pi's Wi-Fi
// on 2026-10-20 an upload took 8..12 s and the dart answers came +12, +10 and +45 s late.
//
// THE RULE, in two halves:
//   1. The duties run on a thread of their own, so the push thread never waits for one: a
//      dart queued during an upload is posted while the upload is still in flight.
//   2. Between UNITS of a duty -- one log chunk, one frames GET, one frame answer -- the
//      duty thread waits until nothing is owed. A dart queued mid-upload is therefore
//      settled before the upload resumes, so the uplink is not shared with the next
//      megabyte while a dart is on it, and a long END is abandoned piecewise rather than
//      finished in front of a round.
//
// The gate holds a count and nothing else. The push side states the queue's size after
// every change (`owed(n)`), the duty side asks `awaitClear()` before every unit. It is
// std-only and owns no thread, so testers/i1912_schedule_check.cpp drives the very class
// the client uses, with its two mutations (the #1787 shape, and a gate that never waits).

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <mutex>

class DutyGate
{
public:
    /** The push side: the queue now holds `n` things owed. Any thread. */
    void owed(size_t n)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            owed_ = n;
        }
        condition_.notify_all();
    }

    /**
     * The duty side, before each unit: block until nothing is owed. Returns false when the
     * gate was closed (the program is stopping or quiescing) and the unit must not start;
     * the duty is then owed still and is resumed at the next END.
     */
    bool awaitClear()
    {
        std::unique_lock<std::mutex> lock(mutex_);
        condition_.wait(lock, [this] { return owed_ == 0 || closed_; });
        return !closed_;
    }

    /** Like awaitClear, but gives up after `most`; true only when clear and open. */
    bool awaitClearFor(std::chrono::milliseconds most)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        condition_.wait_for(lock, most, [this] { return owed_ == 0 || closed_; });
        return owed_ == 0 && !closed_;
    }

    /** Wake every waiter and refuse every unit until open(). */
    void close()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            closed_ = true;
        }
        condition_.notify_all();
    }

    void open()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        closed_ = false;
    }

    size_t owedNow() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return owed_;
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    size_t owed_ = 0;
    bool closed_ = false;
};
