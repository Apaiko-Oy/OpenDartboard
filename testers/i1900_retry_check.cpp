// turnaus#1900: the launcher's artefact fetch is a bounded series, and the clock is waited
// for, bounded, before a look. Read testers/i1900_check.sh first.
//
// WHAT IS REAL: apply_update.hpp's applyUpdate() and fetchWithRetries(), and the launcher's
// own fetchArtefactOverHttp() -- the function a board calls -- pointed by
// OD_UPDATE_ARTEFACT_BASE at testers/i1900_server.py, which answers each path from a script
// and writes one line per request to an access log. EVERY REQUEST COUNT BELOW IS READ OUT
// OF THAT LOG, never out of the launcher's own idea of how often it asked: a retry that is
// merely intended -- announced, waited for, never sent -- leaves one line there.
//
// WHAT IS A SEAM: the wait. It records the seconds it was asked for and returns at once, so
// the series' shape is measured without sitting through it; one scenario then runs with the
// REAL wait to measure that three refusals fit inside one minute on the wall clock.
//
//   i1900_retry_check <port> <work dir>
//
// Exit 0 only when every line is ok.

#include "launcher/apply_update.hpp"
#include "launcher/clock_sync.hpp"

#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

static int failures = 0;
static void note(bool ok, const std::string &what)
{
    std::cout << (ok ? "ok   " : "FAIL ") << what << std::endl;
    if (!ok)
    {
        failures++;
    }
}

static std::string slurp(const std::string &path)
{
    std::ifstream in(path.c_str(), std::ios::binary);
    std::stringstream s;
    s << in.rdbuf();
    return s.str();
}

/** Requests the server logged for one path. */
static int requestsFor(const std::string &log, const std::string &path)
{
    std::istringstream lines(slurp(log));
    std::string line;
    int n = 0;
    while (std::getline(lines, line))
    {
        if (line == "GET " + path)
        {
            n++;
        }
    }
    return n;
}

static std::string joined(const std::vector<int> &waits)
{
    std::string s = "[";
    for (size_t i = 0; i < waits.size(); i++)
    {
        s += (i ? "," : "") + std::to_string(waits[i]);
    }
    return s + "]";
}

struct Outcome
{
    launcher::Application application;
    std::vector<int> waits;
    std::vector<launcher::Text> said_now;  // the retry lines, said as they happened
    std::vector<launcher::Text> said_after; // applicationLines()
    int requests = 0;
    double wall_s = 0;
};

static bool saidEn(const std::vector<launcher::Text> &lines, const std::string &needle)
{
    for (size_t i = 0; i < lines.size(); i++)
    {
        if (lines[i].en.find(needle) != std::string::npos)
        {
            return true;
        }
    }
    return false;
}
static bool saidFi(const std::vector<launcher::Text> &lines, const std::string &needle)
{
    for (size_t i = 0; i < lines.size(); i++)
    {
        if (lines[i].fi.find(needle) != std::string::npos)
        {
            return true;
        }
    }
    return false;
}

