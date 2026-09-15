#include "turnaus_client.hpp"
#include "../utils/logging.hpp"
#include "../utils/od_paths.hpp"
#include <nlohmann/json.hpp>
#include <chrono>
#include <random>
#include <fstream>
#include <sstream>

using json = nlohmann::json;

namespace
{
    /**
     * How long a spooled dart is still worth posting. Fifteen minutes is longer than any
     * round and shorter than any interval over which a Match's turn is still the same
     * turn; §"What this does not establish" in the document says what has not been
     * measured about it.
     */
    const uint64_t kStalenessHorizonMs = 15ULL * 60ULL * 1000ULL;

    /**
     * A ULID, because #821 validates `reference` with Laravel's `ulid` rule: 26
     * characters of Crockford base32, the first of which must be 0-7, over a 48-bit
     * millisecond timestamp and 80 random bits. This is the same shape a Visit's
     * client-generated identifier has (ADR-0003), and it is minted here rather than
     * asked for, so that every retry of one dart carries the same one.
     */
    std::string makeUlid()
    {
        static const char *crockford = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
        static std::mt19937_64 rng([]
                                   {
            std::random_device rd;
            return ((uint64_t)rd() << 32) ^ (uint64_t)rd() ^
                   (uint64_t)std::chrono::system_clock::now().time_since_epoch().count(); }());

        uint64_t ms = (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::system_clock::now().time_since_epoch())
                          .count();

        std::string out(26, '0');
        // 10 characters of timestamp, most significant first: 50 bits of room for 48.
        for (int i = 9; i >= 0; i--)
        {
            out[i] = crockford[ms & 31];
            ms >>= 5;
        }
        // 16 characters of randomness: 80 bits.
        uint64_t a = rng(), b = rng();
        for (int i = 10; i < 18; i++)
        {
            out[i] = crockford[a & 31];
            a >>= 5;
        }
        for (int i = 18; i < 26; i++)
        {
            out[i] = crockford[b & 31];
            b >>= 5;
        }
        return out;
    }

    /** #821's sector tokens. Anything the detector publishes that is not one of these
     *  is refused here rather than posted, because the server answers 422 for it and a
     *  422 is not something a retry can improve. */
    bool isPostableSector(const std::string &s)
    {
        if (s == "25" || s == "Bull" || s == "None")
        {
            return true;
        }
        if (s.size() < 2 || s.size() > 3)
        {
            return false;
        }
        char ring = s[0];
        if (ring != 'S' && ring != 's' && ring != 'D' && ring != 'T')
        {
            return false;
        }
        int n = atoi(s.c_str() + 1);
        return n >= 1 && n <= 20 && s.substr(1) == std::to_string(n);
    }
}

TurnausClient::TurnausClient(const TurnausConfig &config) : config_(config)
{
    url_ = odhttp::parseUrl(config_.base_url);
    spool_path_ = od_paths::join(od_paths::configDir(), "owed.jsonl");
    cursor_path_ = od_paths::join(od_paths::configDir(), "owed.cursor");
    loadCredential();
}

TurnausClient::~TurnausClient()
{
    // #825: main is unwound, ~Scorer runs, and this runs with it. Nothing about this
    // client is leaked the way the httplib server and the score queue were.
    stop();
}

bool TurnausClient::loadCredential()
{
    std::string raw;
    if (config_.credentials_path.empty() || !od_paths::readFile(config_.credentials_path, raw))
    {
        return false;
    }
    try
    {
        json j = json::parse(raw);
        credential_ = j.value("token", "");
        device_id_ = std::to_string(j.value("device_id", 0));
    }
    catch (const std::exception &)
    {
        log_warning("TURNAUS: the credential file could not be parsed; this board is unpaired");
        return false;
    }
    if (credential_.empty())
    {
        return false;
    }
    if (od_paths::worldReadable(config_.credentials_path))
    {
        // Named rather than repaired: a file somebody widened on purpose is their
        // business, but it is not something this program will stay quiet about.
        log_warning("TURNAUS: the credential file is readable by somebody other than its owner");
    }
    paired_ = true;
    return true;
}

