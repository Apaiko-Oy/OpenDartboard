// #1306: the whole of the launcher's update half, measured -- the decisions in process,
// and then the real thing against a real server, real signatures, real zips and real
// child processes.
//
// WHAT IS REAL HERE AND WHAT IS NOT. Everything except the scheme. The manifest is
// fetched over a socket by update_check::fetchOverHttp; the signature is verified by
// p256_verify.hpp against a key OpenSSL minted; the artefact is fetched over a socket by
// the launcher's own fetchArtefactOverHttp; the digest is od_sha256's; the zip is
// unpacked by the real seam; the swap is the real rename; and the detector is really
// started, really waited for and really classified. The ONE substitution is that the
// fixture's `https://releases.example.invalid/...` is rewritten to `http://127.0.0.1:<port>`
// before the socket is opened, because manifest.hpp refuses a payload whose url is not
// https (rightly) and this container's httplib is built without TLS (#822). The rewrite
// is four lines, it is below, and it changes the host and the scheme and nothing else.
//
// THE ORDER OF THE END-TO-END CASES IS THE POINT OF THEM. They run against ONE install
// directory, in sequence, the way a board lives: a fresh install updates, a restart four
// minutes later does not check at all, sixteen minutes later it updates again, and then
// it is sent a release that will not start and goes back. Each case inherits the state
// the last one left, because a slice about rollback that reset the world between cases
// would never measure the thing it is about.
//
//   testers/i1306_update_check.cpp <fixtures> <work> <www> <accesslog> <port>

#include "launcher/apply_update.hpp"
#include "launcher/detector_process.hpp"
#include "launcher/install_layout.hpp"
#include "launcher/launcher.hpp"
#include "launcher/update_moment.hpp"
#include "update/update_check.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <sys/stat.h>
#include <vector>

static int failures = 0;

static void note(bool passed, const std::string &what)
{
    std::cout << (passed ? "ok   " : "FAIL ") << what << std::endl;
    if (!passed)
    {
        failures++;
    }
}

// ------------------------------------------------------------------ a console that keeps

class Recorder : public console_prompt::Console
{
public:
    void say(const console_prompt::Text &text) override
    {
        lines.push_back(text.fi);
        lines.push_back(text.en);
    }
    void sayVerbatim(const std::string &line) override { lines.push_back(line); }
    bool readLine(std::string &) override { return false; }

    bool said(const std::string &needle) const
    {
        for (size_t i = 0; i < lines.size(); i++)
        {
            if (lines[i].find(needle) != std::string::npos)
            {
                return true;
            }
        }
        return false;
    }
    void print(const char *prefix) const
    {
        for (size_t i = 0; i < lines.size(); i++)
        {
            std::cout << "     " << prefix << " " << lines[i] << std::endl;
        }
    }
    std::vector<std::string> lines;
};

// ------------------------------------------------------------------ small file helpers

