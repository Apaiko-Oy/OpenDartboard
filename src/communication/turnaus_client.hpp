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
    // #1259: what a pairing request calls this board -- --label, or the computer's name
    // (announce::defaultLabel), the same label the board already announces. Empty only for
    // a caller that never set it, which is sent as the old literal.
    std::string label;
    size_t queue_capacity = 512;  // detections held in memory before the policy bites
    int connect_timeout_s = 3;
    int read_timeout_s = 5;
    int backoff_initial_ms = 1000;
    int backoff_max_ms = 30000;
};

/**
 * #891: which of the two things this board may be bound to a push is owed to.
 *
 * A board is paired to an Organisation for Matches (#822) and may ALSO be paired to one
 * Casual Contest for an evening (#887, ADR-0069). Both bindings are kept, because a club
 * board is Station 3 on Tuesday and somebody's knockabout on Wednesday, and walking to it
 * with a new six-digit code each way is the thing pairing-once exists to avoid. Only one
 * of them is ever the destination: while a Contest binding is held it wins, because it is
 * the one with a lifetime -- it ends when the evening is given up -- and the Organisation
 * binding is the months-long one still there afterwards.
 *
 * It is on the OwedPush and not read from the client, deliberately. A spooled dart is
 * owed to the binding it was thrown under, not to whatever this board is bound to when it
 * is finally posted, and those are different things across exactly the restart the spool
 * exists for.
 */
enum class Binding
{
    Organisation,
    Contest,
};

/** One thing owed to the server: a detection or a takeout, already serialised. */
struct OwedPush
{
    std::string idempotency_key;
    std::string path;     // "/api/v1/autoscorer/detections"
    std::string body;     // the JSON, exactly as it will be posted, forever
    bool spooled = false; // written to the spool file once, by the worker
    // #891. Which credential it is posted with, and -- for a Contest -- which evening it
    // belongs to, so a dart owed to Tuesday's knockabout can never be delivered into
    // Wednesday's.
    Binding binding = Binding::Organisation;
    long long contest_id = 0;
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

    /**
     * #891: the same exchange at the third door -- `POST /api/v1/casual/boards` -- and
     * what comes back is bound to one Casual Contest and to nothing else. No Station, no
     * Organisation: which is what makes a board in somebody's garage work exactly as a
     * pub's does (ADR-0069).
     *
     * A separate flag rather than a fallback loop over both doors, for
     * `AutoscorerDetectionController`'s own reason turned around: what a request does
     * should be decided by where it is sent and not by a field somebody can get wrong.
     * The two codes are six digits apiece, live in different tables and neither door
     * reads the other's, so nothing in the digits says which evening they are for -- the
     * person typing them knows, and this is where they say so. Trying both would also
     * spend two attempts of one shared `api-pairing` allowance per code.
     *
     * Any Organisation binding already in the file is kept, not replaced. An existing
     * Contest binding IS replaced: a board is on one evening at a time.
     */
    bool pairContest(const std::string &code);

    /** True when a credential was loaded and the address is usable. */
    bool isPaired() const { return paired_; }

    // ---- #1259: one code, either door, asked for at the console. ----

    /** Which door a six-digit code is presented at. */
    enum class Door
    {
        Organisation, // POST /api/v1/autoscorer/devices (#820)
        Contest,      // POST /api/v1/casual/boards (#887)
    };

    /** Why this build may not send a code to this address at all, before anything is sent. */
    enum class PairingBlock
    {
        None,
        BadAddress, // the address does not parse
        NoTls,      // https:// on a build with no TLS transport
        Plaintext,  // http:// without --allow-plaintext or OD_ALLOW_PLAINTEXT=1
        NoConfigDir // nowhere to keep a credential
    };
    PairingBlock pairingBlock() const;

    /** What one presentation of a code came to. Nothing in it came from the response body. */
    struct Redemption
    {
        enum class Kind
        {
            Paired,      // 201, and the credential is written
            Refused,     // 422: the door's one refusal (never minted, spent, expired, ended)
            Unreachable, // no server answered
            RateLimited, // 429
            Unexpected,  // any other status
            Blocked,     // pairingBlock() said no; nothing was sent
            NotKept,     // 201, but the credential could not be read or written; logged
        };
        Kind kind = Kind::Blocked;
        int status = 0;
        int retry_after_s = -1;   // what a 429 named, or -1
        std::string detail;       // a transport error; never a token, never the code
    };

    /**
     * Present `code` at one door and, on a 201, write the credential exactly where and
     * exactly how pair() and pairContest() always have -- those two are now this plus the
     * log lines they always printed. Logs the credential's keeping, never the code.
     */
    Redemption redeem(Door door, const std::string &code);

    /** The Casual Contest a Contest pairing bound this board to, or 0. Not a secret. */
    long long contestId() const { return contest_id_; }
    /** The Organisation device this board is, or "0". Not a secret. */
    const std::string &deviceId() const { return device_id_; }