bool TurnausClient::pair(const std::string &code)
{
    if (!url_.valid)
    {
        log_error("TURNAUS: --turnaus is not a URL this build can parse");
        return false;
    }
    if (url_.tls && !odhttp::tlsAvailable())
    {
        log_error("TURNAUS: this build has no TLS transport (" + std::string(odhttp::transportName()) +
                  ") and will not downgrade an https:// address to plaintext");
        return false;
    }
    if (!url_.tls && !config_.allow_plaintext)
    {
        log_error("TURNAUS: refusing to send a pairing code over http://. Pass --allow-plaintext "
                  "if this really is a loopback or a lab.");
        return false;
    }
    if (config_.credentials_path.empty())
    {
        log_error("TURNAUS: no writable configuration directory, so a credential could not be kept");
        return false;
    }

    json body;
    body["pin"] = code;
    body["label"] = "OpenDartboard";

    odhttp::Response res = odhttp::postJson(url_, "/api/v1/autoscorer/devices", body.dump(), {},
                                            config_.connect_timeout_s, config_.read_timeout_s);
    if (!res.reached_a_server())
    {
        log_error("TURNAUS: pairing could not reach " + url_.host + ": " + res.transport_error);
        return false;
    }
    if (res.status != 201)
    {
        // The body is not echoed. A 201 body carries the token, and a habit of echoing
        // the body is how a token reaches a log line on the one status that matters.
        log_error("TURNAUS: pairing refused with HTTP " + std::to_string(res.status) +
                  " (a code is six digits, single use, and expires in ten minutes)");
        return false;
    }

    try
    {
        json j = json::parse(res.body);
        std::string token = j["data"].value("token", "");
        if (token.empty())
        {
            log_error("TURNAUS: pairing answered 201 with no credential in it");
            return false;
        }
        json stored;
        stored["token"] = token;
        stored["organisation_id"] = j["data"].value("organisationId", 0);
        stored["device_id"] = j["data"]["device"].value("id", 0);
        stored["label"] = j["data"]["device"].value("label", "");
        stored["base_url"] = config_.base_url;

        if (!od_paths::ensureDir(od_paths::configDir()))
        {
            log_error("TURNAUS: could not create the configuration directory");
            return false;
        }
        if (!od_paths::writeSecret(config_.credentials_path, stored.dump(2)))
        {
            log_error("TURNAUS: could not write the credential file");
            return false;
        }
        credential_ = token;
        device_id_ = std::to_string(stored.value("device_id", 0));
        paired_ = true;
    }
    catch (const std::exception &e)
    {
        log_error("TURNAUS: pairing response could not be read: " + std::string(e.what()));
        return false;
    }

    // Device id and label are not secrets; the token is, and it is not here.
    log_info("TURNAUS: paired as device " + device_id_ + ", credential kept at " + config_.credentials_path);
    return true;
}

std::string TurnausClient::newIdempotencyKey()
{
    return makeUlid();
}