/** One application of a release at /rel/<scenario>.tar.gz, with a fresh install around it. */
static Outcome apply(const std::string &work, const std::string &log, const std::string &good,
                     const std::string &scenario, bool real_wait = false)
{
    Outcome outcome;
    const std::string dir = work + "/" + scenario;
    launcher::removeTree(dir);
    launcher::ensureDirectory(dir);
    const std::string detector = launcher::under(dir, launcher::kDetectorFileName);
    {
        std::ofstream old(detector.c_str(), std::ios::binary);
        old << "the version this board already had\n";
    }
    const launcher::Layout layout = launcher::layoutFor(detector);

    update_check::Answer answer;
    answer.kind = update_check::Kind::Available;
    answer.running_version = "1.0.0";
    answer.published_version = "1.1.0";
    answer.published_minimum_launcher = "1.0.0";
    answer.published_url = "https://github.com/example/releases/download/v1.1.0/" + scenario + ".tar.gz";
    answer.published_size = static_cast<long long>(good.size());
    answer.published_sha256 = od_sha256::hex(good);

    // The unpack seam: the bytes were already proved by digest, so it only has to put a
    // detector in staging for the swap to find.
    launcher::Unpack unpack = [](const std::string &, const std::string &into)
    {
        std::ofstream out(launcher::under(into, launcher::kDetectorFileName).c_str(), std::ios::binary);
        out << "release 1.1.0\n";
        return static_cast<bool>(out);
    };
    std::vector<int> *waits = &outcome.waits;
    launcher::Wait wait = [waits](int seconds) { waits->push_back(seconds); };
    if (real_wait)
    {
        wait = [waits](int seconds)
        {
            waits->push_back(seconds);
            launcher::waitForReal(seconds);
        };
    }
    std::vector<launcher::Text> *now = &outcome.said_now;
    launcher::SayNow say = [now](const launcher::Text &line) { now->push_back(line); };

    launcher::State state;
    const std::chrono::steady_clock::time_point began = std::chrono::steady_clock::now();
    outcome.application = launcher::applyUpdate(layout, answer, "1.0.0", state, launcher::fetchArtefactOverHttp,
                                                unpack, wait, say);
    outcome.wall_s = std::chrono::duration<double>(std::chrono::steady_clock::now() - began).count();
    outcome.said_after = launcher::applicationLines(outcome.application);
    outcome.requests = requestsFor(log, "/example/releases/download/v1.1.0/" + scenario + ".tar.gz");

    std::cout << "---- " << scenario << ": step " << static_cast<int>(outcome.application.step) << ", "
              << outcome.requests << " request(s) in the server's log, waits " << joined(outcome.waits) << ", "
              << outcome.wall_s << " s" << std::endl;
    for (size_t i = 0; i < outcome.said_now.size(); i++)
    {
        std::cout << "     | " << outcome.said_now[i].fi << "\n     | " << outcome.said_now[i].en << std::endl;
    }
    for (size_t i = 0; i < outcome.said_after.size(); i++)
    {
        std::cout << "     | " << outcome.said_after[i].fi << "\n     | " << outcome.said_after[i].en << std::endl;
    }
    return outcome;
}

static void theClock(const std::string &work)
{
    std::cout << "---- the clock (clock_sync.hpp), over a timesyncd directory of the check's own" << std::endl;
    const std::string dir = work + "/timesync";
    const std::string flag = dir + "/synchronized";
    launcher::removeTree(dir);

    int asked = 0;
    launcher::Wait counting = [&asked](int seconds) { asked += seconds; };

    launcher::ClockAtLook clock = launcher::waitForTheClock(dir, counting);
    note(!clock.readable && asked == 0, "no timesyncd directory (Windows, a container): unreadable, and no wait");
    clock = launcher::waitForTheClock("", counting);
    note(!clock.readable && asked == 0, "an empty directory name (OD_TIMESYNC_DIR=''): unreadable, and no wait");

    launcher::ensureDirectory(dir);
    { std::ofstream touch(flag.c_str()); }
    clock = launcher::waitForTheClock(dir, counting);
    note(clock.readable && clock.synchronised && clock.waited_s == 0 && asked == 0,
         "the flag already set: synchronised, and no wait");
    note(launcher::clockText(clock).en == "The clock was synchronised when the look for updates began.",
         "  and it says so: " + launcher::clockText(clock).en);

    std::remove(flag.c_str());
    asked = 0;
    // timesyncd synchronises during the wait: the flag appears after the third second.
    launcher::Wait syncs_at_three = [&asked, flag](int seconds)
    {
        asked += seconds;
        if (asked == 3)
        {
            std::ofstream touch(flag.c_str());
        }
    };
    clock = launcher::waitForTheClock(dir, syncs_at_three);
    note(clock.readable && clock.synchronised && clock.waited_s == 3 && asked == 3,
         "the flag set during the wait: synchronised after " + std::to_string(clock.waited_s) + " s, and no longer");
    std::cout << "     | " << launcher::clockText(clock).fi << "\n     | " << launcher::clockText(clock).en << std::endl;

    std::remove(flag.c_str());
    asked = 0;
    clock = launcher::waitForTheClock(dir, counting);
    note(clock.readable && !clock.synchronised && clock.waited_s == launcher::kClockSyncWaitSeconds && asked == 30,
         "the flag never set: the wait is bounded at 30 s (" + std::to_string(asked) + " s asked for) and the look "
         "goes on");
    std::cout << "     | " << launcher::clockText(clock).fi << "\n     | " << launcher::clockText(clock).en << std::endl;

    // And the env pin the real launcher reads.
    setenv("OD_TIMESYNC_DIR", dir.c_str(), 1);
    note(launcher::timesyncDirectory() == dir, "OD_TIMESYNC_DIR replaces the directory");
    setenv("OD_TIMESYNC_DIR", "", 1);
    note(launcher::timesyncDirectory().empty(), "OD_TIMESYNC_DIR='' turns the wait off");
    unsetenv("OD_TIMESYNC_DIR");
    note(launcher::timesyncDirectory() == "/run/systemd/timesync", "unset, it is timesyncd's /run/systemd/timesync");
}

