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

    /**
     * #1351: the identity one owed push is recognised by, wherever the queue has moved
     * it. The worker copies the front out and releases the lock before the POST; by the
     * time it locks again the queue may have been reshaped underneath it (a Contest
     * ending erases from the middle), so "the front" is not a name and this is.
     */
    bool sameOwedPush(const OwedPush &a, const OwedPush &b)
    {
        return a.idempotency_key == b.idempotency_key && a.path == b.path && a.body == b.body;
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

const std::string &TurnausClient::credentialFor(Binding binding) const
{
    return binding == Binding::Contest ? contest_credential_ : credential_;
}

const char *TurnausClient::detectionsPath(Binding binding)
{
    return binding == Binding::Contest ? "/api/v1/casual/detections" : "/api/v1/autoscorer/detections";
}

const char *TurnausClient::takeoutsPath(Binding binding)
{
    return binding == Binding::Contest ? "/api/v1/casual/takeouts" : "/api/v1/autoscorer/takeouts";
}

const char *TurnausClient::heartbeatsPath(Binding binding)
{
    return binding == Binding::Contest ? "/api/v1/casual/heartbeats" : "/api/v1/autoscorer/heartbeats";
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
        // #891: the second binding, if this board took an evening's code. Absent from
        // every file #822 wrote and from every board that never paired to a Contest,
        // which is why it is read with value() rather than asked for.
        if (j.contains("contest") && j["contest"].is_object())
        {
            contest_credential_ = j["contest"].value("token", "");
            contest_id_ = j["contest"].value("casual_contest_id", (long long)0);
            contest_board_id_ = std::to_string(j["contest"].value("board_id", 0));
            contest_bound_ = !contest_credential_.empty();
        }
    }
    catch (const std::exception &)
    {
        log_warning("TURNAUS: the credential file could not be parsed; this board is unpaired");
        return false;
    }
    if (credential_.empty() && !contest_bound_.load())
    {
        return false;
    }
    if (contest_bound_.load())
    {
        // Which evening, and whether the club binding is still underneath it. Neither is
        // a secret: one is the Contest's own identifier and the other is a yes/no.
        log_info("TURNAUS: bound to Casual Contest " + std::to_string(contest_id_) +
                 (credential_.empty() ? " (and to no Organisation)" : " (over an Organisation pairing kept underneath it)"));
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

TurnausClient::PairingBlock TurnausClient::pairingBlock() const
{
    if (!url_.valid)
    {
        return PairingBlock::BadAddress;
    }
    if (url_.tls && !odhttp::tlsAvailable())
    {
        return PairingBlock::NoTls;
    }
    if (!url_.tls && !config_.allow_plaintext)
    {
        return PairingBlock::Plaintext;
    }
    if (config_.credentials_path.empty())
    {
        return PairingBlock::NoConfigDir;
    }
    return PairingBlock::None;
}

std::string TurnausClient::pairingLabel() const
{
    // #1259: the computer's name, as the board already announces itself, rather than the
    // literal every board used to send -- so a club's list of boards says which machine is
    // which. Both doors validate `max:80` (characters); 80 bytes is never more than that,
    // and a cut is moved back off a UTF-8 continuation byte so it never splits a character.
    std::string label = config_.label.empty() ? std::string("OpenDartboard") : config_.label;
    if (label.size() > 80)
    {
        size_t cut = 80;
        while (cut > 0 && (static_cast<unsigned char>(label[cut]) & 0xC0) == 0x80)
        {
            cut--;
        }
        label.resize(cut);
    }
    return label;
}

TurnausClient::Redemption TurnausClient::redeem(Door door, const std::string &code)
{
    Redemption out;
    if (pairingBlock() != PairingBlock::None)
    {
        out.kind = Redemption::Kind::Blocked;
        return out;
    }
    const bool contest_door = door == Door::Contest;

    json body;
    body["pin"] = code;
    body["label"] = pairingLabel();

    // Accept names JSON so a refusal is the door's 422 and not a redirect meant for a
    // browser. The body carries a label from gethostname, which is not promised to be
    // UTF-8 on Windows; replace rather than throw.
    std::map<std::string, std::string> headers;
    headers["Accept"] = "application/json";
    odhttp::Response res = odhttp::postJson(url_, contest_door ? "/api/v1/casual/boards" : "/api/v1/autoscorer/devices",
                                            body.dump(-1, ' ', false, json::error_handler_t::replace), headers,
                                            config_.connect_timeout_s, config_.read_timeout_s);
    if (!res.reached_a_server())
    {
        out.kind = Redemption::Kind::Unreachable;
        out.detail = res.transport_error;
        return out;
    }
    out.status = res.status;
    if (res.status == 422)
    {
        out.kind = Redemption::Kind::Refused;
        return out;
    }
    if (res.status == 429)
    {
        out.kind = Redemption::Kind::RateLimited;
        out.retry_after_s = res.retry_after_s;
        return out;
    }
    if (res.status != 201)
    {
        out.kind = Redemption::Kind::Unexpected;
        return out;
    }

    out.kind = Redemption::Kind::NotKept;
    const std::string what = contest_door ? "Contest pairing" : "pairing";
    try
    {
        json j = json::parse(res.body);
        std::string token = j["data"].value("token", "");
        if (token.empty())
        {
            log_error("TURNAUS: " + what + " answered 201 with no credential in it");
            return out;
        }
        // #891: whatever is already in the file is kept. The club half is written over a
        // club half and a Contest half beside it; neither pairing costs the other.
        json stored = json::object();
        std::string existing;
        if (od_paths::readFile(config_.credentials_path, existing))
        {
            try
            {
                json previous = json::parse(existing);
                if (previous.is_object())
                {
                    stored = previous;
                }
            }
            catch (const std::exception &)
            {
                // An unreadable file is replaced rather than merged into. Nothing in it
                // could be trusted to name a binding.
            }
        }

        json contest;
        if (contest_door)
        {
            // THE DECISION, in four lines: the Organisation binding already in this file is
            // left exactly where it is, and the Contest binding is written beside it.
            contest["token"] = token;
            contest["casual_contest_id"] = j["data"].value("casualContestId", 0);
            contest["board_id"] = j["data"].contains("board") ? j["data"]["board"].value("id", 0) : 0;
            contest["label"] = j["data"].contains("board") ? j["data"]["board"].value("label", "") : "";
            stored["contest"] = contest;
            if (!stored.contains("base_url"))
            {
                stored["base_url"] = config_.base_url;
            }
        }
        else
        {
            stored["token"] = token;
            stored["organisation_id"] = j["data"].value("organisationId", 0);
            stored["device_id"] = j["data"]["device"].value("id", 0);
            stored["label"] = j["data"]["device"].value("label", "");
            stored["base_url"] = config_.base_url;
        }

        // The directory the credential is really in, which is configDir() unless
        // --credentials named another; creating configDir() for a file kept elsewhere was
        // #822's shape and is kept, so a default start is byte-for-byte what it was.
        if (!od_paths::ensureDir(od_paths::configDir()))
        {
            log_error("TURNAUS: could not create the configuration directory");
            return out;
        }
        if (!od_paths::writeSecret(config_.credentials_path, stored.dump(2)))
        {
            log_error("TURNAUS: could not write the credential file");
            return out;
        }
        if (contest_door)
        {
            contest_credential_ = token;
            contest_id_ = contest.value("casual_contest_id", (long long)0);
            contest_board_id_ = std::to_string(contest.value("board_id", 0));
            contest_bound_ = true;
        }
        else
        {
            credential_ = token;
            device_id_ = std::to_string(stored.value("device_id", 0));
        }
        paired_ = true;
    }
    catch (const std::exception &e)
    {
        log_error("TURNAUS: " + what + " response could not be read: " + std::string(e.what()));
        return out;
    }

    out.kind = Redemption::Kind::Paired;
    if (contest_door)
    {
        log_info("TURNAUS: paired to Casual Contest " + std::to_string(contest_id_) + " as board " +
                 contest_board_id_ + ", credential kept at " + config_.credentials_path);
    }
    else
    {
        // Device id and label are not secrets; the token is, and it is not here.
        log_info("TURNAUS: paired as device " + device_id_ + ", credential kept at " + config_.credentials_path);
    }
    return out;
}

bool TurnausClient::pair(const std::string &code)
{
    // #1259: the exchange itself is redeem()'s; what --pair prints is unchanged, line for line.
    switch (pairingBlock())
    {
    case PairingBlock::BadAddress:
        log_error("TURNAUS: --turnaus is not a URL this build can parse");
        return false;
    case PairingBlock::NoTls:
        log_error("TURNAUS: this build has no TLS transport (" + std::string(odhttp::transportName()) +
                  ") and will not downgrade an https:// address to plaintext");
        return false;
    case PairingBlock::Plaintext:
        log_error("TURNAUS: refusing to send a pairing code over http://. Pass --allow-plaintext "
                  "if this really is a loopback or a lab.");
        return false;
    case PairingBlock::NoConfigDir:
        log_error("TURNAUS: no writable configuration directory, so a credential could not be kept");
        return false;
    case PairingBlock::None:
        break;
    }

    Redemption r = redeem(Door::Organisation, code);
    if (r.kind == Redemption::Kind::Unreachable)
    {
        log_error("TURNAUS: pairing could not reach " + url_.host + ": " + r.detail);
    }
    else if (r.kind == Redemption::Kind::Refused || r.kind == Redemption::Kind::RateLimited ||
             r.kind == Redemption::Kind::Unexpected)
    {
        // The body is not echoed. A 201 body carries the token, and a habit of echoing
        // the body is how a token reaches a log line on the one status that matters.
        log_error("TURNAUS: pairing refused with HTTP " + std::to_string(r.status) +
                  " (a code is six digits, single use, and expires in ten minutes)");
    }
    return r.kind == Redemption::Kind::Paired;
}

bool TurnausClient::pairContest(const std::string &code)
{
    // The same four refusals `pair()` makes before it dials, in the words --pair-contest
    // has always printed.
    switch (pairingBlock())
    {
    case PairingBlock::BadAddress:
        log_error("TURNAUS: --turnaus is not a URL this build can parse");
        return false;
    case PairingBlock::NoTls:
        log_error("TURNAUS: this build has no TLS transport (" + std::string(odhttp::transportName()) +
                  ") and will not downgrade an https:// address to plaintext");
        return false;
    case PairingBlock::Plaintext:
        log_error("TURNAUS: refusing to send a Contest pairing code over http://. Pass "
                  "--allow-plaintext if this really is a loopback or a lab.");
        return false;
    case PairingBlock::NoConfigDir:
        log_error("TURNAUS: no writable configuration directory, so a credential could not be kept");
        return false;
    case PairingBlock::None:
        break;
    }

    Redemption r = redeem(Door::Contest, code);
    if (r.kind == Redemption::Kind::Unreachable)
    {
        log_error("TURNAUS: Contest pairing could not reach " + url_.host + ": " + r.detail);
    }
    else if (r.kind == Redemption::Kind::Refused || r.kind == Redemption::Kind::RateLimited ||
             r.kind == Redemption::Kind::Unexpected)
    {
        // Not echoed, for `pair()`'s reason: a 201 body carries the token. #887 answers
        // every refusal in the same words, on purpose, so a guessing loop learns nothing.
        log_error("TURNAUS: Contest pairing refused with HTTP " + std::to_string(r.status) +
                  " (a code is six digits, single use, expires in ten minutes, and dies with the Contest)");
    }
    return r.kind == Redemption::Kind::Paired;
}

bool TurnausClient::forgetContestInFile()
{
    std::string existing;
    if (!od_paths::readFile(config_.credentials_path, existing))
    {
        return false;
    }
    try
    {
        json stored = json::parse(existing);
        if (!stored.is_object() || !stored.contains("contest"))
        {
            return false;
        }
        stored.erase("contest");
        // Through writeSecret, so the mode is the one the pairing set and a token that is
        // no longer valid does not land in a file anybody may read.
        return od_paths::writeSecret(config_.credentials_path, stored.dump(2));
    }
    catch (const std::exception &)
    {
        return false;
    }
}

void TurnausClient::releaseContestBinding(const char *why)
{
    if (!contest_bound_.exchange(false))
    {
        return;
    }
    const long long was = contest_id_;
    contest_credential_.clear();
    contest_id_ = 0;

    // Everything still owed to that evening. Dropped, counted, and said out loud -- the
    // opposite of what a refused Organisation credential does, and §"Give-up" in the
    // document argues the asymmetry. Nobody re-pairs to a Contest that has been Given Up.
    size_t abandoned = 0;
    std::vector<long long> written_records; // #1351: settled by name, below
    bool round_abandoned = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        // #1276: the round in hand goes with the evening it was begun on. Remembered as
        // abandoned rather than forgotten, so the takeout that ends it can say which
        // Contest it belonged to -- and so the next dart begins a fresh round at the
        // club's door instead of joining one the club never saw the beginning of.
        if (round_ == RoundInHand::Open && round_binding_ == Binding::Contest)
        {
            round_ = RoundInHand::AbandonedWithContest;
            round_abandoned = true;
        }
        for (std::deque<OwedPush>::iterator it = queue_.begin(); it != queue_.end();)
        {
            if (it->binding == Binding::Contest)
            {
                if (it->spooled && it->spool_index >= 0)
                {
                    written_records.push_back(it->spool_index);
                }
                it = queue_.erase(it);
                dropped_++;
                abandoned++;
            }
            else
            {
                ++it;
            }
        }
    }

    // MEASURED, AND IT IS THE FINDING THIS SLICE DID NOT KNOW IT WAS ASKING FOR. A
    // record dropped out of the queue without settling it left the run one short: the
    // run afterwards resumed a club dart that had already been delivered, and only
    // #821's dedup made it harmless. A dart that is never retried is settled, by the
    // same definition the horizon uses two functions up, and it is settled HERE.
    //
    // #1351: BY NAME now, not by count. Settling "that many records" moved the cursor
    // over the LEADING records whatever they were, and these erasures come from the
    // middle of the queue -- with club records interleaved ahead of them in the file,
    // the count covered a leading club record that was never delivered, and a restart
    // skipped it. The ledger settles the record each erased item really became, and the
    // cursor stops at the first record still owed.
    for (long long record : written_records)
    {
        settleRecord(record);
    }

    log_warning("TURNAUS: the binding to Casual Contest " + std::to_string(was) + " has ended (" +
                std::string(why) + "). " + std::to_string(abandoned) +
                " push(es) owed to it were dropped rather than kept: a Contest that has been given "
                "up cannot be re-paired to, so a dart still owed to it could only ever land in "
                "somebody else's evening.");
    if (round_abandoned)
    {
        // #1276. Said here as well as at the takeout, because a run that ends before the
        // next END would otherwise never mention the round that went with the evening.
        log_info("TURNAUS: the round in hand was begun on that Contest, so it ends with it. Its "
                 "takeout will not be sent anywhere, and the next dart begins a new round.");
    }
    forgetContestInFile();
    beat_unsupported_ = false;
    binding_changed_ = true;

    if (!credential_.empty())
    {
        log_info("TURNAUS: this board is still paired to its Organisation, so Match scoring "
                 "continues. Pair to another Contest with --pair-contest <code>.");
        return;
    }
    // A board in somebody's garage holds nothing else. It stops, rather than retrying an
    // evening that is over for as long as the machine is switched on.
    paired_ = false;
    unpaired_while_running_ = true; // #1259: a garage board's evening ended; ask, if anybody is there
    log_info("TURNAUS: this board holds no other binding, so nothing further will be pushed. "
             "Pair to a new Contest with --pair-contest <code>.");
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
    // #891: stamped once, here, on the scoring thread's own item. Everything downstream
    // reads it off the item rather than off the client, so a restart cannot deliver a
    // Tuesday dart into a Wednesday evening.
    item.binding = destination();
    item.contest_id = item.binding == Binding::Contest ? contest_id_ : 0;
    if (result.score == "END")
    {
        // The published END is the takeout: #821's /takeouts is the seam where the Visit
        // and its Detections are appended (ADR-0055), and nothing is written on the third
        // dart. A takeout carries no reference and needs none -- a repeated takeout finds
        // no round in hand and answers with a null visitId, so it is idempotent by the
        // server's own construction.
        //
        // #1276: AND IT BELONGS TO THE ROUND, NOT TO THE CLOCK. `destination()` is where a
        // dart thrown *now* would go; a takeout ends a round begun some seconds ago, and
        // after an evening is given up mid-round those are two different doors. Sent at
        // the live one it closes a round that door never saw. Turnaus writes nothing for
        // it -- an empty takeout is a null Visit and no row -- so no data is harmed; what
        // is wrong is that the board has reported something that did not happen, and a
        // board that reports what did not happen is the thing this client exists not to
        // be. So it is dropped, which is #891's rule for a dart owed to an ended Contest
        // applied to the takeout of the round that dart belonged to.
        //
        // DELIBERATELY NARROW, and #1276 says why. Only a round begun on an evening that
        // has ended is dropped. A takeout arriving when THIS PROCESS opened no round --
        // a restart in the middle of a round, whose darts were resumed from the spool
        // rather than offered here -- still goes out at the live binding exactly as it
        // always did. Dropping that one would leave those darts sitting in the server's
        // round in hand until some later takeout closed them into the wrong turn, which
        // is a worse failure than the one this slice is about and is not what it was
        // asked to change.
        std::string dropped_because;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (round_ == RoundInHand::AbandonedWithContest)
            {
                dropped_because = "the round it ends was begun on Casual Contest " +
                                  std::to_string(round_contest_id_) + ", which has ended";
            }
            else if (round_ == RoundInHand::Open && round_binding_ == Binding::Contest &&
                     (!contest_bound_.load() || round_contest_id_ != contest_id_))
            {
                // The same verdict reached without releaseContestBinding() having run --
                // a board re-paired to another evening while a round was in hand.
                dropped_because = "the round it ends was begun on Casual Contest " +
                                  std::to_string(round_contest_id_) + ", which this board is no longer on";
            }
            else if (round_ == RoundInHand::Open)
            {
                item.binding = round_binding_;
                item.contest_id = round_contest_id_;
            }
            // else: RoundInHand::None, and the item keeps `destination()` -- which is
            // exactly where this takeout went before #1276. `round_binding_` still holds
            // the last round's door and must not be read here: nothing was begun.
            round_ = RoundInHand::None;
        }
        if (!dropped_because.empty())
        {
            dropped_++;
            log_warning("TURNAUS: a takeout was dropped rather than sent: " + dropped_because +
                        ". Sending it anyway would close a round at a door that never saw one.");
            return false;
        }
        item.path = takeoutsPath(item.binding);
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
        item.path = detectionsPath(item.binding);
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
        // #1276: a round is begun by the first dart that really entered the queue, and it
        // remembers the door that dart is going out of. A dart dropped by the policy above
        // opens nothing -- a takeout ending a round nothing was ever sent for would close
        // an empty round at a door that saw none of it, which is the same failure one line
        // further back.
        if (result.score != "END" && round_ != RoundInHand::Open)
        {
            round_ = RoundInHand::Open;
            round_binding_ = item.binding;
            round_contest_id_ = item.contest_id;
        }
    }
    condition_.notify_one();
    return true;
}

