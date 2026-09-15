#pragma once
// #822: the outbound half. The detector has served a WebSocket since it was written and
// has never once dialled out; this is the client that pairs the board and posts the darts.
//
// Three rules shape every line of it, and they are the acceptance criteria rather than
// decoration:
//
//  1. NOTHING HERE RUNS ON THE SCORING THREAD. Scorer::sendResult calls offer(), which
//     takes a mutex, pushes onto a deque and returns. The lock is never held across an
//     HTTP call -- the worker copies an item out and releases it before it posts -- so
//     the longest offer() can wait is another push, not a network timeout.
//  2. A DART THAT COULD NOT BE POSTED IS STILL OWED. Every detection is appended to a
//     spool file before it is posted, and a cursor beside it says how far delivery got.
//     A restart resumes from the cursor, so the network being gone for a night costs a
//     retry each, not a dart. ADR-0003: scoring is append-only, so the spool is too.
//  3. A RETRY MUST NOT PRODUCE TWO DARTS. Each detection carries #821's idempotency key,
//     generated once when the detection is spooled and reused by every retry of it
//     forever, including retries after a restart. That is what makes crashing between
//     "posted" and "cursor advanced" cost nothing.
//
// The credential is a secret. It is held in one std::string, put into one Authorization
// header, and passed to no logging call anywhere in this file -- not the value, not a
// prefix, not its length.

#include "score_queue.hpp"
#include "../utils/board_sight.hpp"
#include "http_transport.hpp"
#include "../detector/detector_interface.hpp"
#include <string>
#include <deque>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <cstdint>

/** What a board was told at startup about who to talk to and how. */
struct TurnausConfig
{
    std::string base_url;         // "https://turnaus.example/api"
    std::string credentials_path; // resolved, never empty when pairing is possible
    bool allow_plaintext = false; // http:// is refused without this
    size_t queue_capacity = 512;  // detections held in memory before the policy bites
    int connect_timeout_s = 3;
    int read_timeout_s = 5;
    int backoff_initial_ms = 1000;
    int backoff_max_ms = 30000;
};

/** One thing owed to the server: a detection or a takeout, already serialised. */
struct OwedPush
{
    std::string idempotency_key;
    std::string path;     // "/api/v1/autoscorer/detections"
    std::string body;     // the JSON, exactly as it will be posted, forever
    bool spooled = false; // written to the spool file once, by the worker
};

class TurnausClient
{
public:
    TurnausClient(const TurnausConfig &config);
    ~TurnausClient();

    /**
     * Exchange a pairing code for a credential and write it to the credential file.
     * Runs on the calling thread, before any scoring starts, and is the only blocking
     * call in this class. Returns false and says why -- without quoting anything the
     * server sent back that might contain the token -- on any failure.
     */
    bool pair(const std::string &code);

    /** True when a credential was loaded and the address is usable. */
    bool isPaired() const { return paired_; }

    /** Start the worker. Safe to call when unpaired: it starts nothing and says so. */
    void start();

    /**
     * Stop the worker and join it. Called by ~TurnausClient, which #825's exit path
     * reaches because ~Scorer runs when main is unwound. Bounded: the worker waits on
     * a condition variable that stop() notifies, so a shutdown during a backoff sleep
     * returns immediately rather than after 30 seconds.
     */
    void stop();

    /**
     * Hand a detection over. Never blocks on the network, never does I/O, never throws.
     * Returns false when the queue was full and the policy below was applied.
     */
    bool offer(const DetectorResult &result);

    /**
     * #892: the beat, and the reason it is a thread of its own rather than an item the
     * push worker posts.
     *
     * A beat is not a Detection, and the three places that difference has to hold are
     * the round in hand, the `reference` dedup and the fifteen-minute abandonment
     * horizon -- all three of which are scoped to the round being thrown right now. A
     * beat carries no `reference`, so it must not enter the dedup; it must never be
     * appended to `owed.jsonl`, because a beat that is fifteen minutes old is not a
     * retry, it is a dead board saying it is alive, which is worse than the dart the
     * horizon exists to refuse. A FAILED BEAT IS DROPPED, NOT SPOOLED: the next one is
     * due in an interval and carries the same claim, fresher.
     *
     * The worker cannot carry it even if the queue were bypassed. That thread spends up
     * to thirty seconds asleep in a backoff after an unreachable server, and up to
     * `read_timeout_s` inside a POST; a beat that queued behind either would arrive late
     * for a reason that has nothing to do with whether this board can see. So the two
     * share nothing but `running_`, and the beat's cadence is its own.
     */
    void beat();

    // Counters, for the run's own summary line. None of them is a secret.
    uint64_t queued() const { return queued_; }
    uint64_t delivered() const { return delivered_; }
    uint64_t dropped() const { return dropped_; }
    uint64_t attempts() const { return attempts_; }
    uint64_t beats() const { return beats_; }
    uint64_t beatsLost() const { return beats_lost_; }
    size_t backlog() const;

private:
    void run();
    /**
     * One beat. Posts the word and reads the two numbers back. Returns false for every
     * outcome that is not a `200`, which is the only answer that means the server has
     * this board's condition written down.
     */
    bool postBeat(const char *condition_word, int &interval_s, int &silence_s);
    bool loadCredential();
    bool deliver(const OwedPush &item);
    void spool(const OwedPush &item);
    void loadSpool();
    void settleOneRecord();
    void compactSpoolIfSettled();
    std::string newIdempotencyKey();

    TurnausConfig config_;
    odhttp::Url url_;
    std::string credential_; // the secret. Never logged.
    std::string device_id_;  // not a secret; it is what the server calls this board.
    std::atomic<bool> paired_{false};
    std::atomic<bool> running_{false};

    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::deque<OwedPush> queue_;
    std::thread worker_;

    // #892. Its own mutex and its own condition variable, deliberately: a beat waiting
    // on `mutex_` would be waiting behind whatever the push worker is doing with the
    // queue, and the whole point of the beat is that its lateness means something.
    std::thread beater_;
    mutable std::mutex beat_mutex_;
    std::condition_variable beat_condition_;
    // Both are the server's own answer and neither has a compiled-in value. Zero means
    // "the server has not said yet", which is a state this board really is in for the
    // length of its first beat and is not a number standing in for one.
    std::atomic<int> interval_s_{0};
    std::atomic<int> silence_s_{0};

    std::string spool_path_;
    std::string cursor_path_;
    // How many leading records of the spool are settled -- delivered, refused as
    // unreadable, or abandoned as stale. A record index rather than a byte offset,
    // because records are appended in order and never rewritten, so an index is stable
    // under a torn write in a way an offset is not.
    uint64_t settled_records_ = 0;
    uint64_t spool_records_ = 0;

    std::atomic<uint64_t> queued_{0};
    std::atomic<uint64_t> delivered_{0};
    std::atomic<uint64_t> dropped_{0};
    std::atomic<uint64_t> attempts_{0};
    std::atomic<uint64_t> sequence_{0};
    std::atomic<uint64_t> beats_{0};
    std::atomic<uint64_t> beats_lost_{0};
};
