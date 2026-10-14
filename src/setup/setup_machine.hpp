#pragma once
// #1849: how a Pi board with no network gets onto one, and paired, from a phone.
//
// THE FLOW (ADR-0083, turnaus #1849's "Decided"):
//
//   1. A board with no working network counts failed joins IN A FILE. At N = 5 it raises its
//      own access point, Turnaus-XXXX (the last four hex digits of its Wi-Fi MAC, upper case,
//      the same four characters as the sticker on the board).
//   2. The phone joins that access point from its own Wi-Fi settings.
//   3. GET /setup says which board this is and which networks it saw BEFORE the access point
//      came up (a Pi has one radio). POST /setup hands over {ssid, passphrase, pin} in one
//      request, and is answered 202 BEFORE the access point goes down.
//   4. The board drops the access point, joins that Wi-Fi through NetworkManager, and spends
//      the code with `opendartboard --pair <pin> --keep-device`.
//   5. Any failure throws the new Wi-Fi connection away and raises the access point again.
//      The phone learns the outcome from Turnaus (turnaus #1847), never from the board.
//
// WHY A FILE. Anything run under `Restart=` is restarted underneath a counter it keeps in
// memory, and a count that resets on every start never reaches N (ADR-0083 §4). So the count
// is read from and written to `join-attempts` in the StateDirectory on every look, and the
// look that writes N is the look that raises the access point -- never the start after it.
// The file also carries the kernel's boot id: a new boot starts the count again, so a board
// whose router was merely down when it rebooted does not raise an access point on its first
// failed look. "Within N failed joins of boot" is exact.
//
// WHAT THIS FILE IS. The decisions and nothing else. Everything that touches the radio,
// NetworkManager, the pair call, systemd or the clock is behind `Platform`, so the whole
// machine is driven by testers/i1849_machine_check.cpp with no radio at all; the real
// Platform is nmcli and a child process (setup_platform.hpp). The HTTP endpoint is two pure
// functions over the machine (get(), post()), so the server is a socket and nothing more.
//
// WHAT IT NEVER HOLDS. The passphrase lives in the pending request between post() and
// apply(), and is handed to NetworkManager and dropped. It is never written, never logged and
// never answered: GET /setup carries no credential, no token and no passphrase.

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace setup
{
    /** ADR-0083 §4's starting value: about ten seconds of looking at a two-second cadence. */
    constexpr int kFailedJoinsBeforeAccessPoint = 5;

    /** The leaf in the StateDirectory (/var/lib/opendartboard, #1660) the count is kept in. */
    constexpr const char *kCounterLeaf = "join-attempts";

    /**
     * The access point's password: the same on every board, a speed bump against a passing
     * phone auto-joining, and deliberately NOT the security boundary -- the six-digit code is
     * (ADR-0083 §7). It is in every .deb, so anybody can read it, and that is accepted.
     */
    constexpr const char *kAccessPointPassword = "turnaus-setup";

    /** NetworkManager's shared mode hands this out; the real Platform pins it explicitly. */
    constexpr const char *kAccessPointAddress = "10.42.0.1";

    /** One network a scan saw. */
    struct Network
    {
        std::string ssid;
        int signal = 0; // 0..100, NetworkManager's scale
        bool secured = false;
    };

    /** What one join came to. `connection` is named even when the join failed: nmcli leaves
     *  a profile behind on a wrong passphrase, and the machine must delete it. */
    struct Join
    {
        std::string connection; // "" when no profile was created at all
        bool up = false;
    };

    /**
     * The world, as the machine needs it. Every method is a question or an act; none decides.
     */
    class Platform
    {
    public:
        virtual ~Platform() = default;
        /** /proc/sys/kernel/random/boot_id: what makes "since boot" a fact a file can carry. */
        virtual std::string bootId() = 0;
        /** Is this board on a network that reaches anything (NetworkManager's connectivity)? */
        virtual bool online() = 0;
        /** Is Turnaus answering? Asked only while online and paired. */
        virtual bool turnausReachable() = 0;
        /** Does the board hold a club credential (credentials.json with a token)? */
        virtual bool paired() = 0;
        /** The networks in range. Called only while the radio is NOT an access point. */
        virtual std::vector<Network> scan() = 0;
        virtual bool raiseAccessPoint(const std::string &ssid, const std::string &password) = 0;
        virtual void dropAccessPoint() = 0;
        /** Create a NetworkManager connection for this network and bring it up. */
        virtual Join join(const std::string &ssid, const std::string &passphrase) = 0;
        /** `nmcli connection delete`: the connection a failed setup created, and only that. */
        virtual void forget(const std::string &connection) = 0;
        /** `opendartboard --pair <pin> --keep-device`; true on a credential kept. */
        virtual bool pair(const std::string &pin) = 0;
        virtual void restartDetector() = 0;
        /** sd_notify STATUS= (ADR-0083 §10). */
        virtual void status(const std::string &line) = 0;
    };

    // ---- the three sentences ADR-0083 §10 asks for, and the fourth that is not a fault ----

    constexpr const char *kStatusNeverProvisioned = "SETUP: never provisioned -- this board has no Turnaus credential";
    constexpr const char *kStatusNoNetwork = "SETUP: credentials present, network unreachable";
    constexpr const char *kStatusNoTurnaus = "SETUP: network up, Turnaus unreachable";
    constexpr const char *kStatusFine = "SETUP: joined and paired";

    /** "dc:a6:32:12:ab:cd" -> "Turnaus-ABCD"; "" when the address has fewer than 4 hex digits. */
    inline std::string boardName(const std::string &mac)
    {
        std::string hex;
        for (char c : mac)
        {
            if (std::isxdigit(static_cast<unsigned char>(c)))
            {
                hex += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            }
        }
        if (hex.size() < 4)
        {
            return "";
        }
        return "Turnaus-" + hex.substr(hex.size() - 4);
    }

    /** One visible network per name, strongest first; hidden (empty-named) networks dropped. */
    inline std::vector<Network> tidy(const std::vector<Network> &seen)
    {
        std::map<std::string, Network> best;
        for (const Network &n : seen)
        {
            if (n.ssid.empty())
            {
                continue;
            }
            auto it = best.find(n.ssid);
            if (it == best.end() || n.signal > it->second.signal)
            {
                best[n.ssid] = n;
            }
        }
        std::vector<Network> out;
        for (auto &kv : best)
        {
            out.push_back(kv.second);
        }
        std::stable_sort(out.begin(), out.end(), [](const Network &a, const Network &b)
                         { return a.signal > b.signal; });
        return out;
    }

    // ---- the counter, which is a file and never a member --------------------------------

    /** The failed joins counted since `boot`; 0 for a missing, unreadable or older-boot file. */
    inline int readFailedJoins(const std::string &path, const std::string &boot)
    {
        std::ifstream in(path);
        std::string kept_boot;
        int failed = 0;
        if (!(in >> kept_boot >> failed) || kept_boot != boot || failed < 0)
        {
            return 0;
        }
        return failed;
    }

    /** Written whole and renamed into place, so a power cut leaves the old count or the new. */
    inline bool writeFailedJoins(const std::string &path, const std::string &boot, int failed)
    {
        const std::string tmp = path + ".tmp";
        {
            std::ofstream out(tmp, std::ios::trunc);
            if (!(out << boot << ' ' << failed << '\n'))
            {
                return false;
            }
        }
        return std::rename(tmp.c_str(), path.c_str()) == 0;
    }

    /** A successful join clears the count by removing the file: nothing there is zero. */
    inline void clearFailedJoins(const std::string &path) { std::remove(path.c_str()); }

    // ---- the endpoint's two answers -----------------------------------------------------

    struct Reply
    {
        int status = 200;
        std::string body;
    };

    /** What POST /setup was handed, once it has been found well-formed. */
    struct Request
    {
        std::string ssid;
        std::string passphrase;
        std::string pin;
    };

    /**
     * Read POST /setup's body. Answers the field errors -- empty when the body is acceptable.
     * The shape is Laravel's `errors` object so the phone reads one refusal grammar, and the
     * values are machine codes (ADR-0008), never sentences.
     */
    inline std::map<std::string, std::string> readRequest(const std::string &body, Request &out)
    {
        std::map<std::string, std::string> errors;
        nlohmann::json j = nlohmann::json::parse(body, nullptr, false);
        if (j.is_discarded() || !j.is_object())
        {
            errors["body"] = "not_json_object";
            return errors;
        }
        auto text = [&](const char *key, std::string &into) -> bool
        {
            if (!j.contains(key))
            {
                errors[key] = "required";
                return false;
            }
            if (!j[key].is_string())
            {
                errors[key] = "not_string";
                return false;
            }
            into = j[key].get<std::string>();
            return true;
        };
        // An SSID is 1..32 octets (IEEE 802.11).
        if (text("ssid", out.ssid) && (out.ssid.empty() || out.ssid.size() > 32))
        {
            errors["ssid"] = "length";
        }
        // Empty is an open network; otherwise WPA-PSK's 8..63 characters.
        if (text("passphrase", out.passphrase) && !out.passphrase.empty() &&
            (out.passphrase.size() < 8 || out.passphrase.size() > 63))
        {
            errors["passphrase"] = "length";
        }
        if (text("pin", out.pin))
        {
            bool six_digits = out.pin.size() == 6;
            for (char c : out.pin)
            {
                six_digits = six_digits && c >= '0' && c <= '9';
            }
            if (!six_digits)
            {
                errors["pin"] = "six_digits";
            }
        }
        return errors;
    }

    // ---- the machine --------------------------------------------------------------------

    enum class Mode
    {
        Watching,    // looking at the network every tick, counting failures in the file
        AccessPoint, // Turnaus-XXXX is up and the endpoint takes a request
        Applying,    // a request was accepted (202) and apply() has not finished with it
    };

    class Machine
    {
    public:
        Machine(Platform &platform, std::string counter_path, std::string board, std::string version,
                int threshold = kFailedJoinsBeforeAccessPoint)
            : p_(platform), counter_path_(std::move(counter_path)), board_(std::move(board)),
              version_(std::move(version)), threshold_(threshold)
        {
        }

        Mode mode() const { return mode_; }
        const std::vector<Network> &networks() const { return networks_; }

        /**
         * One look at the network. The service calls it every two seconds while Watching; it
         * does nothing in the other two modes, because the radio is an access point then and
         * "not online" would be a count of our own doing.
         */
        void tick()
        {
            if (mode_ != Mode::Watching)
            {
                return;
            }
            const std::string boot = p_.bootId();
            if (p_.online())
            {
                clearFailedJoins(counter_path_);
                if (!p_.paired())
                {
                    p_.status(kStatusNeverProvisioned);
                }
                else
                {
                    p_.status(p_.turnausReachable() ? kStatusFine : kStatusNoTurnaus);
                }
                return;
            }
            const int failed = readFailedJoins(counter_path_, boot) + 1;
            writeFailedJoins(counter_path_, boot, failed);
            if (failed >= threshold_)
            {
                raise();
                return;
            }
            p_.status(p_.paired() ? kStatusNoNetwork : kStatusNeverProvisioned);
        }

        /** GET /setup. Never a credential, a token or a passphrase. */
        Reply get()
        {
            // Only while this board is being set up: on the club's network it says nothing
            // about itself to whoever asks.
            if (mode_ == Mode::Watching)
            {
                return {409, nlohmann::json({{"errors", {{"board", "not_in_setup"}}}}).dump()};
            }
            nlohmann::json j;
            j["board"] = board_;
            j["version"] = version_;
            j["paired"] = p_.paired();
            j["networks"] = nlohmann::json::array();
            for (const Network &n : networks_)
            {
                j["networks"].push_back({{"ssid", n.ssid}, {"signal", n.signal}, {"secured", n.secured}});
            }
            return {200, j.dump()};
        }

        /**
         * POST /setup. A malformed body is 422 and changes nothing. A good one is kept as the
         * pending request and answered 202 -- the caller sends that answer and only then calls
         * apply(), which is what drops the access point the answer travels over.
         */
        Reply post(const std::string &body)
        {
            if (mode_ != Mode::AccessPoint)
            {
                return {409, nlohmann::json({{"errors", {{"board", "not_in_setup"}}}}).dump()};
            }
            Request r;
            const std::map<std::string, std::string> errors = readRequest(body, r);
            if (!errors.empty())
            {
                nlohmann::json e = nlohmann::json::object();
                for (auto &kv : errors)
                {
                    e[kv.first] = nlohmann::json::array({kv.second});
                }
                return {422, nlohmann::json({{"errors", e}}).dump()};
            }
            pending_ = r;
            mode_ = Mode::Applying;
            return {202, nlohmann::json({{"board", board_}, {"accepted", true}}).dump()};
        }

        /**
         * Carry out the request post() accepted. Success: joined, paired (or re-paired, same
         * club -- the pair call's own refusal otherwise), the count cleared, the detector
         * restarted. Every failure: the new connection deleted, the access point raised again,
         * and the credential left exactly as it was (only a successful pair call writes it).
         */
        void apply()
        {
            if (mode_ != Mode::Applying)
            {
                return;
            }
            Request r = pending_;
            pending_ = Request{};
            p_.dropAccessPoint();
            const Join joined = p_.join(r.ssid, r.passphrase);
            r.passphrase.assign(r.passphrase.size(), '\0');
            if (!joined.up)
            {
                restore(joined.connection);
                return;
            }
            if (!p_.pair(r.pin))
            {
                restore(joined.connection);
                return;
            }
            clearFailedJoins(counter_path_);
            p_.restartDetector();
            mode_ = Mode::Watching;
            p_.status(kStatusFine);
        }

    private:
        /** Scan while the radio is still ours, then raise. Stays Watching if the raise failed. */
        void raise()
        {
            networks_ = tidy(p_.scan());
            if (!p_.raiseAccessPoint(board_, kAccessPointPassword))
            {
                p_.status(p_.paired() ? kStatusNoNetwork : kStatusNeverProvisioned);
                return;
            }
            mode_ = Mode::AccessPoint;
            p_.status(std::string(p_.paired() ? kStatusNoNetwork : kStatusNeverProvisioned) + " -- access point " +
                      board_ + " is up at " + kAccessPointAddress);
        }

        void restore(const std::string &connection)
        {
            if (!connection.empty())
            {
                p_.forget(connection);
            }
            mode_ = Mode::Watching;
            raise();
        }

        Platform &p_;
        std::string counter_path_;
        std::string board_;
        std::string version_;
        int threshold_;
        Mode mode_ = Mode::Watching;
        std::vector<Network> networks_;
        Request pending_;
    };
}