void TurnausClient::spool(OwedPush &item)
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
        //
        // #1351: and the item's spool_index stays -1, which the ledger refuses to
        // count. The old count-cursor advanced on delivery whether or not the record
        // had reached the file, so a full disk made the cursor cover one real record
        // per unwritten one -- the same overshoot the middle-of-queue erasures had,
        // arrived at through the filesystem.
        return;
    }
    json line;
    line["path"] = item.path;
    line["body"] = item.body;
    line["key"] = item.idempotency_key;
    // #891. A spool record that does not name its binding is a record a restart delivers
    // to whichever binding happens to be live, which is the one thing the horizon exists
    // to prevent, arrived at by a different door. Absent on a file #822 wrote, and read
    // back as "organisation", which is what such a file held.
    line["binding"] = item.binding == Binding::Contest ? "contest" : "organisation";
    line["contest_id"] = item.contest_id;
    line["spooled_ms"] = (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::system_clock::now().time_since_epoch())
                             .count();
    out << line.dump() << "\n";
    out.flush();
    // #1351: the record's name, stamped on the item that became it. Appends are one
    // thread at a time -- the worker, or spoolUnwritten() after the worker is joined --
    // so the ledger's count is the file's.
    std::lock_guard<std::mutex> lock(spool_mutex_);
    item.spool_index = ledger_.recordAppended();
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
        // #1351: the cursor file still says what it always said -- how many LEADING
        // records are settled -- so every file an earlier build wrote reads the same.
        ledger_.startFrom(strtoull(cursor_raw.c_str(), nullptr, 10));
    }
    std::ifstream in(spool_path_.c_str(), std::ios::binary);
    if (!in)
    {
        ledger_.clear();
        return;
    }

    uint64_t now = (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::system_clock::now().time_since_epoch())
                       .count();
    std::string line;
    size_t resumed = 0, stale = 0, orphaned = 0;

    while (std::getline(in, line))
    {
        if (line.empty())
        {
            continue;
        }
        const long long here = ledger_.recordAppended();
        if ((uint64_t)here < ledger_.settledPrefix())
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
            // #891. A record #822 wrote carries neither field, and "organisation" is
            // exactly what such a record was owed to.
            item.binding = j.value("binding", std::string("organisation")) == "contest"
                               ? Binding::Contest
                               : Binding::Organisation;
            item.contest_id = j.value("contest_id", (long long)0);
        }
        catch (const std::exception &)
        {
            // A torn last line is what a power cut leaves behind. It is settled by
            // being unreadable; there is nothing to retry. #1351: settled by its own
            // index -- the old assignment covered every record before it as well, which
            // for a mid-file line would have been records this scan just resumed.
            ledger_.settle(here);
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
            ledger_.settle(here);
            continue;
        }

        if (item.path.empty())
        {
            ledger_.settle(here);
            continue;
        }

        // #891: THE SECOND HORIZON, and it is a binding rather than a clock. A record
        // owed to a Contest this board is no longer bound to -- the evening was given up,
        // or this board has since taken a different evening's code -- can never be
        // delivered where it was owed, and posting it anywhere else is the fifteen-minute
        // failure without the fifteen minutes. It is settled here, counted, and named.
        if (item.binding == Binding::Contest && (!contest_bound_.load() || item.contest_id != contest_id_))
        {
            orphaned++;
            dropped_++;
            // #1351: its own index and nothing more. The old assignment declared every
            // record BEFORE the orphan settled too -- including club records this very
            // scan had just resumed into the queue, which one delivery would then write
            // into the cursor, and a crash before they delivered lost them.
            ledger_.settle(here);
            continue;
        }

        item.spooled = true; // it is already in the file; do not write it twice
        item.spool_index = here;
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push_back(item);
        resumed++;
    }

    if (resumed || stale || orphaned)
    {
        log_info("TURNAUS: resumed " + std::to_string(resumed) + " owed push(es) from the spool; " +
                 std::to_string(stale) + " abandoned as older than the round they belonged to; " +
                 std::to_string(orphaned) + " abandoned as owed to a Contest this board is no longer bound to");
    }
}

