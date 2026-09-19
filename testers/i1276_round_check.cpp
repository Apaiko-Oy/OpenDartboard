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
// #1374 gives it a SECOND case for the opposite reason. `released` is the state the
// detector CAN reach and cannot be made to reach on cue: a takeout ending a round begun on
// an evening that has since been given up. #1276's `givenup` case arranged it by counting
// darts at the stub -- the third dart of the mock footage used to be the last dart of the
// first round, so the release landed between that dart and its END. #1353 and #1354 changed
// what the detector reads off those same three files (the first round is two darts and a
// takeout now, not three darts), the third dart became the FIRST dart of the second round,
// and the ordering the case was built on stopped happening. It was never arranged: it was
// a coincidence of the footage that a dart counter happened to name. Here the ordering IS
// arranged -- one round opened at the Casual door, the evening given up, the binding seen
// to be gone, and only then the takeout -- so no reading of any video can move it.
//
//   i1276_round_check <base-url> <credentials-path> [noround|released]
//
// Compiled and run by testers/i1276_takeout_check.py's `noround` and `released` cases,
// which is where the compile line lives.

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

    /** Wait until the Contest binding is gone -- `contestId()` is 0 -- or give up. */
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

    /**
     * #1374's case. The three events #1276 is about, in the one order that makes it the
     * issue it is, and every one of them waited for rather than hoped for.
     */
    int released(TurnausClient &client)
    {
        bool well = true;

        // A round is begun at the Casual door: this dart is what opens it, and the door it
        // goes out of is what the round remembers.
        client.offer(aDart("S20"));
        if (!deliveredAtLeast(client, 1, 15))
        {
            std::printf("FAIL the dart that opens the round never reached the Casual door\n");
            well = false;
        }

        // The stub gave the evening up on that dart (STUB_GIVE_UP_AFTER=1). A second dart
        // is what meets the refusal -- #891's `deliver()` 401, which is the same verdict
        // the beat reaches and the one a harness can time. It is abandoned with the
        // evening, as everything owed to it is.
        client.offer(aDart("S20"));
        const bool released_now = contestBindingReleased(client, 30);
        std::printf("RELEASED contest=%lld dropped=%llu\n", (long long)client.contestId(),
                    (unsigned long long)client.dropped());
        if (!released_now)
        {
            std::printf("FAIL the evening was given up and the binding is still held\n");
            client.stop();
            return 1;
        }

        // THE CASE. The takeout that ends the round begun above. The board is on its club
        // binding now, and this takeout belongs to neither door: to the Casual one because
        // the evening is over, to the club's because the club never saw the round begin.
        const uint64_t dropped_before = client.dropped();
        const bool accepted = client.offer(aTakeout());
        std::printf("GIVENUP accepted=%d dropped=%llu (was %llu)\n", accepted ? 1 : 0,
                    (unsigned long long)client.dropped(), (unsigned long long)dropped_before);

        // THE CONTROL, in the same process: the next dart begins a fresh round at the club
        // and its takeout closes it there. #1276 is narrow, and this is the edge of it.
        client.offer(aDart("S20"));
        client.offer(aTakeout());
        const bool both = deliveredAtLeast(client, 3, 15);
        std::printf("CONTROL delivered=%llu dropped=%llu\n", (unsigned long long)client.delivered(),
                    (unsigned long long)client.dropped());
        if (!both)
        {
            std::printf("FAIL the club's own dart and takeout were not both delivered\n");
            well = false;
        }

        client.stop();
        return well ? 0 : 1;
    }
}

int main(int argc, char **argv)
{
    if (argc < 3)
    {
        std::printf("USAGE i1276_round_check <base-url> <credentials-path> [noround|released]\n");
        return 2;
    }
    const std::string mode = argc > 3 ? argv[3] : "noround";
    if (mode != "noround" && mode != "released")
    {
        std::printf("USAGE i1276_round_check <base-url> <credentials-path> [noround|released]\n");
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

    if (mode == "released")
    {
        return released(client);
    }

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