bool TurnausClient::offer(const DetectorResult &result)
{
    if (!paired_ || !running_)
    {
        return false;
    }

    OwedPush item;
    if (result.score == "END")
    {
        // The published END is the takeout: #821's /takeouts is the seam where the Visit
        // and its Detections are appended (ADR-0055), and nothing is written on the third
        // dart. A takeout carries no reference and needs none -- a repeated takeout finds
        // no round in hand and answers with a null visitId, so it is idempotent by the
        // server's own construction.
        item.path = "/api/v1/autoscorer/takeouts";
        item.body = "{}";
        item.idempotency_key = "";
    }
    else
    {
        // The detector publishes MISS; #821's Sector grammar spells the same thing None.
        // Measured rather than assumed: a 1100-cycle run at the mock footage published
        // two of them, and without this line they were refused here and never counted.
        std::string sector = result.score == "MISS" ? std::string("None") : result.score;
        if (!isPostableSector(sector))
        {
            // A 422 is not something a retry improves, so it does not enter the queue.
            dropped_++;
            log_warning("TURNAUS: not posting an unpostable sector '" + result.score + "'");
            return false;
        }
        json body;
        body["reference"] = newIdempotencyKey();
        body["sector"] = sector;
        body["bounced_out"] = false;
        item.idempotency_key = body["reference"];
        item.path = "/api/v1/autoscorer/detections";
        item.body = body.dump();
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.size() >= config_.queue_capacity)
        {
            // THE POLICY, stated rather than hoped for. The queue is bounded because an
            // unbounded one is a memory leak with a slow fuse, and blocking is forbidden
            // because it would put a network timeout on the scoring thread.
            //
            // The newest is dropped, not the oldest. Order is load-bearing on this
            // surface: #821 assigns throw_index by arrival, so a hole punched in the
            // middle of a round puts every later dart of it in the wrong slot, while a
            // hole at the end is a short round somebody can see and fix. A drop is
            // counted and said out loud at shutdown; it is never silent.
            dropped_++;
            return false;
        }
        queue_.push_back(item);
        queued_++;
    }
    condition_.notify_one();
    return true;
}

void TurnausClient::spool(const OwedPush &item)
{
    if (spool_path_.empty())
    {
        return;
    }
    std::ofstream out(spool_path_.c_str(), std::ios::binary | std::ios::app);
    if (!out)
    {
        // The spool is a durability improvement, not a precondition. A board whose disk
        // is full still scores, still serves the WebSocket and still pushes what is in
        // memory; it just cannot survive a restart with a backlog.
        return;
    }
    json line;
    line["path"] = item.path;
    line["body"] = item.body;
    line["key"] = item.idempotency_key;
    line["spooled_ms"] = (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::system_clock::now().time_since_epoch())
                             .count();
    out << line.dump() << "\n";
    out.flush();
    spool_records_++;
}

void TurnausClient::loadSpool()
{
    // A restart resumes what the last run still owed. The spool is append-only and the
    // cursor is a count of settled leading records, which is the same shape the scoring
    // model has (ADR-0003): nothing is rewritten, and the only thing that moves is how
    // far delivery got.
    std::string cursor_raw;
    if (od_paths::readFile(cursor_path_, cursor_raw))
    {
        settled_records_ = strtoull(cursor_raw.c_str(), nullptr, 10);
    }
    std::ifstream in(spool_path_.c_str(), std::ios::binary);
    if (!in)
    {
        settled_records_ = 0;
        return;
    }

    uint64_t now = (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::system_clock::now().time_since_epoch())
                       .count();
    std::string line;
    uint64_t index = 0;
    size_t resumed = 0, stale = 0;

    while (std::getline(in, line))
    {
        if (line.empty())
        {
            continue;
        }
        uint64_t here = index++;
        spool_records_ = index;
        if (here < settled_records_)
        {
            continue; // already delivered by an earlier run
        }
        OwedPush item;
        uint64_t spooled_ms = 0;
        try
        {
            json j = json::parse(line);
            item.path = j.value("path", "");
            item.body = j.value("body", "");
            item.idempotency_key = j.value("key", "");
            spooled_ms = j.value("spooled_ms", (uint64_t)0);
        }
        catch (const std::exception &)
        {
            // A torn last line is what a power cut leaves behind. It is settled by
            // being unreadable; there is nothing to retry.
            settled_records_ = index;
            continue;
        }

        // THE STALENESS HORIZON, and #821's dedup model imposes it rather than taste.
        // `reference` is absorbed only while the round it belongs to is still the round
        // in hand: the server empties counted_references at every takeout and whenever
        // the Leg or the ordinal moves on. So a dart replayed hours later is not absorbed
        // as a duplicate -- it is COUNTED, into whichever turn somebody is throwing now.
        // Posting it is worse than losing it, so past the horizon it is abandoned, out
        // loud and counted. Records are appended in time order, so the stale ones are
        // always a prefix and settling them here cannot skip a fresh one.
        if (spooled_ms != 0 && now > spooled_ms && (now - spooled_ms) > kStalenessHorizonMs)
        {
            stale++;
            dropped_++;
            settled_records_ = index;
            continue;
        }

        if (item.path.empty())
        {
            settled_records_ = index;
            continue;
        }
        item.spooled = true; // it is already in the file; do not write it twice
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push_back(item);
        resumed++;
    }

    if (resumed || stale)
    {
        log_info("TURNAUS: resumed " + std::to_string(resumed) + " owed push(es) from the spool; " +
                 std::to_string(stale) + " abandoned as older than the round they belonged to");
    }
}