void TurnausClient::settleRecord(long long index)
{
    // #1351: any order, any thread. The ledger holds settlement apart from the cursor:
    // the file is rewritten only when the settled PREFIX advanced, so it can never name
    // a count that covers a record still owed.
    std::lock_guard<std::mutex> lock(spool_mutex_);
    if (!ledger_.settle(index))
    {
        return;
    }
    std::ofstream out(cursor_path_.c_str(), std::ios::binary | std::ios::trunc);
    if (out)
    {
        out << ledger_.settledPrefix();
        out.flush();
    }
}

void TurnausClient::compactSpoolIfSettled()
{
    // Nothing owed and nothing unsettled: the spool has done its job and may start again
    // at nothing, so a board left running for a month does not accumulate a file.
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!queue_.empty())
        {
            return;
        }
    }
    std::lock_guard<std::mutex> spool_lock(spool_mutex_);
    if (ledger_.records() == 0 || !ledger_.allSettled())
    {
        return;
    }
    std::ofstream truncate_spool(spool_path_.c_str(), std::ios::binary | std::ios::trunc);
    std::ofstream truncate_cursor(cursor_path_.c_str(), std::ios::binary | std::ios::trunc);
    if (truncate_cursor)
    {
        truncate_cursor << 0;
    }
    ledger_.clear();
}

