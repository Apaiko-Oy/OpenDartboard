// #1841: the beat meeting a given-up Contest's 409, driven directly -- and the mutation's
// instrument.
//
// The end-to-end cases in i1841_contest_check.py run the REAL detector against #822's stub
// and read its log. What they cannot do is run against a MUTATED client, because the
// detector binary is the worktree's one Linux build and nobody rebuilds it per mutation. So
// the client is driven here the way i1276_round_check.cpp drives it: one TurnausClient, one
// credential file written by --pair and --pair-contest, the real HTTP transport, the stub
// at the other end -- and the translation unit is whichever turnaus_client.cpp the compile
// line names, which under --mutate is a scratch copy with every 409 read as "no Station".
//
// Two modes, one question each:
//
//   over   the stub (STUB_GIVE_UP_STATUS=409, STUB_GIVE_UP_AFTER_BEATS=1) gives the Contest
//          up on the first beat and answers the next with 409 + errors.board. Waits up to
//          thirty seconds for contestId() to reach 0. Prints I1841 released=1 and exits 0
//          when it did; released=0 and 1 when the binding was still held. Under the
//          mutation this is the line that goes red, and the harness's own log lines beside
//          it name the door it read wrongly ("this Contest board stands at no Station").
//   kept   the stub (STUB_GIVE_UP_STATUS=409-nostation) answers 409 WITHOUT errors.board.
//          Waits ten seconds -- five beats at a two-second interval -- and exits 0 only if
//          the binding is STILL held. The control the mutation cannot pass by accident: a
//          client that released on every 409 would go red here instead.
//
// Both modes print the client's own log lines (it logs to the console), so the Python
// harness reads the sentences off this process exactly as it reads them off the detector.
//
//   i1841_beat_check <base-url> <credentials-path> <over|kept>
//
// Compiled and run by testers/i1841_contest_check.py's `harness` case, where the compile
// line lives.

#include "communication/turnaus_client.hpp"

#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

namespace
{
    /** Wait until the Contest binding is gone -- contestId() is 0 -- or give up. */
    bool contestBindingReleased(const TurnausClient &client, int seconds)
    {
        for (int i = 0; i < seconds * 10; i++)
        {
            if (client.contestId() == 0)
            {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        return false;
    }
}

int main(int argc, char **argv)
{
    if (argc < 4)
    {
        std::printf("USAGE i1841_beat_check <base-url> <credentials-path> <over|kept>\n");
        return 2;
    }
    const std::string mode = argv[3];
    if (mode != "over" && mode != "kept")
    {
        std::printf("USAGE i1841_beat_check <base-url> <credentials-path> <over|kept>\n");
        return 2;
    }

    TurnausConfig config;
    config.base_url = argv[1];
    config.credentials_path = argv[2];
    config.allow_plaintext = true;
    config.connect_timeout_s = 2;
    config.read_timeout_s = 3;

    TurnausClient client(config);
    if (!client.isPaired())
    {
        std::printf("FAIL no credential was loaded from %s\n", argv[2]);
        return 2;
    }
    const long long bound_to = client.contestId();
    if (bound_to == 0)
    {
        std::printf("FAIL the credential file names no Contest; pair with --pair-contest first\n");
        return 2;
    }
    client.start();

    if (mode == "over")
    {
        // The stub gives the evening up on the first beat and the second meets the 409.
        // Thirty seconds is fifteen beats at the stub's two-second interval: ample, and
        // short enough that a held binding is reported rather than waited on.
        const bool released = contestBindingReleased(client, 30);
        std::printf("I1841 released=%d contest_was=%lld contest=%lld\n", released ? 1 : 0, bound_to,
                    (long long)client.contestId());
        std::fflush(stdout);
        if (released)
        {
            // Stay up for two more intervals, so the beat AFTER the release -- the one that
            // must land at /api/v1/autoscorer/heartbeats with the club credential -- is in
            // the stub's transcript before the harness reads it.
            std::this_thread::sleep_for(std::chrono::seconds(5));
        }
        client.stop();
        return released ? 0 : 1;
    }

    // kept: the no-Station body at the Contest door must not end the binding. Ten seconds
    // is five beats, each of them a 409 once the stub has given up on the first.
    const bool released = contestBindingReleased(client, 10);
    std::printf("I1841 kept=%d contest_was=%lld contest=%lld\n", released ? 0 : 1, bound_to,
                (long long)client.contestId());
    std::fflush(stdout);
    client.stop();
    return released ? 1 : 0;
}
