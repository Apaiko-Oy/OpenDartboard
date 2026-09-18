// #1276: the one state a scripted run of the detector cannot reach, driven directly.
//
// A takeout is published by the scorer only after darts have been published, so a run of
// the real program can never offer an END with NO round begun in that process. That state
// is reachable in the field and is the one #1276 deliberately leaves alone: a board that
// restarts in the middle of a round resumes the darts already owed from its spool -- which
// begins no round, because nothing was offered here -- and then the live END of that same
// round arrives. It must still be sent, at the live binding, exactly as it always was.
// Dropping it would leave those darts sitting in the server's round in hand until some
// later takeout closed them into the wrong turn.
//
// So the client is driven here rather than the program: one TurnausClient, one credential
// file, the real HTTP transport, and #822's stub at the other end. Everything asserted is
// asserted twice over -- by the counters this prints and by the stub's own transcript,
// which testers/i1276_takeout_check.py reads.
//
//   i1276_round_check <base-url> <credentials-path>
//
// Compiled and run by testers/i1276_takeout_check.py's `noround` case, which is where the
// compile line lives.

#include "communication/turnaus_client.hpp"

#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

namespace
{
    DetectorResult aDart(const std::string &sector)
    {
        DetectorResult result;
        result.dart_detected = true;
        result.score = sector;
        result.confidence = 0.9f;
        result.camera_index = 0;
        return result;
    }

    DetectorResult aTakeout()
    {
        // What the scorer publishes when the darts come off the board: no dart, no
        // position, no camera. #822's offer() reads nothing off it but `score`.
        DetectorResult result;
        result.score = "END";
        result.confidence = 1.0f;
        return result;
    }

    /** Wait until the client says it has delivered at least `wanted`, or give up. */
    bool deliveredAtLeast(const TurnausClient &client, uint64_t wanted, int seconds)
    {
        for (int i = 0; i < seconds * 10; i++)
        {
            if (client.delivered() >= wanted)
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
    if (argc < 3)
    {
        std::printf("USAGE i1276_round_check <base-url> <credentials-path>\n");
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
    client.start();

    // THE CASE. A takeout with no round begun in this process -- the darts it ends were
    // pushed by somebody else, which is what the caller has just planted at the stub.
    const bool accepted = client.offer(aTakeout());
    const bool sent = deliveredAtLeast(client, 1, 15);
    std::printf("NOROUND accepted=%d delivered=%llu dropped=%llu\n", accepted ? 1 : 0,
                (unsigned long long)client.delivered(), (unsigned long long)client.dropped());
    if (!sent)
    {
        std::printf("FAIL the takeout with no round in hand was never delivered\n");
    }

    // THE CONTROL, in the same process: a dart and the takeout that ends the round it
    // began. Both go to the same door and neither is dropped.
    client.offer(aDart("S20"));
    client.offer(aTakeout());
    const bool both = deliveredAtLeast(client, 3, 15);
    std::printf("CONTROL delivered=%llu dropped=%llu\n", (unsigned long long)client.delivered(),
                (unsigned long long)client.dropped());
    if (!both)
    {
        std::printf("FAIL the dart and its takeout were not both delivered\n");
    }

    client.stop();
    return (sent && both) ? 0 : 1;
}