bool TurnausClient::deliver(const OwedPush &item)
{
    attempts_++;
    std::map<std::string, std::string> headers;
    // #891: the item's own binding, not this board's current one.
    headers["Authorization"] = "Bearer " + credentialFor(item.binding);
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
        if (item.binding == Binding::Contest)
        {
            // #891: THE GIVE-UP CASE, and it is a real one rather than a hypothetical.
            // #887's `CasualContestBoard::release()` DELETES the token when the evening
            // is given up, so the guard refuses the next push rather than some check
            // somebody had to remember to write. A board cannot tell that from a
            // credential somebody revoked, and does not need to: both mean this evening
            // is over for this board. It stops pushing into the Contest -- it does not
            // retry it, this run or any later one -- and falls back to the club binding
            // if it has one.
            releaseContestBinding(res.status == 401 ? "HTTP 401" : "HTTP 403");
            return false;
        }
        // The credential is gone or was never right. Retrying a 401 forever is a board
        // hammering a server it will never satisfy, so the client stops pushing and says
        // what to do. The spool is left alone: the darts are still owed.
        log_error("TURNAUS: this board's credential was refused (HTTP " + std::to_string(res.status) +
                  "). Re-pair with --pair <code>. Nothing further will be pushed.");
        paired_ = false;
        unpaired_while_running_ = true; // #1259: an interactive board asks for a new code
        return false;
    }
    if (res.status == 404 && item.binding == Binding::Contest)
    {
        // A deployment that does not serve the Casual door at all. Retrying is a board
        // posting into a wall for the length of an evening, and the person who can fix it
        // is the one reading this line on a pub PC.
        log_error("TURNAUS: this deployment has no Casual push address (404 at " + item.path +
                  "). Nothing further will be pushed into that Contest.");
        releaseContestBinding("HTTP 404");
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
    // #892: idempotent, because the beat now starts in main -- before the cameras are
    // opened, so that INITIALISING and CALIBRATING are beaten rather than passed in
    // silence -- and `Scorer::run()` goes on calling this the way #822 wrote it.
    std::lock_guard<std::mutex> lifecycle(lifecycle_mutex_);
    if (running_.load())
    {
        return;
    }
    if (quiesced_.load())
    {
        // #1259: stopped for a new code at the console. resume() restarts the threads over
        // the queue exactly as it stands; loading the spool again here would owe it twice.
        return;
    }
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
    // #892. Two threads and one flag: `stop()` drops `running_`, notifies both condition
    // variables and joins both, so neither can outlive the other or the object.
    beater_ = std::thread(&TurnausClient::beat, this);
    log_info("TURNAUS: pushing to " + url_.host + " over " + std::string(odhttp::transportName()) +
             (url_.tls ? " (TLS)" : " (plaintext, --allow-plaintext)"));
}