void TurnausClient::settleOneRecord()
{
    settled_records_++;
    std::ofstream out(cursor_path_.c_str(), std::ios::binary | std::ios::trunc);
    if (out)
    {
        out << settled_records_;
        out.flush();
    }
}

void TurnausClient::compactSpoolIfSettled()
{
    // Nothing owed and nothing unsettled: the spool has done its job and may start again
    // at nothing, so a board left running for a month does not accumulate a file.
    if (spool_records_ == 0 || settled_records_ < spool_records_)
    {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!queue_.empty())
        {
            return;
        }
    }
    std::ofstream truncate_spool(spool_path_.c_str(), std::ios::binary | std::ios::trunc);
    std::ofstream truncate_cursor(cursor_path_.c_str(), std::ios::binary | std::ios::trunc);
    if (truncate_cursor)
    {
        truncate_cursor << 0;
    }
    spool_records_ = 0;
    settled_records_ = 0;
}

bool TurnausClient::deliver(const OwedPush &item)
{
    attempts_++;
    std::map<std::string, std::string> headers;
    headers["Authorization"] = "Bearer " + credential_;
    headers["Accept"] = "application/json";

    odhttp::Response res = odhttp::postJson(url_, item.path, item.body, headers,
                                            config_.connect_timeout_s, config_.read_timeout_s);
    if (!res.reached_a_server())
    {
        return false;
    }
    if (res.status == 202)
    {
        std::string outcome;
        try
        {
            json j = json::parse(res.body);
            outcome = j["data"].value("outcome", "");
        }
        catch (const std::exception &)
        {
        }
        delivered_++;
        if (!outcome.empty())
        {
            log_info("TURNAUS: " + outcome + " " + item.body);
        }
        return true;
    }
    if (res.status == 401 || res.status == 403)
    {
        // The credential is gone or was never right. Retrying a 401 forever is a board
        // hammering a server it will never satisfy, so the client stops pushing and says
        // what to do. The spool is left alone: the darts are still owed.
        log_error("TURNAUS: this board's credential was refused (HTTP " + std::to_string(res.status) +
                  "). Re-pair with --pair <code>. Nothing further will be pushed.");
        paired_ = false;
        return false;
    }
    if (res.status == 409)
    {
        // The club has stood this board at no Station. A retry cannot fix it either, but
        // it is a state somebody clears in the browser, so it is worth retrying slowly.
        log_warning("TURNAUS: the server says this board stands at no Station (409)");
        return false;
    }
    if (res.status == 422)
    {
        // The server will refuse this body every time it is sent. Drop it rather than
        // retry it forever, and count it.
        dropped_++;
        log_warning("TURNAUS: a push was refused as unreadable (422) and will not be retried");
        return true; // "done with it", so the cursor advances past it
    }
    return false; // 5xx, 429, anything else: worth another go
}

