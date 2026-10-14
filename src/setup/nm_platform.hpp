#pragma once
// #1849: the real setup::Platform -- NetworkManager through nmcli, the pair call through the
// detector binary, systemd through systemctl and sd_notify.
//
// NOT MEASURED ON A PI. The fork's build image carries no NetworkManager, so nothing here has
// run against a radio; testers/i1849_machine_check.cpp drives the decisions with a fake, and
// this file is the part a real first boot (docs/rig.md, the maintainer's at the board) is
// the test of. Every command is written out below so that a reader can run it by hand.
//
// Every child is fork+exec with an argv, never a shell, so nothing a phone sends is ever
// parsed by one. The passphrase does reach nmcli's argv, where root on the board can read it
// in /proc for the second the command runs; the board has no other interactive accounts and
// NetworkManager keeps the same passphrase in a root-only file afterwards (ADR-0083 §6).

#include "setup_machine.hpp"

#include "communication/turnaus_address.hpp"
#include "od_notify.hpp"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <sstream>
#include <string>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace setup
{
    /** Run argv, capture stdout, answer the exit status (-1 when it could not be started). */
    inline int run(const std::vector<std::string> &argv, std::string *out = nullptr)
    {
        int pipefd[2];
        if (::pipe(pipefd) != 0)
        {
            return -1;
        }
        const pid_t pid = ::fork();
        if (pid < 0)
        {
            ::close(pipefd[0]);
            ::close(pipefd[1]);
            return -1;
        }
        if (pid == 0)
        {
            ::dup2(pipefd[1], STDOUT_FILENO);
            ::close(pipefd[0]);
            ::close(pipefd[1]);
            std::vector<char *> args;
            for (const std::string &a : argv)
            {
                args.push_back(const_cast<char *>(a.c_str()));
            }
            args.push_back(nullptr);
            ::execvp(args[0], args.data());
            ::_exit(127);
        }
        ::close(pipefd[1]);
        std::string captured;
        char buf[4096];
        ssize_t n;
        while ((n = ::read(pipefd[0], buf, sizeof buf)) > 0)
        {
            captured.append(buf, static_cast<size_t>(n));
        }
        ::close(pipefd[0]);
        int st = 0;
        ::waitpid(pid, &st, 0);
        if (out != nullptr)
        {
            *out = captured;
        }
        return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
    }

    /** Split one line of `nmcli -t` output on unescaped colons, unescaping `\:` and `\\`. */
    inline std::vector<std::string> terseFields(const std::string &line)
    {
        std::vector<std::string> fields(1);
        for (size_t i = 0; i < line.size(); i++)
        {
            if (line[i] == '\\' && i + 1 < line.size())
            {
                fields.back() += line[++i];
            }
            else if (line[i] == ':')
            {
                fields.emplace_back();
            }
            else
            {
                fields.back() += line[i];
            }
        }
        return fields;
    }

    /** `nmcli -t -f SSID,SIGNAL,SECURITY device wifi list` -> networks. */
    inline std::vector<Network> parseWifiList(const std::string &terse)
    {
        std::vector<Network> out;
        std::istringstream in(terse);
        std::string line;
        while (std::getline(in, line))
        {
            const std::vector<std::string> f = terseFields(line);
            if (f.size() < 3)
            {
                continue;
            }
            Network n;
            n.ssid = f[0];
            n.signal = std::atoi(f[1].c_str());
            n.secured = !f[2].empty() && f[2] != "--";
            out.push_back(n);
        }
        return out;
    }

    struct NmConfig
    {
        std::string interface = "wlan0";
        std::string state_dir;                                  // $STATE_DIRECTORY
        std::string detector = "/usr/local/bin/opendartboard";  // what --pair is asked of
        std::string detector_unit = "opendartboard.service";
        std::string access_point_connection = "turnaus-access-point";
    };

    class NmPlatform : public Platform
    {
    public:
        explicit NmPlatform(NmConfig c) : c_(std::move(c)) {}

        std::string bootId() override
        {
            std::ifstream in("/proc/sys/kernel/random/boot_id");
            std::string id;
            in >> id;
            return id.empty() ? "unknown-boot" : id;
        }

        /** The Wi-Fi MAC, for the board's name. */
        std::string mac() const
        {
            std::ifstream in("/sys/class/net/" + c_.interface + "/address");
            std::string a;
            in >> a;
            return a;
        }

        bool online() override
        {
            // STATE is `connected` with a route out, `connected (site only)` without one.
            std::string out;
            if (run({"nmcli", "-t", "-f", "STATE", "general"}, &out) != 0)
            {
                return false;
            }
            return out.rfind("connected", 0) == 0 && out.find("site only") == std::string::npos;
        }

        bool turnausReachable() override
        {
            // Asked at most every thirty seconds; between asks the last answer stands.
            const auto now = std::chrono::steady_clock::now();
            if (asked_ && now - last_ask_ < std::chrono::seconds(30))
            {
                return reachable_;
            }
            asked_ = true;
            last_ask_ = now;
            std::string base = credentialField("base_url");
            if (base.empty())
            {
                base = turnaus_address::kDefault;
            }
            // Any HTTP answer at all is "reachable"; this is not a health check of Turnaus.
            // An https:// address needs this target built with CPPHTTPLIB_OPENSSL_SUPPORT.
            httplib::Client client(base);
            client.set_connection_timeout(3, 0);
            client.set_read_timeout(5, 0);
            auto res = client.Get("/up");
            reachable_ = static_cast<bool>(res);
            return reachable_;
        }

        bool paired() override { return !credentialField("token").empty(); }

        std::vector<Network> scan() override
        {
            std::string out;
            run({"nmcli", "-t", "-f", "SSID,SIGNAL,SECURITY", "device", "wifi", "list", "ifname", c_.interface,
                 "--rescan", "yes"},
                &out);
            return parseWifiList(out);
        }

        bool raiseAccessPoint(const std::string &ssid, const std::string &password) override
        {
            run({"nmcli", "connection", "delete", c_.access_point_connection});
            // Shared mode is NetworkManager's DHCP and NAT; the address is pinned rather than
            // left to its default so the documented 10.42.0.1 is ours to promise.
            if (run({"nmcli", "connection", "add", "type", "wifi", "ifname", c_.interface, "con-name",
                     c_.access_point_connection, "autoconnect", "no", "ssid", ssid, "802-11-wireless.mode", "ap",
                     "802-11-wireless.band", "bg", "ipv4.method", "shared", "ipv4.addresses",
                     std::string(kAccessPointAddress) + "/24", "wifi-sec.key-mgmt", "wpa-psk", "wifi-sec.psk",
                     password}) != 0)
            {
                return false;
            }
            return run({"nmcli", "connection", "up", c_.access_point_connection}) == 0;
        }

        void dropAccessPoint() override
        {
            run({"nmcli", "connection", "down", c_.access_point_connection});
            run({"nmcli", "connection", "delete", c_.access_point_connection});
        }

        Join join(const std::string &ssid, const std::string &passphrase) override
        {
            Join j;
            const std::string name = "turnaus-" + std::to_string(static_cast<long long>(std::time(nullptr)));
            std::vector<std::string> add = {"nmcli", "connection", "add", "type", "wifi", "ifname", c_.interface,
                                            "con-name", name, "ssid", ssid};
            if (!passphrase.empty())
            {
                add.insert(add.end(), {"wifi-sec.key-mgmt", "wpa-psk", "wifi-sec.psk", passphrase});
            }
            if (run(add) != 0)
            {
                return j;
            }
            j.connection = name;
            if (run({"nmcli", "--wait", "45", "connection", "up", name}) != 0)
            {
                return j;
            }
            for (int i = 0; i < 20 && !online(); i++)
            {
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
            j.up = online();
            return j;
        }

        void forget(const std::string &connection) override { run({"nmcli", "connection", "delete", connection}); }

        bool pair(const std::string &pin) override
        {
            // The child inherits $STATE_DIRECTORY, so it writes the credential where the
            // detector reads it (#1660).
            return run({c_.detector, "--pair", pin, "--keep-device"}) == 0;
        }

        void restartDetector() override { run({"systemctl", "restart", "--no-block", c_.detector_unit}); }

        void status(const std::string &line) override
        {
            if (line != last_status_)
            {
                std::printf("%s\n", line.c_str());
                std::fflush(stdout);
                last_status_ = line;
            }
            od_notify::send("STATUS=" + line);
        }

    private:
        std::string credentialField(const std::string &key) const
        {
            std::ifstream in(c_.state_dir + "/credentials.json");
            if (!in)
            {
                return "";
            }
            nlohmann::json j = nlohmann::json::parse(in, nullptr, false);
            if (j.is_discarded() || !j.is_object() || !j.contains(key) || !j[key].is_string())
            {
                return "";
            }
            return j[key].get<std::string>();
        }

        NmConfig c_;
        bool asked_ = false;
        bool reachable_ = false;
        std::chrono::steady_clock::time_point last_ask_;
        std::string last_status_;
    };
}