void TurnausClient::haltThreads()
{
    condition_.notify_all();
    beat_condition_.notify_all();
    if (worker_.joinable())
    {
        worker_.join();
    }
    if (beater_.joinable())
    {
        beater_.join();
    }
}

size_t TurnausClient::spoolUnwritten()
{
    // Whatever the worker never reached is written down, so that leaving early costs no
    // dart. The worker is joined, so this is the only thread there is.
    size_t written = 0;
    std::lock_guard<std::mutex> lock(mutex_);
    for (OwedPush &item : queue_)
    {
        if (!item.spooled)
        {
            spool(item);
            item.spooled = true;
            written++;
        }
    }
    return written;
}

void TurnausClient::quiesce()
{
    std::lock_guard<std::mutex> lifecycle(lifecycle_mutex_);
    if (!running_.exchange(false))
    {
        return;
    }
    haltThreads();
    spoolUnwritten();
    // #1259: BOTH threads can meet the same refusal -- the push worker on a dart and the beat
    // thread on the next beat -- and each sets the flag. Taking it once and asking once is
    // right; what is wrong is the second one outliving the pairing that answered it, which
    // asked the person for a second code the moment the first had worked (measured on
    // Windows). The threads are joined here, so nothing can set it again until they run.
    unpaired_while_running_ = false;
    quiesced_ = true;
}

