// #1366: a dart delivered after a restart keeps its position -- driven through the real
// client, a real spool file on a real disk, and a real restart.
//
// THIS IS THE CRITERION THAT CANNOT BE PROVED ON A HAPPY PATH. #822 rule 3 writes a body
// ONCE, when the dart is offered, and posts those same bytes for ever -- out of
// `owed.jsonl` after every restart, for as long as the dart is owed. A position added at
// POST time instead of at BUILD time is therefore a position that every dart thrown while
// the network was down loses, silently, and a run against a reachable server would look
// perfect. So nothing here is asked of a body in memory: the assertion is made against
// the bytes on the disk, and then against what a SECOND PROCESS delivers out of them.
//
//   i1366_spool_check <base-url> <credentials-path> offer|resume
//
//     offer    the address is a closed port. Three placed darts and the takeout that
//              closes the round are offered, the worker writes them to the spool, and the
//              process stops without ever having reached a server. Prints what it offered.
//     resume   the same config dir with #822's stub listening. The client is constructed
//              afresh -- a different process, loadSpool() from the cursor -- and delivers.
//
// testers/i1366_position_check.py runs both halves and reads the spool file between them.

#include "communication/turnaus_client.hpp"

#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

namespace
{
    /**
     * The three darts, with the positions this tester searches for BY VALUE afterwards.
     * Four decimal places apiece, so that what the seam writes and what is searched for
     * are the same number said twice rather than a rounding somebody has to reason about.
     */
    struct PlacedDart
    {
        const char *score;
        float radius;
        float angle;
    };
    const PlacedDart kDarts[3] = {
        {"T20", 0.6123f, 12.3456f},
        {"D18", 0.9876f, 201.5f},
        {"BULL", 0.0212f, 44.25f},
    };

    DetectorResult placedDart(const PlacedDart &d)
    {
        DetectorResult result;
        result.dart_detected = true;
        result.score = d.score;
        result.confidence = 0.9f;
        result.camera_index = 0;
        result.board_radius_known = true;
        result.board_angle_known = true;
        result.board_radius = d.radius;
        result.board_angle = d.angle;
        return result;
    }

    DetectorResult aTakeout()
    {
        DetectorResult result;
        result.score = "END";
        result.confidence = 1.0f;
        return result;
    }

    bool reaches(const TurnausClient &client, uint64_t wanted, int seconds)
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
    if (argc < 4)
    {
        std::printf("USAGE i1366_spool_check <base-url> <credentials-path> offer|resume\n");
        return 2;
    }
    const std::string mode = argv[3];

    TurnausConfig config;
    config.base_url = argv[1];
    config.credentials_path = argv[2];
    config.allow_plaintext = true;
    config.connect_timeout_s = 1;
    config.read_timeout_s = 2;

    TurnausClient client(config);
    if (!client.isPaired())
    {
        std::printf("FAIL no credential was loaded from %s\n", argv[2]);
        return 2;
    }
    client.start();

    if (mode == "offer")
    {
        // Nothing is listening at `base_url`. Every offer is queued, spooled by the worker
        // and then fails to post, which is the state a pub board is in when the router is
        // off -- and the state every one of these darts has to survive.
        for (const PlacedDart &d : kDarts)
        {
            if (!client.offer(placedDart(d)))
            {
                std::printf("FAIL the dart %s was not accepted\n", d.score);
                return 1;
            }
        }
        client.offer(aTakeout());

        // The worker spools an item before it posts it, so the spool file is written
        // within an attempt. Give it attempts rather than a fixed sleep.
        for (int i = 0; i < 300 && client.attempts() < 1; i++)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        std::printf("OFFER queued=%llu delivered=%llu dropped=%llu attempts=%llu\n",
                    (unsigned long long)client.queued(), (unsigned long long)client.delivered(),
                    (unsigned long long)client.dropped(), (unsigned long long)client.attempts());
        if (client.delivered() != 0)
        {
            std::printf("FAIL something was delivered to an address nothing answers\n");
            return 1;
        }
        client.stop();
        return 0;
    }

    if (mode == "resume")
    {
        // A different process over the same config dir: start() calls loadSpool(), which
        // is where the bytes written by the run above come back. Nothing is offered here.
        const bool all = reaches(client, 4, 30);
        std::printf("RESUME queued=%llu delivered=%llu dropped=%llu\n",
                    (unsigned long long)client.queued(), (unsigned long long)client.delivered(),
                    (unsigned long long)client.dropped());
        if (!all)
        {
            std::printf("FAIL the spooled round was not delivered after the restart\n");
        }
        client.stop();
        return all ? 0 : 1;
    }

    std::printf("USAGE i1366_spool_check <base-url> <credentials-path> offer|resume\n");
    return 2;
}