int main(int argc, char **argv)
{
    if (argc < 3)
    {
        std::cerr << "usage: i1900_retry_check <port> <work dir>" << std::endl;
        return 2;
    }
    const std::string port = argv[1];
    const std::string work = argv[2];
    const std::string log = work + "/access.log";
    const std::string good = slurp(work + "/good.bin");
    if (good.empty())
    {
        std::cerr << "no " << work << "/good.bin" << std::endl;
        return 2;
    }
    setenv("OD_UPDATE_ARTEFACT_BASE", ("http://127.0.0.1:" + port).c_str(), 1);

    using launcher::Step;

    // 1. 403, then 200: installed at the second attempt.
    {
        const Outcome o = apply(work, log, good, "r403-200");
        note(o.application.ok && o.application.step == Step::Applied, "403 then 200: installed");
        note(o.requests == 2, "  at the second attempt: 2 requests in the server's log (" + std::to_string(o.requests) +
                                  ")");
        note(o.waits == std::vector<int>({5}), "  after one wait of 5 s " + joined(o.waits));
        note(saidEn(o.said_now, "The release could not be fetched (HTTP 403); trying again, 2 of 3, in 5 seconds.") &&
                 saidFi(o.said_now, "Julkaisua ei saatu haettua (HTTP 403), yritetään uudelleen 2/3 5 sekunnin "
                                    "kuluttua."),
             "  and the retry was said, in Finnish and English, before the wait");
    }

    // 2. 403, 403, 403: the old version, and when the board looks next.
    {
        const Outcome o = apply(work, log, good, "r403-403-403");
        note(!o.application.ok && o.application.step == Step::CouldNotReach, "403, 403, 403: not installed");
        note(o.requests == 3, "  after 3 requests in the server's log (" + std::to_string(o.requests) + ")");
        note(o.waits == std::vector<int>({5, 15}), "  waiting 5 s then 15 s " + joined(o.waits));
        note(saidEn(o.said_now, "trying again, 3 of 3, in 15 seconds."), "  the second retry line names 3 of 3, 15 s");
        note(saidEn(o.said_after, "The release could not be fetched: HTTP 403."), "  the last failure is named");
        note(saidEn(o.said_after, "The board starts version 1.0.0, the one it already had; it looks again at the next "
                                  "06:00 restart, or now with --update-now.") &&
                 saidFi(o.said_after, "seuraavassa kello 06:00 uudelleenkäynnistyksessä, tai heti valitsimella "
                                      "--update-now."),
             "  and the next-look sentence, in both languages");
        note(launcher::fileExists(launcher::under(work + "/r403-403-403", launcher::kDetectorFileName)) &&
                 slurp(launcher::under(work + "/r403-403-403", launcher::kDetectorFileName)) ==
                     "the version this board already had\n",
             "  and the detector it already had is untouched");
    }

    // 3. 404: not retried.
    {
        const Outcome o = apply(work, log, good, "r404-200");
        note(!o.application.ok && o.application.step == Step::CouldNotReach, "404: not installed");
        note(o.requests == 1 && o.waits.empty() && o.said_now.empty(),
             "  and NOT retried: 1 request, no wait, no retry line (" + std::to_string(o.requests) + ")");
    }

    // 4. 429 with Retry-After: 7 waits 7, not 5.
    {
        const Outcome o = apply(work, log, good, "r429ra7-200");
        note(o.application.ok, "429 (Retry-After: 7) then 200: installed");
        note(o.waits == std::vector<int>({7}), "  after a wait of 7 s, not 5 " + joined(o.waits));
        note(o.requests == 2, "  2 requests in the server's log (" + std::to_string(o.requests) + ")");
        note(saidEn(o.said_now, "(HTTP 429); trying again, 2 of 3, in 7 seconds."), "  and the line says 7 seconds");
    }

    // 5. A digest mismatch: not retried, though the next copy would have been good.
    {
        const Outcome o = apply(work, log, good, "rbad-200");
        note(!o.application.ok && o.application.step == Step::DigestMismatch, "wrong bytes: the digest refuses them");
        note(o.requests == 1 && o.waits.empty(),
             "  and NOT retried: 1 request, no wait (" + std::to_string(o.requests) + ")");
        note(!saidEn(o.said_after, "looks again at the next 06:00"),
             "  a digest refusal keeps #1306's sentence: the next-look clause is the fetch's");
    }

    // 6. 503 then 500 then 200: installed at the third attempt -- 5xx is retried.
    {
        const Outcome o = apply(work, log, good, "r503-500-200");
        note(o.application.ok && o.requests == 3 && o.waits == std::vector<int>({5, 15}),
             "503, 500, 200: installed at the third attempt, waits " + joined(o.waits) + ", " +
                 std::to_string(o.requests) + " requests");
    }

    // 7. A Retry-After longer than the look waits ends the series rather than being shortened.
    {
        const Outcome o = apply(work, log, good, "r503ra99-200");
        note(!o.application.ok && o.requests == 1 && o.waits.empty(),
             "503 (Retry-After: 99): the series ends, 1 request, no wait (" + std::to_string(o.requests) + ")");
    }

    // 8. 408: retried.
    {
        const Outcome o = apply(work, log, good, "r408-200");
        note(o.application.ok && o.requests == 2, "408 then 200: installed at the second attempt");
    }

    // 9. Nothing answering: a transport error is retried. A port nobody listens on.
    {
        setenv("OD_UPDATE_ARTEFACT_BASE", "http://127.0.0.1:9", 1);
        const Outcome o = apply(work, log, good, "rclosed");
        setenv("OD_UPDATE_ARTEFACT_BASE", ("http://127.0.0.1:" + port).c_str(), 1);
        note(!o.application.ok && o.application.attempts == 3 && o.waits == std::vector<int>({5, 15}),
             "a closed port: 3 attempts, waits " + joined(o.waits) + ", then the old version");
    }

    // 10. The whole series, on the wall clock, with the REAL wait. A mutant run skips it.
    if (std::getenv("OD_I1900_NO_WALL") == NULL)
    {
        const Outcome o = apply(work, log, good, "rwall-403-403-403", /*real_wait*/ true);
        note(o.requests == 3 && o.wall_s < 60.0,
             "three refusals with the real wait: " + std::to_string(o.requests) + " requests in " +
                 std::to_string(o.wall_s) + " s, inside one minute");
        note(o.wall_s >= 19.5, "  and it really waited (5 + 15 s)");
    }

    theClock(work);

    std::cout << failures << " failed" << std::endl;
    return failures == 0 ? 0 : 1;
}