    /**
     * True once per refusal: a credential this RUNNING board held was refused (#822's 401
     * on a push or a beat) or its Contest binding ended with nothing underneath (#891),
     * so it now holds nothing it can push with. The console watcher asks; nothing else
     * reads it, so a board with no console behaves exactly as before.
     */
    bool takeUnpairing() { return unpaired_while_running_.exchange(false); }

    /**
     * Stop and join the two threads without the shutdown summary, and write down whatever
     * they never reached, so a new credential is never written while a thread may still
     * be reading the old one. resume() starts them again; stop() still ends everything.
     */
    void quiesce();

    /** After quiesce() and a successful redeem(): the two threads again, the queue as it was. */
    bool resume();

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
    /** #891. The live destination: the Contest binding while there is one, else the club's. */
    Binding destination() const { return contest_bound_.load() ? Binding::Contest : Binding::Organisation; }
    const std::string &credentialFor(Binding binding) const;
    /**
     * #891. The three addresses, per binding. A Contest board holds
     * `autoscorer:detections.push` and nothing else, exactly as a club's board does
     * (ADR-0065) -- what differs is the door, because `RequireAutoscorerDevice` will not
     * have a `CasualContestBoard` at any price and a board on somebody's evening must not
     * be able to reach a league night by holding the wrong end of its own credential.
     */
    static const char *detectionsPath(Binding binding);
    static const char *takeoutsPath(Binding binding);
    static const char *heartbeatsPath(Binding binding);
    /**
     * #891. The evening ended: forget the Contest binding, drop what was owed to it, and
     * fall back to the Organisation binding if this board has one.
     *
     * What is owed is DROPPED rather than kept, which is the opposite of what a refused
     * Organisation credential does (#822 leaves that spool alone, because those darts are
     * still owed to whoever re-pairs the board). Nobody re-pairs to a Contest that has
     * been Given Up: #887 deletes the token and refuses the code in the same words as one
     * that never existed. A record naming it can therefore never be delivered, and the
     * only thing keeping it could do is land in somebody else's evening.
     */
    void releaseContestBinding(const char *why);
    /** Rewrite the credential file without its `contest` object, keeping everything else. */
    bool forgetContestInFile();
    /**
     * One beat. Posts the word and reads the two numbers back. Returns false for every
     * outcome that is not a `200`, which is the only answer that means the server has
     * this board's condition written down.
     */
    bool postBeat(const char *condition_word, int &interval_s, int &silence_s);
    bool loadCredential();
    /** #1259: the label a pairing request sends, never longer than the doors accept. */
    std::string pairingLabel() const;
    /** #1259: notify and join the worker and the beat. The caller has already dropped running_. */
    void haltThreads();
    /** #1259: write every queued push the worker never reached to the spool. */
    size_t spoolUnwritten();
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
    // #891: the second credential, and it is as secret as the first. The two are held
    // apart rather than in one slot because a board really does hold both, and because
    // the one that is refused is the one that has to be forgotten.
    std::string contest_credential_;
    long long contest_id_ = 0;   // not a secret: it is the evening's own identifier.
    std::string contest_board_id_;
    std::atomic<bool> contest_bound_{false};
    // Set when a Contest binding is dropped mid-run, so the push worker can pick up the
    // next item at once instead of serving a backoff it no longer has a reason for.
    std::atomic<bool> binding_changed_{false};
    // A deployment whose Casual door has no heartbeat address answers 404. Silence is
    // always available and always safe (#822 §11), so the board stops beating and says so
    // once, rather than beating at a 404 every interval for the length of an evening.
    std::atomic<bool> beat_unsupported_{false};
    std::atomic<bool> paired_{false};
    std::atomic<bool> running_{false};
    // #1259. Set where a running board loses the last thing it could push with.
    std::atomic<bool> unpaired_while_running_{false};
    // #1259. Threads stopped by quiesce() and not yet resumed; start() leaves them to resume().
    std::atomic<bool> quiesced_{false};
    // #1259. start, stop, quiesce and resume from two threads (Scorer::run and the console
    // watcher) must not both assign worker_.
    std::mutex lifecycle_mutex_;

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
    // #1259. The beat thread's own two locals, kept when it leaves so a thread started again by
    // resume() asks "has a frame arrived since my LAST beat" rather than "since this instant".
    // Asked of a snapshot taken a microsecond earlier, the answer is always no, and the first
    // beat after a new pairing said ERROR about a board that was seeing (measured on Windows
    // and in the pty check). Only the beat thread reads or writes them, and resume() starts it
    // only after quiesce() has joined the last one.
    uint64_t kept_frames_at_last_beat_ = 0;
    bool kept_asked_since_calibration_ = false;
    bool beat_state_kept_ = false;

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
