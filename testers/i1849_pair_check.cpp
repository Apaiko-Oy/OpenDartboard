// #1849: `--pair --keep-device`, the re-join half of the pair path, against an in-process
// stand-in for POST /api/v1/autoscorer/devices (turnaus #1848's contract).
//
// The real TurnausClient and the real HTTP transport; the server is cpp-httplib on loopback,
// recording the Authorization header it was sent and answering what the case asks. Prints one
// OK or FAIL line per assertion and exits on the count (#1335). Compiled and run by
// testers/i1849_check.sh.
//
//   fresh       keep_device, no credential: no Authorization header; the credential is written
//   rejoin      keep_device, club 7's credential: `Bearer tok-OLD` is presented; 201 for club 7
//               with a rotated token is written over it
//   refused     keep_device, 422: the file is byte-for-byte what it was
//   other-club  keep_device, 201 naming club 9: pair() is false and the file is untouched
//   plain       no keep_device, a credential present: no Authorization header (today's --pair)

#include "communication/turnaus_client.hpp"
#include "httplib.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <unistd.h>

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

    std::string slurp(const std::string &path)
    {
        std::ifstream in(path, std::ios::binary);
        std::stringstream s;
        s << in.rdbuf();
        return s.str();
    }

    void put(const std::string &path, const std::string &body)
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << body;
    }

    struct Stub
    {
        std::mutex m;
        int status = 201;
        long long organisation = 7;
        std::string token = "tok-ROTATED";
        std::string authorization; // what the last request carried, "<none>" when absent
        int requests = 0;
    };

    const char *kOld = R"({"base_url":"http://127.0.0.1","device_id":3,"label":"pi","organisation_id":7,"token":"tok-OLD"})";
}

int main()
{
    char tmpl[] = "/tmp/i1849-pair-XXXXXX";
    const char *dir = ::mkdtemp(tmpl);
    if (dir == nullptr)
    {
        std::printf("FAIL could not make a scratch directory\n");
        return 2;
    }
    ::setenv("STATE_DIRECTORY", dir, 1); // configDir() reads it first (#1660)
    const std::string cred = std::string(dir) + "/credentials.json";

    Stub stub;
    httplib::Server server;
    server.Post("/api/v1/autoscorer/devices", [&](const httplib::Request &req, httplib::Response &res)
                {
                    std::lock_guard<std::mutex> lock(stub.m);
                    stub.requests++;
                    stub.authorization = req.has_header("Authorization") ? req.get_header_value("Authorization") : "<none>";
                    res.status = stub.status;
                    if (stub.status == 201)
                    {
                        res.set_content("{\"data\":{\"token\":\"" + stub.token + "\",\"organisationId\":" +
                                            std::to_string(stub.organisation) +
                                            ",\"device\":{\"id\":3,\"label\":\"pi\"}}}",
                                        "application/json");
                    }
                    else
                    {
                        res.set_content("{\"errors\":{\"pin\":[\"invalid\"]}}", "application/json");
                    } });
    const int port = server.bind_to_any_port("127.0.0.1");
    std::thread serving([&]
                        { server.listen_after_bind(); });

    auto client = [&](bool keep)
    {
        TurnausConfig c;
        c.base_url = "http://127.0.0.1:" + std::to_string(port);
        c.credentials_path = cred;
        c.allow_plaintext = true;
        c.label = "pi";
        c.keep_device = keep;
        return c;
    };
    auto set = [&](int status, long long org, const std::string &token)
    {
        std::lock_guard<std::mutex> lock(stub.m);
        stub.status = status;
        stub.organisation = org;
        stub.token = token;
        stub.authorization.clear();
    };

    // fresh
    ::unlink(cred.c_str());
    set(201, 7, "tok-FIRST");
    {
        TurnausClient c(client(true));
        check(c.pair("123456"), "fresh: pair() is true");
    }
    check(stub.authorization == "<none>", "fresh: no Authorization header on a board with no credential");
    check(slurp(cred).find("tok-FIRST") != std::string::npos, "fresh: the credential is written");

    // rejoin
    put(cred, kOld);
    set(201, 7, "tok-ROTATED");
    {
        TurnausClient c(client(true));
        check(c.pair("123456"), "rejoin: pair() is true");
    }
    check(stub.authorization == "Bearer tok-OLD", "rejoin: the current token is presented as Bearer tok-OLD");
    const std::string after = slurp(cred);
    check(after.find("tok-ROTATED") != std::string::npos && after.find("tok-OLD") == std::string::npos,
          "rejoin: the rotated token replaces the old one");

    // refused
    put(cred, kOld);
    set(422, 7, "");
    {
        TurnausClient c(client(true));
        check(!c.pair("123456"), "refused: pair() is false on 422");
    }
    check(stub.authorization == "Bearer tok-OLD", "refused: the current token was presented");
    check(slurp(cred) == kOld, "refused: the credential file is byte-for-byte what it was");

    // other-club
    put(cred, kOld);
    set(201, 9, "tok-OTHER-CLUB");
    {
        TurnausClient c(client(true));
        check(!c.pair("123456"), "other-club: pair() is false when the answer names club 9 and the file says 7");
    }
    check(slurp(cred) == kOld, "other-club: the credential file is byte-for-byte what it was");

    // plain
    put(cred, kOld);
    set(201, 9, "tok-PLAIN");
    {
        TurnausClient c(client(false));
        check(c.pair("123456"), "plain: without --keep-device pair() is today's (true, another club allowed)");
    }
    check(stub.authorization == "<none>", "plain: without --keep-device no Authorization header is sent");

    server.stop();
    serving.join();
    std::printf("I1849 pair failures=%d\n", failures);
    return failures == 0 ? 0 : 1;
}