bool TurnausClient::resume()
{
    std::lock_guard<std::mutex> lifecycle(lifecycle_mutex_);
    if (!quiesced_.load() || !paired_.load() || running_.load())
    {
        return false;
    }
    quiesced_ = false;
    // The queue is left as it stands: everything in it is already in the spool (quiesce()
    // wrote what the worker had not), and it is owed to whoever this board is paired to now.
    running_ = true;
    worker_ = std::thread(&TurnausClient::run, this);
    beater_ = std::thread(&TurnausClient::beat, this);
    log_info("TURNAUS: pushing to " + url_.host + " over " + std::string(odhttp::transportName()) +
             (url_.tls ? " (TLS)" : " (plaintext, --allow-plaintext)") + " again, after a new pairing");
    return true;
}

void TurnausClient::stop()
{
    std::lock_guard<std::mutex> lifecycle(lifecycle_mutex_);
    // #1259: a client quiesced for a new code and never resumed still owes its summary.
    if (!running_.exchange(false) && !quiesced_.exchange(false))
    {
        return;
    }
    haltThreads();

    size_t written_on_exit = spoolUnwritten();
    if (written_on_exit)
    {
        log_info("TURNAUS: wrote " + std::to_string(written_on_exit) +
                 " unsent push(es) to the spool on the way out");
    }

    log_info("TURNAUS: client stopped. queued=" + std::to_string(queued_.load()) +
             " delivered=" + std::to_string(delivered_.load()) +
             " attempts=" + std::to_string(attempts_.load()) +
             " dropped=" + std::to_string(dropped_.load()) +
             " still_owed=" + std::to_string(backlog()) +
             " beats=" + std::to_string(beats_.load()) +
             " beats_lost=" + std::to_string(beats_lost_.load()) +
             // #891. Which binding was live at the end, and never anything about either
             // credential: a Contest's identifier is the evening's own, and "none" is a
             // board that was refused or never paired.
             " binding=" + (contest_bound_.load() ? "contest:" + std::to_string(contest_id_)
                                                  : (credential_.empty() ? std::string("none")
                                                                         : std::string("organisation"))));
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
        bool still_owed = true;
        if (!item.spooled)
        {
            spool(item);
            item.spooled = true;
            std::lock_guard<std::mutex> lock(mutex_);
            // #1351: mark the item that was spooled, BY NAME. The shipped code marked
            // `queue_.front()`, and the front is only this item until the queue is
            // reshaped underneath the write -- a Contest ending on the beat thread
            // erases from the middle -- after which the front was some other item,
            // marked spooled though it was never written, and its later delivery moved
            // the cursor over a record the file does not hold.
            OwedPush *owner = nullptr;
            for (OwedPush &owed : queue_)
            {
                if (!owed.spooled && sameOwedPush(owed, item))
                {
                    owner = &owed;
                    break;
                }
            }
            if (owner)
            {
                owner->spooled = true;
                owner->spool_index = item.spool_index;
            }
            else
            {
                // The item left the queue while it was becoming a record -- dropped
                // with its Contest. Nothing owes it any more, and a record nothing
                // will retry is settled (#891's definition), or the cursor wedges
                // under it for the life of the file.
                still_owed = false;
            }
        }
        if (!still_owed)
        {
            settleRecord(item.spool_index);
            compactSpoolIfSettled();
            continue;
        }

        bool settled = deliver(item);

        if (settled)
        {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                // #1351: pop what was delivered, which is the front unless the queue
                // was reshaped underneath the POST. The shipped pop took the front
                // unconditionally, so a reshape made it drop an item the POST was not
                // about -- an undelivered dart, gone from memory and then from the
                // file when the cursor passed it.
                if (!queue_.empty() && sameOwedPush(queue_.front(), item))
                {
                    queue_.pop_front();
                }
                else
                {
                    for (std::deque<OwedPush>::iterator it = queue_.begin(); it != queue_.end(); ++it)
                    {
                        if (sameOwedPush(*it, item))
                        {
                            queue_.erase(it);
                            break;
                        }
                    }
                }
            }
            settleRecord(item.spool_index);
            compactSpoolIfSettled();
            backoff_ms = config_.backoff_initial_ms;
            continue;
        }

        if (!paired_)
        {
            break; // a refused credential; deliver() has said so
        }

        // #891: the Contest binding was dropped under us and its items went with it, so
        // the next item is a club one and there is nothing to back off from. Waiting here
        // would be a board idling for a second per dart because an evening ended.
        if (binding_changed_.exchange(false))
        {
            backoff_ms = config_.backoff_initial_ms;
            continue;
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

// ---------------------------------------------------------------------------------
// #892: the heartbeat. Everything below this line is the beat and nothing above it is.
// ---------------------------------------------------------------------------------

bool TurnausClient::postBeat(const char *condition_word, int &interval_s, int &silence_s)
{
    // Note what this does NOT do, because it is the acceptance criterion rather than an
    // omission: no `OwedPush` is built, `newIdempotencyKey()` is not called, `queue_` is
    // not touched, `spool()` is not called, and `attempts_`/`delivered_`/`dropped_` --
    // the dart counters -- do not move. A beat is a message about the last interval and
    // about no turn at all.
    // #891: the beat follows the live binding, because what it is claiming -- that this
    // board can see -- is claimed to whoever is drawing this board's screen right now.
    const Binding binding = destination();
    std::map<std::string, std::string> headers;
    headers["Authorization"] = "Bearer " + credentialFor(binding);
    headers["Accept"] = "application/json";

    const std::string body = std::string("{\"condition\":\"") + condition_word + "\"}";

    odhttp::Response res = odhttp::postJson(url_, heartbeatsPath(binding), body, headers,
                                            config_.connect_timeout_s, config_.read_timeout_s);
    if (!res.reached_a_server())
    {
        return false;
    }
    if (res.status == 200)
    {
        try
        {
            json j = json::parse(res.body);
            interval_s = j["data"].value("intervalSeconds", 0);
            silence_s = j["data"].value("silenceSeconds", 0);
        }
        catch (const std::exception &)
        {
            interval_s = 0;
            silence_s = 0;
        }
        return true;
    }
    if (res.status == 401 || res.status == 403)
    {
        if (binding == Binding::Contest)
        {
            // #891: the beat is often what meets a Given Up Contest first, because it
            // goes every interval while darts go only when somebody throws. Same verdict
            // as `deliver()`'s, reached from the other thread.
            releaseContestBinding(res.status == 401 ? "HTTP 401 on a heartbeat" : "HTTP 403 on a heartbeat");
            return false;
        }
        // The same refusal `deliver()` makes, for the same reason, and it has to be made
        // here too: a board whose credential is gone would otherwise go on beating for
        // ever at a server that will never write its condition down.
        log_error("TURNAUS: this board's credential was refused on a heartbeat (HTTP " +
                  std::to_string(res.status) + "). Re-pair with --pair <code>.");
        paired_ = false;
        unpaired_while_running_ = true; // #1259
        return false;
    }
    if (res.status == 404 && binding == Binding::Contest)
    {
        // #891: a deployment that serves the Casual push door but no beat at it. NOT a
        // reason to drop the binding -- the darts are the point and they are arriving --
        // and not a reason to go on posting into a 404 every interval either. Silence is
        // always available and always safe (#822 §11), so this board goes quiet about its
        // condition and says so once.
        if (!beat_unsupported_.exchange(true))
        {
            log_warning("TURNAUS: this deployment has no heartbeat address for a Casual Contest board "
                        "(404 at " + std::string(heartbeatsPath(binding)) +
                        "). This board will push darts and say nothing about its condition while it is "
                        "bound to a Contest.");
        }
        return false;
    }
    if (res.status == 409)
    {
        // The club has stood this board at no Station, so there is no row for a
        // condition to belong to. Somebody clears that in a browser; keep beating.
        log_warning("TURNAUS: the server says this board stands at no Station (409); the beat has nowhere to land");
        return false;
    }
    if (res.status == 422)
    {
        // ADR-0065 refuses an unrecognised word rather than degrading it, and says why:
        // this is our own door, and the detector's author is the person who can fix it.
        // So say the word out loud -- it is not a secret, and a beat that is silently
        // wrong is the failure this whole slice exists to remove.
        log_error("TURNAUS: the server refused the condition \"" + std::string(condition_word) +
                  "\" (422). A board may only state UPDATING, INITIALISING, CALIBRATING, READY or ERROR.");
        return false;
    }
    return false;
}

void TurnausClient::beat()
{
    // The frame count as of the previous beat. The whole of this thread's state, and the
    // reason the freshness question needs no window of its own: "has a frame arrived
    // since I last said I could see" is answered by two reads of one counter.
    uint64_t frames_at_last_beat =
        beat_state_kept_ ? kept_frames_at_last_beat_ : board_sight::framesSeen().load(); // #1259
    // Whether the ladder has already spent its one "calibration has only just finished"
    // answer. See board_sight.hpp: it is spent once and never refilled.
    bool asked_since_calibration = beat_state_kept_ ? kept_asked_since_calibration_ : false;

    // Used only until the server has answered once. After that every wait is the
    // interval the server named, and this is not consulted again.
    int backoff_ms = config_.backoff_initial_ms;

    // When this board last got a `200`. `steady_clock`, because what is being measured
    // is an elapsed time on this machine and a pub PC's wall clock may step.
    auto last_answer = std::chrono::steady_clock::now();
    bool ever_answered = false;
    bool warned_about_silence = false;

    while (running_)
    {
        // #891: a deployment with no Casual beat address has been told once and is not
        // told again. The thread stays alive rather than exiting, because the Contest
        // binding can end -- and then this board's club binding has a beat to make again.
        if (beat_unsupported_.load() && destination() == Binding::Contest)
        {
            std::unique_lock<std::mutex> lock(beat_mutex_);
            beat_condition_.wait_for(lock, std::chrono::seconds(5), [this] { return !running_.load(); });
            continue;
        }

        const board_sight::Condition condition = board_sight::conditionSince(frames_at_last_beat, asked_since_calibration);
        const char *word = board_sight::word(condition);

        int interval_s = 0;
        int silence_s = 0;
        const bool answered = postBeat(word, interval_s, silence_s);

        int wait_ms = 0;

        if (answered)
        {
            beats_++;
            last_answer = std::chrono::steady_clock::now();
            warned_about_silence = false;

            if (interval_s > 0)
            {
                const int previous = interval_s_.exchange(interval_s);
                silence_s_ = silence_s;
                if (previous != interval_s || !ever_answered)
                {
                    log_info("TURNAUS: beating " + std::string(word) + " every " + std::to_string(interval_s) +
                             "s; this server gives up on a silent board after " + std::to_string(silence_s) + "s");
                }
                if (silence_s > 0 && interval_s >= silence_s)
                {
                    // Not this board's to fix and not its to ignore either. Beating no
                    // oftener than the server gives up means every Station's screen
                    // degrades to the keypad in front of a board that is watching.
                    log_warning("TURNAUS: this server asks for a beat every " + std::to_string(interval_s) +
                                "s and gives up after " + std::to_string(silence_s) +
                                "s, so no beat of ours can keep it believing this board");
                }
                wait_ms = interval_s * 1000;
            }
            else
            {
                // A `200` that named no interval. The message was recorded, so the board
                // is not silent, but there is no cadence in the answer to adopt -- so
                // back off rather than spin, and say so once.
                if (!ever_answered)
                {
                    log_warning("TURNAUS: the server recorded a beat but named no interval; falling back to this "
                                "board's own backoff until it does");
                }
                wait_ms = backoff_ms;
                backoff_ms = backoff_ms * 2 > config_.backoff_max_ms ? config_.backoff_max_ms : backoff_ms * 2;
            }
            ever_answered = true;
        }
        else
        {
            beats_lost_++;
            if (!paired_)
            {
                break; // a refused credential; postBeat() has said so
            }

            const int known = interval_s_.load();
            if (known > 0)
            {
                // §11: a failed beat is dropped, not spooled. There is nothing to retry,
                // because the next beat carries the same claim, fresher -- so the cadence
                // does not change when one is lost, and a board coming back after an
                // outage is beating at the interval it was already told.
                wait_ms = known * 1000;
            }
            else
            {
                wait_ms = backoff_ms;
                backoff_ms = backoff_ms * 2 > config_.backoff_max_ms ? config_.backoff_max_ms : backoff_ms * 2;
            }

            // §11's other use for the second number: past the silence window the
            // Station's screen has already degraded to a keypad, and the people at this
            // board are the only ones who can see this machine. Say it here, once per
            // outage, rather than leaving them to wonder why the scoreboard stopped.
            const int silence = silence_s_.load();
            if (ever_answered && silence > 0 && !warned_about_silence)
            {
                const auto since = std::chrono::duration_cast<std::chrono::seconds>(
                                       std::chrono::steady_clock::now() - last_answer)
                                       .count();
                if (since > silence)
                {
                    log_warning("TURNAUS: nothing has reached the server for " + std::to_string(since) +
                                "s, which is past this deployment's " + std::to_string(silence) +
                                "s window. The Station's screen has degraded to the keypad and the players "
                                "should mark by hand until this line stops.");
                    warned_about_silence = true;
                }
            }
        }

        std::unique_lock<std::mutex> lock(beat_mutex_);
        beat_condition_.wait_for(lock, std::chrono::milliseconds(wait_ms),
                                 [this] { return !running_.load(); });
    }

    // #1259: whatever ended this thread -- a refusal or a stop -- the next one continues from here.
    kept_frames_at_last_beat_ = frames_at_last_beat;
    kept_asked_since_calibration_ = asked_since_calibration;
    beat_state_kept_ = true;
}
