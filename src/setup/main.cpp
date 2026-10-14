// #1849: opendartboard-setup -- a Pi board's way onto a network and into a club, from a phone.
//
// A second binary, not part of the detector (ADR-0083 §4: the detector neither joins
// networks nor exits for want of one). It looks at the network every two seconds, counts
// failed looks in $STATE_DIRECTORY/join-attempts, and at five raises Turnaus-XXXX and serves
// GET and POST /setup on port 80. setup_machine.hpp holds every decision; this file is the
// loop, the socket and the lock between them.
//
//   opendartboard-setup [--interface wlan0] [--port 80]
//
// Run by opendartboard-setup.service (StateDirectory=opendartboard, the detector's own
// directory, #1660), as root because NetworkManager's system connections and port 80 are.

#include "nm_platform.hpp"
#include "setup_machine.hpp"

#include <httplib.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>

#ifndef APP_VERSION
#define APP_VERSION "0.0.0-dev"
#endif

namespace
{
    std::string arg(int argc, char **argv, const char *name, const std::string &fallback)
    {
        for (int i = 1; i + 1 < argc; i++)
        {
            if (std::strcmp(argv[i], name) == 0)
            {
                return argv[i + 1];
            }
        }
        return fallback;
    }
}

int main(int argc, char **argv)
{
    const char *state = std::getenv("STATE_DIRECTORY");
    if (state == nullptr || *state == '\0')
    {
        std::fprintf(stderr, "opendartboard-setup: no $STATE_DIRECTORY; run it from opendartboard-setup.service "
                             "(StateDirectory=opendartboard)\n");
        return 2;
    }
    setup::NmConfig nm;
    nm.interface = arg(argc, argv, "--interface", "wlan0");
    nm.state_dir = state;
    const int port = std::atoi(arg(argc, argv, "--port", "80").c_str());

    setup::NmPlatform platform(nm);
    const std::string board = setup::boardName(platform.mac());
    if (board.empty())
    {
        std::fprintf(stderr, "opendartboard-setup: no MAC address on %s; is that the Wi-Fi interface?\n",
                     nm.interface.c_str());
        return 2;
    }
    std::printf("opendartboard-setup %s: %s, counting in %s/%s\n", APP_VERSION, board.c_str(), state,
                setup::kCounterLeaf);
    std::fflush(stdout);

    setup::Machine machine(platform, std::string(state) + "/" + setup::kCounterLeaf, board, APP_VERSION);
    std::mutex lock;
    std::atomic<bool> accepted{false};

    httplib::Server server;
    server.Get("/setup", [&](const httplib::Request &, httplib::Response &res)
               {
                   std::lock_guard<std::mutex> g(lock);
                   const setup::Reply r = machine.get();
                   res.status = r.status;
                   res.set_content(r.body, "application/json"); });
    server.Post("/setup", [&](const httplib::Request &req, httplib::Response &res)
                {
                    std::lock_guard<std::mutex> g(lock);
                    const setup::Reply r = machine.post(req.body);
                    res.status = r.status;
                    res.set_content(r.body, "application/json");
                    if (r.status == 202)
                    {
                        accepted = true;
                    } });
    std::thread serving([&]
                        { server.listen("0.0.0.0", port); });

    for (;;)
    {
        if (accepted.exchange(false))
        {
            // The 202 is on its way over the access point this is about to drop; give it a
            // second to leave before the radio changes under it.
            std::this_thread::sleep_for(std::chrono::seconds(1));
            std::lock_guard<std::mutex> g(lock);
            machine.apply();
        }
        {
            std::lock_guard<std::mutex> g(lock);
            machine.tick();
        }
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
}