static std::string slurp(const std::string &path)
{
    std::ifstream in(path.c_str(), std::ios::binary);
    std::string all((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return all;
}

static void spill(const std::string &path, const std::string &body)
{
    std::ofstream out(path.c_str(), std::ios::binary | std::ios::trunc);
    out.write(body.data(), static_cast<std::streamsize>(body.size()));
}

static long long inodeOf(const std::string &path)
{
    struct stat info;
    if (::stat(path.c_str(), &info) != 0)
    {
        return -1;
    }
    return static_cast<long long>(info.st_ino);
}

/** How many times the server has been asked for a manifest, out of its own access log. */
static int manifestRequests(const std::string &log)
{
    const std::string all = slurp(log);
    int count = 0;
    size_t at = 0;
    const std::string needle = "GET /updates/opendartboard/";
    while ((at = all.find(needle, at)) != std::string::npos)
    {
        count++;
        at += needle.size();
    }
    return count;
}

// ------------------------------------------------------------------ the pure decisions

static void theMomentRule()
{
    std::cout << "---- ADR-0077 §7: whether this launch may look at all ----" << std::endl;
    const long long now = 1758300000;

    launcher::State fresh;
    note(launcher::decideMoment(fresh, now, false).may_check &&
             launcher::decideMoment(fresh, now, false).moment == launcher::Moment::MayCheck,
         "a board that has never recorded a stop may check");

    launcher::State recent;
    recent.last_ending = "cleanly";
    recent.last_stopped = now - (14 * 60 + 59);
    note(!launcher::decideMoment(recent, now, false).may_check &&
             launcher::decideMoment(recent, now, false).moment == launcher::Moment::QuickRestart,
         "stopped 14m59s ago: a quick restart, and no check is made");

    launcher::State older = recent;
    older.last_stopped = now - (15 * 60 + 1);
    note(launcher::decideMoment(older, now, false).may_check,
         "stopped 15m01s ago: the quiet window has passed and it may check");

    launcher::State faulted;
    faulted.last_ending = "faulted";
    faulted.last_stopped = now - 3 * 24 * 3600;
    note(!launcher::decideMoment(faulted, now, false).may_check &&
             launcher::decideMoment(faulted, now, false).moment == launcher::Moment::EndedBadly,
         "the last run faulted three days ago: still no check, because somebody may be standing there");

    launcher::State killed;
    killed.last_ending = "killed";
    killed.last_stopped = now - 3 * 24 * 3600;
    note(launcher::decideMoment(killed, now, false).may_check,
         "the last run was STOPPED BY HAND three days ago: that is not a bad ending, and it may check");

    note(launcher::decideMoment(recent, now, true).may_check &&
             launcher::decideMoment(faulted, now, true).moment == launcher::Moment::Forced,
         "--update-now overrides both");

    launcher::State future = recent;
    future.last_stopped = now + 3600; // a clock corrected backwards
    note(!launcher::decideMoment(future, now, false).may_check,
         "a clock that has gone backwards reads as recent, not as ancient");
}

static void whatFailedToStartMeans()
{
    std::cout << std::endl << "---- what counts as a failure to start (#895's vigil cannot trip it) ----" << std::endl;
    using launcher::Ending;
    note(launcher::failedToStart(Ending::NeverStarted, 0) && launcher::failedToStart(Ending::NeverStarted, 100000),
         "a process that was never created is a failure to start, at any duration");
    note(launcher::failedToStart(Ending::Faulted, 0) && launcher::failedToStart(Ending::Faulted, 59),
         "a fault in the first 59 seconds is a failure to start");
    note(!launcher::failedToStart(Ending::Faulted, 60) && !launcher::failedToStart(Ending::Faulted, 36000),
         "a fault at 60 seconds and beyond is a board that RAN and then fell over, and is not");
    note(!launcher::failedToStart(Ending::Killed, 0) && !launcher::failedToStart(Ending::Killed, 1),
         "SOMEBODY STOPPING THE BOARD is never a failure to start, however fast");
    note(!launcher::failedToStart(Ending::Cleanly, 0), "and neither is a clean exit");
    note(launcher::kAllowedFailedStarts == 2 && launcher::kSettledSeconds == 60,
         "the two numbers are 2 attempts and 60 seconds");
}

static void theVersionOrdering()
{
    std::cout << std::endl << "---- minimumLauncher: the one ordering in this program (ADR-0077 §1) ----" << std::endl;
    note(launcher::versionIsAtLeast("v1.0.0", "v1.0.0"), "equal is enough");
    note(!launcher::versionIsAtLeast("v1.0.0", "v1.0.1"), "older is refused");
    note(launcher::versionIsAtLeast("v1.10.0", "v1.9.0"),
         "v1.10 is newer than v1.9, which a string compare gets wrong");
    note(launcher::versionIsAtLeast("v2", "v1.9.9"), "a short version is padded with zeroes, not refused");
    note(launcher::versionIsAtLeast("1.4.2-rc1", "v1.4.2"), "a suffix and a leading v are both ignored");
    note(!launcher::versionIsAtLeast("0.0.0-dev", "v1.0.0"), "a development build is below every real minimum");
    note(!launcher::versionIsAtLeast("dev", "v1.0.0") && !launcher::versionIsAtLeast("v1.0.0", "nonsense"),
         "anything that cannot be read is REFUSED, which is the safe direction");
}

static void theStateFile(const std::string &work)
{
    std::cout << std::endl << "---- the six facts, and a file that cannot fail to parse ----" << std::endl;
    const std::string path = work + "/state-probe.txt";
    launcher::State written;
    written.detector_version = "v1.4.2";
    written.previous_version = "v1.4.1";
    written.last_started = 1758300000;
    written.last_stopped = 1758300600;
    written.last_ending = "cleanly";
    written.failed_starts = 1;
    note(launcher::writeState(path, written), "it writes");
    const launcher::State read = launcher::readState(path);
    note(read.detector_version == "v1.4.2" && read.previous_version == "v1.4.1" && read.last_stopped == 1758300600 &&
             read.last_ending == "cleanly" && read.failed_starts == 1,
         "and reads back every one of the six");

    spill(path, "this is not a state file at all\n\0\0\0binary\n");
    const launcher::State ruined = launcher::readState(path);
    note(ruined.detector_version.empty() && ruined.failed_starts == 0 && ruined.last_stopped == 0,
         "a ruined file reads as a board that has never updated, rather than as a launcher that will not run");
    const launcher::State missing = launcher::readState(work + "/no-such-state.txt");
    note(missing.detector_version.empty() && missing.last_stopped == 0, "and so does a missing one");
    std::remove(path.c_str());
}

// ------------------------------------------------------------------ the real thing

struct World
{
    std::string fixtures;
    std::string work;
    std::string www;
    std::string log;
    int port = 0;

    std::string install() const { return work + "/install"; }
    std::string detector() const { return install() + "/opendartboard"; }
    launcher::Layout layout() const { return launcher::layoutFor(detector()); }
};

/** Serve this manifest at the stable channel's address, as a deployment would. */
static void publish(const World &world, const std::string &fixture)
{
    spill(world.www + "/updates/opendartboard/stable.json", slurp(world.fixtures + "/" + fixture));
}

/** Serve nothing at all: what a deployment that has published none answers. */
static void publishNothing(const World &world)
{
    std::remove((world.www + "/updates/opendartboard/stable.json").c_str());
}

struct Run
{
    launcher::Report report;
    Recorder console;
    int manifest_requests = 0;
};

/**
 * One carry, with every seam the real launcher uses except the two named at the top of
 * this file. `cut_after` truncates the artefact to that many bytes, which is a download
 * interrupted part-way arranged rather than waited for; 0 means do not cut.
 */
static Run carryOnce(const World &world, long long now, bool forced, size_t cut_after = 0)
{
    Run run;
    const int before = manifestRequests(world.log);

    launcher::Surroundings surroundings;
    surroundings.layout = world.layout();
    surroundings.address = "http://127.0.0.1:" + std::to_string(world.port);
    surroundings.channel = "stable";
    std::string anchor = slurp(world.fixtures + "/anchor.hex");
    while (!anchor.empty() && (anchor[anchor.size() - 1] == '\n' || anchor[anchor.size() - 1] == '\r'))
    {
        anchor.erase(anchor.size() - 1);
    }
    surroundings.anchors.push_back(update_manifest::anchorFromHex(anchor));
    surroundings.fetch_manifest = update_check::fetchOverHttp;
    const std::string local = "http://127.0.0.1:" + std::to_string(world.port);
    surroundings.fetch_artefact = [local, cut_after](const std::string &url)
    {
        // The one substitution. The scheme and host of the SIGNED url are replaced with
        // this harness's own; the path is the path the manifest named, byte for byte.
        const std::string marker = "https://releases.example.invalid";
        std::string reachable = url;
        if (reachable.compare(0, marker.size(), marker) == 0)
        {
            reachable = local + reachable.substr(marker.size());
        }
        odhttp::Response response = launcher::fetchArtefactOverHttp(reachable);
        if (cut_after > 0 && response.body.size() > cut_after)
        {
            response.body.resize(cut_after);
        }
        return response;
    };
    surroundings.unpack = launcher::unpackWithSystemTool;
    surroundings.clock = [now]() { return now; };
    surroundings.forced = forced;

    std::vector<std::string> arguments;
    arguments.push_back("--cams");
    arguments.push_back("0,1,2");
    run.report = launcher::carry(arguments, run.console, /*somebodyIsThere*/ false, launcher::runAndWait, surroundings);
    run.manifest_requests = manifestRequests(world.log) - before;
    return run;
}

/** What the state file says now. */
static launcher::State nowHolding(const World &world) { return launcher::readState(world.layout().state_file); }

static void theBoardLives(const World &world, const std::string &first, const std::string &second,
                          const std::string &third, const std::string &bad)
{
    const std::string ran = world.work + "/ran.txt";
    ::setenv("OD_STUB_RAN_TO", ran.c_str(), 1);
    ::setenv("OD_STUB_ARGV_TO", (world.work + "/argv.txt").c_str(), 1);
    ::setenv("OD_STUB_BAD", bad.c_str(), 1);

    long long now = 1758300000;

    // ---- 1. a fresh install, and a channel that publishes something newer -------------
    std::cout << std::endl
              << "---- 1. a fresh install on " << first << ", and " << second << " is published ----" << std::endl;
    std::remove(ran.c_str());
    publish(world, "stable-" + second + ".json");
    const long long was_inode = inodeOf(world.detector());
    Run one = carryOnce(world, now, false);
    one.console.print("|");
    note(one.report.applied_an_update, "the update was applied");
    note(slurp(ran) == second + "\n", "and the version that really ran is " + second);
    note(one.report.ran_version == second, "  which is the version the launcher says it ran");
    note(nowHolding(world).detector_version == second && nowHolding(world).previous_version == first,
         "the state names " + second + " installed and " + first + " kept");
    note(launcher::fileExists(world.layout().previous_exe), "and the kept version is really on disk");
    note(inodeOf(world.layout().previous_exe) == was_inode,
         "THE SWAP WAS A RENAME: the kept file is the same file object the install had, not a copy of it");
    note(!launcher::directoryExists(world.layout().staging_dir) && !launcher::fileExists(world.layout().download_file),
         "and nothing is left staged");
    note(slurp(world.work + "/argv.txt").find("--cams") != std::string::npos,
         "the arguments the install carries still reached the detector");

    // ---- 2. four minutes later, the board is restarted --------------------------------
    std::cout << std::endl << "---- 2. four minutes later, somebody restarts the board ----" << std::endl;
    std::remove(ran.c_str());
    publish(world, "stable-" + third + ".json");
    now += 4 * 60;
    Run two = carryOnce(world, now, false);
    note(two.manifest_requests == 0,
         "NOT ONE REQUEST REACHED THE SERVER -- the rule was evaluated, not intended (its own access log says so)");
    note(!two.report.looked_for_an_update && two.report.moment == launcher::Moment::QuickRestart,
         "the launcher says why: a quick restart");
    note(slurp(ran) == second + "\n", "and the board started " + second + ", the version it had");

    // ---- 3. and again, sixteen minutes after that: two updates in a row ---------------
    std::cout << std::endl << "---- 3. sixteen minutes later: the second update in a row ----" << std::endl;
    std::remove(ran.c_str());
    now += 16 * 60;
    Run three = carryOnce(world, now, false);
    three.console.print("|");
    note(three.manifest_requests > 0, "now it does ask");
    note(three.report.applied_an_update && slurp(ran) == third + "\n",
         "TWO UPDATES IN A ROW: " + first + " -> " + second + " -> " + third + ", and " + third + " is what ran");
    note(nowHolding(world).previous_version == second, "and what is kept is now " + second + ", not " + first);

    // ---- 4. the superseded release, published again -----------------------------------
    std::cout << std::endl
              << "---- 4. " << second << " is published again, after being superseded by " << third << " ----"
              << std::endl;
    std::remove(ran.c_str());
    publish(world, "stable-" + second + ".json");
    now += 16 * 60;
    Run four = carryOnce(world, now, false);
    note(four.report.applied_an_update && slurp(ran) == second + "\n",
         "a superseded version installs like any other: nothing here orders two versions");
    note(nowHolding(world).detector_version == second && nowHolding(world).previous_version == third,
         "and " + third + " is what is now kept, which is how a bad release is undone for a whole channel");

    // ---- 5. a release that will not start ---------------------------------------------
    std::cout << std::endl << "---- 5. " << bad << " is published, and it does not start ----" << std::endl;
    std::remove(ran.c_str());
    publish(world, "stable-" + bad + ".json");
    now += 16 * 60;
    Run five = carryOnce(world, now, false);
    five.console.print("|");
    note(five.report.applied_an_update, "it was installed, because nothing could have known");
    note(five.report.rolled_back, "IT WENT BACK");
    note(slurp(ran) == bad + "\n" + bad + "\n" + second + "\n",
         "and it went back after exactly two attempts: " + bad + ", " + bad + ", then " + second);
    note(five.report.starts == 3 && five.report.starts <= launcher::kAttemptsInOneCarry,
         "which is inside the bound this carry has");
    note(five.console.said("Going back to version " + second) && five.console.said("Palataan versioon " + second),
         "the launcher says which version is running and why, in both languages");
    note(five.console.said("opendartboard " + second), "and names the version it ended on");
    note(nowHolding(world).detector_version == second && nowHolding(world).previous_version.empty(),
         "the state names " + second + " running and nothing kept: there is one previous and it has been used");
    note(five.report.exit_code == launcher::kRanCleanly,
         "and the launcher exits on what the version it went back to did, not on what the bad one did");

    // ---- 6. the board is stopped by hand, fast ----------------------------------------
    std::cout << std::endl << "---- 6. somebody stops the board two seconds after starting it ----" << std::endl;
    std::remove(ran.c_str());
    publishNothing(world);
    ::setenv("OD_STUB_KILLED", second.c_str(), 1);
    now += 16 * 60;
    Run six = carryOnce(world, now, false);
    ::unsetenv("OD_STUB_KILLED");
    note(!six.report.rolled_back && six.report.starts == 1,
         "THE FASTEST BAD ENDING THERE IS, and nothing was retried and nothing was rolled back");
    note(six.report.exit_code == launcher::kKilled, "it is reported as a board that was stopped (exit 42)");
    note(nowHolding(world).failed_starts == 0, "and nothing is counted against the version");
    note(launcher::decideMoment(nowHolding(world), now + 24 * 3600, false).may_check,
         "  and the next launch a day later may still check, because being stopped is not ending badly");
}

static void theRefusals(const World &world, const std::string &running, const std::string &offered)
{
    long long now = 1758400000;
    const std::string ran = world.work + "/ran.txt";
    const launcher::State before = nowHolding(world);

    struct Refusal
    {
        const char *fixture;
        const char *what;
        const char *needle;
        size_t cut;
    };
    const Refusal refusals[] = {
        {"baddigest-", "an artefact whose digest does not match the signed manifest", "CHECKSUM DOES NOT MATCH", 0},
        {"tampered-", "a manifest with one payload byte changed and the signature left alone",
         "SIGNATURE VERIFICATION FAILED", 0},
        {"stranger-", "a perfectly valid signature by a key this build has never heard of",
         "SIGNATURE VERIFICATION FAILED", 0},
        {"toonew-", "a release that needs a launcher newer than this one", "needs launcher v9999.0.0", 0},
        {"stable-", "a download cut off part-way", "cut short", 1024},
    };

    for (size_t i = 0; i < sizeof(refusals) / sizeof(refusals[0]); i++)
    {
        std::cout << std::endl << "---- refusal: " << refusals[i].what << " ----" << std::endl;
        std::remove(ran.c_str());
        publish(world, std::string(refusals[i].fixture) + offered + ".json");
        now += 16 * 60;
        Run run = carryOnce(world, now, false, refusals[i].cut);
        run.console.print("|");
        note(!run.report.applied_an_update, "nothing was installed");
        note(run.console.said(refusals[i].needle), "and the reason is said by name");
        note(slurp(ran) == running + "\n", "THE BOARD STARTED THE VERSION IT HAD: " + running);
        note(nowHolding(world).detector_version == running, "the state still names " + running);
        note(!launcher::directoryExists(world.layout().staging_dir) &&
                 !launcher::fileExists(world.layout().download_file),
             "and NOTHING IS HALF-INSTALLED: no staging directory and no part-downloaded file");
        note(nowHolding(world).previous_version == before.previous_version,
             "  and what was kept is still what was kept");
    }
}

int main(int argc, char **argv)
{
    if (argc < 6)
    {
        std::cerr << "usage: i1306_update_check <fixtures> <work> <www> <accesslog> <port>" << std::endl;
        return 2;
    }
    World world;
    world.fixtures = argv[1];
    world.work = argv[2];
    world.www = argv[3];
    world.log = argv[4];
    world.port = std::atoi(argv[5]);

    theMomentRule();
    whatFailedToStartMeans();
    theVersionOrdering();
    theStateFile(world.work);

    const std::string first = argc > 6 ? argv[6] : "v1.0.0";
    const std::string second = argc > 7 ? argv[7] : "v1.1.0";
    const std::string third = argc > 8 ? argv[8] : "v1.2.0";
    const std::string bad = argc > 9 ? argv[9] : "v1.3.0";

    theBoardLives(world, first, second, third, bad);
    theRefusals(world, second, third);

    std::cout << std::endl << failures << " failed" << std::endl;
    return failures == 0 ? 0 : 1;
}
