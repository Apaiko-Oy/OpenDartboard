// turnaus#1912: the real TurnausClient against testers/i1912_stub.py -- the #1787 duties on
// the wire, and a dart thrown during a 10 s frames upload.
//
// Two darts and an END are offered. The END owes the log since the last END and an answer
// to the stub's one pending frames request, whose answer the stub holds FRAMES_HOLD_S
// (10 s): the board's slow upload on 2026-10-20, as the board sees it. Two seconds into
// that hold a third dart is offered, and the time from the offer to the queue being empty
// again -- the server's 202 taken -- is the figure asserted under 1 s. Before #1912 that
// dart waited for the hold to end (the push thread was the one inside it), so it read
// about 8 s here.
//
// After stop() the stub's transcript is read back by i1912_check.sh: the log text and the
// frames answer both arrived, the dart arrived inside the hold, and the darts shared a
// connection (keep-alive). This program asserts only what it alone can see -- the offer
// to answer time -- and prints it on a line the script reads.
//
// Args: <port> <workdir>. Env: OD_KEEP_FRAMES=on is required (the frames duty is off
// without it), and HOME must be <workdir> so the spool is kept there.

#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>
#include <sys/stat.h>
#include <thread>

#include "communication/turnaus_client.hpp"

static DetectorResult dart(const std::string &score)
{
    DetectorResult r;
    r.dart_detected = true;
    r.score = score;
    r.confidence = 0.9f;
    r.camera_index = 1;
    return r;
}

/** True when the transcript holds `needle`; the stub writes one JSON object per line. */
static bool transcriptSays(const std::string &transcript, const std::string &needle)
{
    std::ifstream in(transcript.c_str());
    std::string line;
    while (std::getline(in, line))
    {
        if (line.find(needle) != std::string::npos)
        {
            return true;
        }
    }
    return false;
}

int main(int argc, char **argv)
{
    if (argc < 3)
    {
        std::cerr << "usage: i1912_wire_check <port> <workdir>" << std::endl;
        return 2;
    }
    const std::string port = argv[1];
    const std::string dir = argv[2];
    const std::string transcript = dir + "/stub.jsonl";

    const std::string cred = dir + "/credential.json";
    {
        std::ofstream c(cred.c_str());
        c << "{\"token\":\"wire-1912-token\",\"device_id\":1912}";
    }
    chmod(cred.c_str(), 0600);
    const std::string log_file = dir + "/board.log";
    {
        std::ofstream l(log_file.c_str());
        l << "2026-10-20 19:00:00 INFO first line of the board's log, before any END\n";
        l << "2026-10-20 19:00:01 INFO MARKER-1912 the line the stub must receive\n";
    }

    TurnausConfig cfg;
    cfg.base_url = "http://127.0.0.1:" + port;
    cfg.allow_plaintext = true;
    cfg.credentials_path = cred;
    cfg.log_file = log_file;
    cfg.connect_timeout_s = 3;
    // The stub holds the frames answer 10 s; the board's own 5 s would time it out, and
    // what is measured here is the dart BESIDE a long upload, not a timeout.
    cfg.read_timeout_s = 15;
    cfg.backoff_initial_ms = 200;
    cfg.backoff_max_ms = 1000;

    TurnausClient client(cfg);
    if (!client.isPaired())
    {
        std::cout << "FAIL the client did not read its credential" << std::endl;
        return 2;
    }
    client.start();

    client.offer(dart("S20"));
    client.offer(dart("T19"));
    client.offer(dart("END"));

    // Wait for the duty thread to be inside the frames upload.
    bool hold = false;
    for (int i = 0; i < 200 && !hold; i++)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        hold = transcriptSays(transcript, "frames_hold_begun");
    }
    if (!hold)
    {
        std::cout << "FAIL the END's frames answer never reached the stub within 10 s" << std::endl;
        client.stop();
        return 1;
    }
    std::this_thread::sleep_for(std::chrono::seconds(2));

    const auto queued = std::chrono::steady_clock::now();
    client.offer(dart("D13"));
    long long answered_ms = -1;
    for (int i = 0; i < 3000; i++)
    {
        if (client.backlog() == 0)
        {
            answered_ms = (long long)std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::steady_clock::now() - queued)
                              .count();
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    const bool still_holding = !transcriptSays(transcript, "frames_hold_ended");
    std::cout << "WIRE_D13_OFFER_TO_ANSWER_MS " << answered_ms << std::endl;
    std::cout << "WIRE_D13_ANSWERED_INSIDE_HOLD " << (still_holding ? "yes" : "no") << std::endl;

    // Let the frames answer finish so its arrival can be read back, then stop.
    for (int i = 0; i < 300 && !transcriptSays(transcript, "frames_hold_ended"); i++)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    client.stop();

    int rc = 0;
    if (answered_ms < 0 || answered_ms >= 1000)
    {
        std::cout << "FAIL D13 queued during the 10 s frames upload was answered after " << answered_ms
                  << " ms (want < 1000)" << std::endl;
        rc = 1;
    }
    else
    {
        std::cout << "PASS D13 queued during the 10 s frames upload was answered " << answered_ms << " ms after its offer"
                  << std::endl;
    }
    if (!still_holding)
    {
        std::cout << "FAIL D13 was answered only after the frames upload ended" << std::endl;
        rc = 1;
    }
    return rc;
}
