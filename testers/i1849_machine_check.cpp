// #1849: the setup machine (src/setup/setup_machine.hpp) driven through every path the
// issue's checklist names, with NetworkManager, the radio and the pair call faked. No radio,
// no network, no detector: the fake records every act in order and the checks read that log.
//
// Prints one OK or FAIL line per assertion and exits on the count of FAILs (#1335). Compiled
// and run by testers/i1849_check.sh.
//
// The counter is a REAL file in a scratch directory, never a fake: the property under test
// is that a count survives the process it was made in, so case `restarts` builds a new
// Machine for every look, as Restart= would. A machine that counted in a member passes every
// other case here and fails that one.

#include "setup/nm_platform.hpp"
#include "setup/setup_machine.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace
{
    int failures = 0;

    void check(bool ok, const std::string &what)
    {
        std::printf("%s %s\n", ok ? "OK  " : "FAIL", what.c_str());
        if (!ok)
        {
            failures++;
        }
    }

    bool contains(const std::string &hay, const std::string &needle) { return hay.find(needle) != std::string::npos; }

    bool exists(const std::string &path)
    {
        struct stat st;
        return ::stat(path.c_str(), &st) == 0;
    }

    /** The world, recorded. Every act appends one line to `acts`. */
    struct FakePlatform : setup::Platform
    {
        std::string boot = "boot-A";
        bool is_online = false;
        bool turnaus_up = true;
        std::string credential;              // "" is unpaired; what pair() writes on success
        std::string pair_answers_token = "tok-NEW";
        bool pair_succeeds = true;           // false: refused, expired or another club
        bool join_succeeds = true;           // false: wrong Wi-Fi password
        bool raise_succeeds = true;
        bool ap_up = false;
        std::vector<setup::Network> in_range = {{"Pub", 40, true}, {"Pub", 70, true}, {"", 90, true}, {"Guest", 55, false}};
        std::vector<std::string> acts;
        std::vector<std::string> statuses;
        std::vector<std::string> connections; // what NetworkManager holds
        int joins = 0;

        std::string bootId() override { return boot; }
        bool online() override { return is_online; }
        bool turnausReachable() override { return turnaus_up; }
        bool paired() override { return !credential.empty(); }
        std::vector<setup::Network> scan() override
        {
            acts.push_back("scan");
            // One radio: a scan while the access point is up sees nothing useful.
            return ap_up ? std::vector<setup::Network>{} : in_range;
        }
        bool raiseAccessPoint(const std::string &ssid, const std::string &password) override
        {
            acts.push_back("raise " + ssid + " " + password);
            ap_up = raise_succeeds;
            return raise_succeeds;
        }
        void dropAccessPoint() override
        {
            acts.push_back("drop");
            ap_up = false;
        }
        setup::Join join(const std::string &ssid, const std::string &passphrase) override
        {
            acts.push_back("join " + ssid);
            (void)passphrase;
            const std::string name = "turnaus-" + std::to_string(++joins);
            connections.push_back(name); // nmcli keeps the profile even when activation fails
            setup::Join j;
            j.connection = name;
            j.up = join_succeeds;
            return j;
        }
        void forget(const std::string &connection) override
        {
            acts.push_back("forget " + connection);
            std::vector<std::string> kept;
            for (auto &c : connections)
            {
                if (c != connection)
                {
                    kept.push_back(c);
                }
            }
            connections = kept;
        }
        bool pair(const std::string &pin) override
        {
            acts.push_back("pair " + pin);
            if (pair_succeeds)
            {
                credential = pair_answers_token;
            }
            return pair_succeeds;
        }
        void restartDetector() override { acts.push_back("restart-detector"); }
        void status(const std::string &line) override { statuses.push_back(line); }

        int count(const std::string &prefix) const
        {
            int n = 0;
            for (auto &a : acts)
            {
                n += a.rfind(prefix, 0) == 0 ? 1 : 0;
            }
            return n;
        }
        int indexOf(const std::string &prefix) const
        {
            for (size_t i = 0; i < acts.size(); i++)
            {
                if (acts[i].rfind(prefix, 0) == 0)
                {
                    return static_cast<int>(i);
                }
            }
            return -1;
        }
    };

    std::string scratch;
    int scratch_n = 0;

    std::string freshCounter()
    {
        const std::string dir = scratch + "/" + std::to_string(++scratch_n);
        ::mkdir(dir.c_str(), 0700);
        return dir + "/" + setup::kCounterLeaf;
    }

    const char *kBoard = "Turnaus-ABCD";
    const char *kGood = R"({"ssid":"Pub","passphrase":"hunter2-pub","pin":"123456"})";

    /** A machine already standing with its access point up, as the issue's paths begin. */
    void raiseOn(setup::Machine &m, FakePlatform &p)
    {
        for (int i = 0; i < setup::kFailedJoinsBeforeAccessPoint; i++)
        {
            m.tick();
        }
        (void)p;
    }

    void caseThreshold()
    {
        FakePlatform p;
        const std::string counter = freshCounter();
        setup::Machine m(p, counter, kBoard, "0.0.0-dev");
        for (int i = 1; i < setup::kFailedJoinsBeforeAccessPoint; i++)
        {
            m.tick();
        }
        check(p.count("raise") == 0 && m.mode() == setup::Mode::Watching,
              "threshold: four failed looks raise nothing");
        check(setup::readFailedJoins(counter, "boot-A") == 4, "threshold: the file says 4 after four failed looks");
        m.tick();
        check(m.mode() == setup::Mode::AccessPoint, "threshold: the fifth failed look raises the access point");
        check(p.count("raise Turnaus-ABCD turnaus-setup") == 1, "threshold: raised as Turnaus-ABCD with the shared password");
        check(p.indexOf("scan") >= 0 && p.indexOf("scan") < p.indexOf("raise"),
              "threshold: the networks are scanned BEFORE the access point comes up");
        m.tick();
        check(p.count("raise") == 1 && setup::readFailedJoins(counter, "boot-A") == 5,
              "threshold: a look while the access point is up counts nothing and raises nothing again");
    }

    void caseRestarts()
    {
        // Restart= under every look: a new process, so a new Machine, each time.
        FakePlatform p;
        const std::string counter = freshCounter();
        bool raised_on_fifth = false;
        for (int i = 1; i <= setup::kFailedJoinsBeforeAccessPoint; i++)
        {
            setup::Machine m(p, counter, kBoard, "0.0.0-dev");
            m.tick();
            if (i < setup::kFailedJoinsBeforeAccessPoint && p.count("raise") != 0)
            {
                break;
            }
            raised_on_fifth = i == setup::kFailedJoinsBeforeAccessPoint && m.mode() == setup::Mode::AccessPoint;
        }
        check(raised_on_fifth, "restarts: five processes of one look each raise the access point on the fifth "
                               "(the count is the file's, not the process's)");
    }

    void caseBoot()
    {
        FakePlatform p;
        const std::string counter = freshCounter();
        setup::writeFailedJoins(counter, "boot-OLD", 4);
        setup::Machine m(p, counter, kBoard, "0.0.0-dev");
        m.tick();
        check(m.mode() == setup::Mode::Watching && setup::readFailedJoins(counter, "boot-A") == 1,
              "boot: a count from another boot is not carried over (4 then a new boot is 1, no access point)");
    }

    void caseOnlineClears()
    {
        FakePlatform p;
        const std::string counter = freshCounter();
        setup::Machine m(p, counter, kBoard, "0.0.0-dev");
        m.tick();
        m.tick();
        m.tick();
        p.is_online = true;
        m.tick();
        check(!exists(counter), "online: a successful join removes the count file");
        p.is_online = false;
        for (int i = 0; i < 4; i++)
        {
            m.tick();
        }
        check(m.mode() == setup::Mode::Watching, "online: the count starts again from zero after it (4 more raise nothing)");
    }

    void caseStatus()
    {
        {
            FakePlatform p;
            setup::Machine m(p, freshCounter(), kBoard, "0.0.0-dev");
            m.tick();
            check(!p.statuses.empty() && p.statuses.back() == setup::kStatusNeverProvisioned,
                  "status: unpaired and offline says `never provisioned`");
        }
        {
            FakePlatform p;
            p.credential = "tok-OLD";
            setup::Machine m(p, freshCounter(), kBoard, "0.0.0-dev");
            m.tick();
            check(!p.statuses.empty() && p.statuses.back() == setup::kStatusNoNetwork,
                  "status: paired and offline says `credentials present, network unreachable`");
            raiseOn(m, p);
            check(contains(p.statuses.back(), setup::kStatusNoNetwork) && contains(p.statuses.back(), "Turnaus-ABCD") &&
                      contains(p.statuses.back(), "10.42.0.1"),
                  "status: with the access point up it says so, by name and address");
        }
        {
            FakePlatform p;
            p.credential = "tok-OLD";
            p.is_online = true;
            p.turnaus_up = false;
            setup::Machine m(p, freshCounter(), kBoard, "0.0.0-dev");
            m.tick();
            check(!p.statuses.empty() && p.statuses.back() == setup::kStatusNoTurnaus,
                  "status: paired, online, Turnaus not answering says `network up, Turnaus unreachable`");
            p.turnaus_up = true;
            m.tick();
            check(p.statuses.back() == setup::kStatusFine, "status: all three well says `joined and paired`");
        }
    }

    void caseGet()
    {
        FakePlatform p;
        p.credential = "tok-SECRET-OLD";
        setup::Machine m(p, freshCounter(), kBoard, "1.2.3");
        raiseOn(m, p);
        setup::Reply r = m.get();
        nlohmann::json j = nlohmann::json::parse(r.body, nullptr, false);
        check(r.status == 200 && j.is_object(), "get: 200 with a JSON object");
        check(j.value("board", "") == "Turnaus-ABCD" && j.value("version", "") == "1.2.3" && j.value("paired", false),
              "get: board, version and paired say what they should");
        const bool shaped = j.contains("networks") && j["networks"].is_array() && j["networks"].size() == 2 &&
                            j["networks"][0].value("ssid", "") == "Pub" && j["networks"][0].value("signal", 0) == 70 &&
                            j["networks"][0].value("secured", false) == true &&
                            j["networks"][1].value("ssid", "") == "Guest" &&
                            j["networks"][1].value("secured", true) == false;
        check(shaped, "get: networks are the pre-access-point scan, one per name, strongest first, hidden dropped");
        check(j.size() == 4, "get: exactly four keys (board, version, paired, networks)");
        m.post(kGood);
        r = m.get();
        check(!contains(r.body, "tok-SECRET") && !contains(r.body, "hunter2") && !contains(r.body, "123456"),
              "get: carries no token, no passphrase and no pin, even with a request pending");
    }

    void caseMalformed()
    {
        const char *bodies[] = {
            "not json",
            "[]",
            R"({"ssid":"Pub","passphrase":"hunter2-pub"})",                 // no pin
            R"({"ssid":"Pub","passphrase":"hunter2-pub","pin":"12345"})",   // five digits
            R"({"ssid":"Pub","passphrase":"hunter2-pub","pin":"12345a"})",  // not digits
            R"({"ssid":"Pub","passphrase":"hunter2-pub","pin":123456})",    // a number, not a string
            R"({"ssid":"","passphrase":"hunter2-pub","pin":"123456"})",     // empty ssid
            R"({"ssid":"Pub","passphrase":"short","pin":"123456"})",        // WPA wants 8..63
            R"({"passphrase":"hunter2-pub","pin":"123456"})",               // no ssid
        };
        bool all422 = true, nothing_changed = true;
        for (const char *b : bodies)
        {
            FakePlatform p;
            setup::Machine m(p, freshCounter(), kBoard, "0.0.0-dev");
            raiseOn(m, p);
            const size_t acts_before = p.acts.size();
            setup::Reply r = m.post(b);
            m.apply();
            all422 = all422 && r.status == 422 && contains(r.body, "\"errors\"");
            nothing_changed = nothing_changed && m.mode() == setup::Mode::AccessPoint && p.acts.size() == acts_before;
            if (r.status != 422)
            {
                std::printf("     body %s answered %d\n", b, r.status);
            }
        }
        check(all422, "malformed: every malformed body is 422 with an errors object");
        check(nothing_changed, "malformed: and changes nothing -- the access point stays, no act is taken");
        {
            FakePlatform p;
            setup::Machine m(p, freshCounter(), kBoard, "0.0.0-dev");
            setup::Reply r = m.post(R"({"ssid":"Pub","passphrase":"hunter2-pub","pin":"123456"})");
            check(r.status == 409 && p.acts.empty(), "malformed: a request while no access point is up is 409 and acts on nothing");
            check(m.get().status == 409, "malformed: and GET /setup is 409 too -- a board on its network says nothing");
        }
        {
            FakePlatform p;
            setup::Machine m(p, freshCounter(), kBoard, "0.0.0-dev");
            raiseOn(m, p);
            setup::Reply r = m.post(R"({"ssid":"Guest","passphrase":"","pin":"000001"})");
            check(r.status == 202, "malformed: an open network (empty passphrase) is accepted");
        }
    }

    void caseSuccess(bool rejoin)
    {
        const std::string tag = rejoin ? "rejoin" : "success";
        FakePlatform p;
        if (rejoin)
        {
            p.credential = "tok-OLD";
        }
        const std::string counter = freshCounter();
        setup::Machine m(p, counter, kBoard, "0.0.0-dev");
        raiseOn(m, p);
        setup::Reply r = m.post(kGood);
        check(r.status == 202 && p.count("drop") == 0 && p.ap_up,
              tag + ": 202 is answered while the access point is still up");
        m.apply();
        const int drop = p.indexOf("drop"), join = p.indexOf("join Pub"), pair = p.indexOf("pair 123456"),
                  restart = p.indexOf("restart-detector");
        check(drop >= 0 && drop < join && join < pair && pair < restart,
              tag + ": drop, join, pair, restart the detector -- in that order");
        check(p.credential == "tok-NEW", tag + (rejoin ? ": the rotated credential is kept" : ": paired"));
        check(!exists(counter), tag + ": the count is cleared");
        check(p.count("forget") == 0 && p.connections.size() == 1, tag + ": the new connection is kept");
        check(m.mode() == setup::Mode::Watching && !p.ap_up, tag + ": the access point stays down");
        check(p.statuses.back() == setup::kStatusFine, tag + ": status says joined and paired");
    }

    /** Every failure: the connection deleted, the access point back, the credential as it was. */
    void caseFailure(const std::string &tag, bool join_ok, bool pair_ok, bool already_paired)
    {
        FakePlatform p;
        p.join_succeeds = join_ok;
        p.pair_succeeds = pair_ok;
        if (already_paired)
        {
            p.credential = "tok-OLD";
        }
        const std::string before = p.credential;
        const std::string counter = freshCounter();
        setup::Machine m(p, counter, kBoard, "0.0.0-dev");
        raiseOn(m, p);
        check(m.post(kGood).status == 202, tag + ": accepted (202)");
        m.apply();
        check(p.connections.empty() && p.count("forget turnaus-1") == 1,
              tag + ": the new Wi-Fi connection is deleted");
        check(m.mode() == setup::Mode::AccessPoint && p.ap_up && p.count("raise Turnaus-ABCD") == 2,
              tag + ": the access point is raised again");
        const size_t n = p.acts.size();
        check(n >= 3 && p.acts[n - 3].rfind("forget", 0) == 0 && p.acts[n - 2] == "scan" &&
                  p.acts[n - 1].rfind("raise", 0) == 0,
              tag + ": forget, a fresh scan (the radio is free), then raise -- in that order");
        check(p.credential == before, tag + ": the credential is untouched");
        check(p.count("restart-detector") == 0, tag + ": the detector is not restarted");
        check(exists(counter), tag + ": the count is not cleared");
        check(join_ok == (p.count("pair") == 1), tag + (join_ok ? ": the code was presented" : ": the code was NOT presented"));
        // And the same board can be set up straight away.
        p.join_succeeds = true;
        p.pair_succeeds = true;
        check(m.post(kGood).status == 202, tag + ": the endpoint takes the next request");
        m.apply();
        check(p.credential == "tok-NEW" && m.mode() == setup::Mode::Watching && p.connections.size() == 1,
              tag + ": and the next good request succeeds");
    }

    void casePure()
    {
        check(setup::boardName("dc:a6:32:12:ab:cd") == "Turnaus-ABCD", "pure: boardName takes the last four hex digits, upper case");
        check(setup::boardName("DC-A6-32-12-0B-0D") == "Turnaus-0B0D", "pure: boardName reads dashes too");
        check(setup::boardName("ab") == "", "pure: boardName refuses an address with fewer than four hex digits");
        // nmcli -t escapes a colon in a field as \: -- an SSID may hold one.
        const std::vector<setup::Network> seen =
            setup::parseWifiList("Pub\\:2:70:WPA2\n:90:WPA2\nGuest:55:\nOpen Bar:31:--\n");
        check(seen.size() == 4 && seen[0].ssid == "Pub:2" && seen[0].signal == 70 && seen[0].secured,
              "pure: parseWifiList reads an escaped colon inside an SSID");
        check(seen.size() == 4 && !seen[2].secured && !seen[3].secured && seen[3].ssid == "Open Bar",
              "pure: an empty or `--` SECURITY is an open network");
        check(setup::tidy(seen).size() == 3, "pure: tidy drops the hidden network");
    }

    void caseRaiseFails()
    {
        FakePlatform p;
        p.raise_succeeds = false;
        const std::string counter = freshCounter();
        setup::Machine m(p, counter, kBoard, "0.0.0-dev");
        raiseOn(m, p);
        check(m.mode() == setup::Mode::Watching, "raise-fails: a refused raise leaves the machine Watching");
        p.raise_succeeds = true;
        m.tick();
        check(m.mode() == setup::Mode::AccessPoint, "raise-fails: and the next failed look raises it (the count is still N)");
    }
}

int main()
{
    char tmpl[] = "/tmp/i1849-machine-XXXXXX";
    const char *dir = ::mkdtemp(tmpl);
    if (dir == nullptr)
    {
        std::printf("FAIL could not make a scratch directory\n");
        return 2;
    }
    scratch = dir;

    casePure();
    caseThreshold();
    caseRestarts();
    caseBoot();
    caseOnlineClears();
    caseRaiseFails();
    caseStatus();
    caseGet();
    caseMalformed();
    caseSuccess(false);
    caseSuccess(true);
    caseFailure("wrong-wifi-password", false, true, true);
    caseFailure("refused-code", true, false, false);
    caseFailure("expired-code", true, false, true);
    caseFailure("other-organisation", true, false, true);

    std::printf("I1849 machine failures=%d\n", failures);
    return failures == 0 ? 0 : 1;
}