void TurnausClient::start()
{
    if (!paired_)
    {
        log_info("TURNAUS: no credential, so nothing is pushed. Scoring and the score "
                 "WebSocket are unaffected; pair with --pair <code>.");
        return;
    }
    if (!url_.valid || (!url_.tls && !config_.allow_plaintext) || (url_.tls && !odhttp::tlsAvailable()))
    {
        log_error("TURNAUS: the address is not one this build may post to; nothing will be pushed");
        paired_ = false;
        return;
    }
    od_paths::ensureDir(od_paths::configDir());
    loadSpool();
    running_ = true;
    worker_ = std::thread(&TurnausClient::run, this);
    log_info("TURNAUS: pushing to " + url_.host + " over " + std::string(odhttp::transportName()) +
             (url_.tls ? " (TLS)" : " (plaintext, --allow-plaintext)"));
}

void TurnausClient::stop()
{
    if (!running_.exchange(false))
    {
        return;
    }
    condition_.notify_all();
    if (worker_.joinable())
    {
        worker_.join();
    }

    // Whatever the worker never reached is written down on the way out, so that leaving
    // early costs no dart. The worker is joined, so this is the only thread there is.
    size_t written_on_exit = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (OwedPush &item : queue_)
        {
            if (!item.spooled)
            {
                spool(item);
                item.spooled = true;
                written_on_exit++;
            }
        }
    }
    if (written_on_exit)
    {
        log_info("TURNAUS: wrote " + std::to_string(written_on_exit) +
                 " unsent push(es) to the spool on the way out");
    }

    log_info("TURNAUS: client stopped. queued=" + std::to_string(queued_.load()) +
             " delivered=" + std::to_string(delivered_.load()) +
             " attempts=" + std::to_string(attempts_.load()) +
             " dropped=" + std::to_string(dropped_.load()) +
             " still_owed=" + std::to_string(backlog()));
}

size_t TurnausClient::backlog() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size();
}

void TurnausClient::run()
{
    int backoff_ms = config_.backoff_initial_ms;

    while (running_)
    {
        OwedPush item;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            condition_.wait_for(lock, std::chrono::milliseconds(200),
                                [this] { return !queue_.empty() || !running_; });
            // THE SHUTDOWN RULE, and getting it wrong is how this client first hung.
            // The worker leaves the moment running_ drops, WHATEVER is still in the
            // queue -- it does not try to drain it. Draining is what a board with no
            // network would do forever, and ~TurnausClient's join would never return:
            // measured here as a run that reached its cycle budget, printed its
            // control and then never exited.
            //
            // Leaving early costs nothing, because what is owed is owed on disk. The
            // spool is written before the post, and anything the worker never reached
            // is written on the way out, below. A dart survives the exit in the file,
            // not in the thread.
            if (!running_)
            {
                break;
            }
            if (queue_.empty())
            {
                continue;
            }
            // Copied out under the lock; the lock is released before the POST. This is
            // the line that keeps an HTTP timeout off every other thread in the program.
            item = queue_.front();
        }

        // Spool before posting, exactly once per item. A dart written down before it is
        // sent is a dart a power cut cannot take.
        if (!item.spooled)
        {
            spool(item);
            item.spooled = true;
            std::lock_guard<std::mutex> lock(mutex_);
            if (!queue_.empty())
            {
                queue_.front().spooled = true;
            }
        }

        bool settled = deliver(item);

        if (settled)
        {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (!queue_.empty())
                {
                    queue_.pop_front();
                }
            }
            settleOneRecord();
            compactSpoolIfSettled();
            backoff_ms = config_.backoff_initial_ms;
            continue;
        }

        if (!paired_)
        {
            break; // a refused credential; deliver() has said so
        }

        // The network is gone, or the server is unwell. Wait, and wake immediately if
        // the program is shutting down -- so #825's exit path never waits 30 seconds for
        // a sleeping worker.
        std::unique_lock<std::mutex> lock(mutex_);
        condition_.wait_for(lock, std::chrono::milliseconds(backoff_ms),
                            [this] { return !running_.load(); });
        backoff_ms = backoff_ms * 2;
        if (backoff_ms > config_.backoff_max_ms)
        {
            backoff_ms = config_.backoff_max_ms;
        }
    }
}
