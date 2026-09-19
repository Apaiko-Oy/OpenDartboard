// #1306: the four questions only real Windows can answer, asked of the real code.
//
// testers/i1306_check.sh measures the whole slice in the Linux container -- 54 checks
// over real sockets, real signatures, real zips and real child processes. Four things it
// cannot measure there, and every one of them is a reason this slice exists at all:
//
//   1. A RUNNING .exe CANNOT BE OVERWRITTEN. That is the fifth criterion and it is the
//      whole reason the swap lives in the launcher rather than in the detector. Linux
//      refuses the write too (ETXTBSY), so the shape can be rehearsed there -- but the
//      file this is really about is a Windows .exe with its image mapped, and the
//      recovery is MoveFileExA, which does not exist there.
//   2. THE SIGNATURE IS VERIFIED BY CNG. ADR-0077 §2 chose BCryptVerifySignature
//      precisely so the artefact vendors no cryptography, and the Linux harness verifies
//      through p256_verify.hpp instead -- the other half of manifest.hpp. Every manifest
//      read below goes through the half that ships.
//   3. THE ZIP IS UNPACKED BY %SystemRoot%\tar.exe, on an archive Compress-Archive wrote,
//      which is what release.yml produces.
//   4. THE DETECTOR IS STARTED BY CreateProcessA and classified from a Windows exit code.
//
// WHAT IS SUBSTITUTED HERE. The artefact's bytes come from a file rather than from a
// socket, because a signed manifest's url must be https (manifest.hpp refuses anything
// else, rightly) and this harness has no certificate anybody would accept. The socket is
// measured on Linux; what is measured here is everything the bytes then go through. The
// substitution is one lambda, below, and it hands over the file the manifest names.
//
//   i1306_windows_check.exe <fixtures> <work> <vA> <vB> <vC> <vBAD>

#include "utils/od_platform_first.hpp"

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

static std::string slurp(const std::string &path)
{
    std::ifstream in(path.c_str(), std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

/** The file object itself, not its name. Two names for one identity compare equal. */
static std::string identityOf(const std::string &path)
{
    HANDLE handle = ::CreateFileA(path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                                  OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (handle == INVALID_HANDLE_VALUE)
    {
        return std::string();
    }
    BY_HANDLE_FILE_INFORMATION info;
    const BOOL got = ::GetFileInformationByHandle(handle, &info);
    ::CloseHandle(handle);
    if (!got)
    {
        return std::string();
    }
    char text[64];
    std::snprintf(text, sizeof(text), "%08lX:%08lX:%08lX", (unsigned long)info.dwVolumeSerialNumber,
                  (unsigned long)info.nFileIndexHigh, (unsigned long)info.nFileIndexLow);
    return std::string(text);
}

static std::string basenameOf(const std::string &path)
{
    const size_t cut = path.find_last_of("/\\");
    return cut == std::string::npos ? path : path.substr(cut + 1);
}

// ------------------------------------------------------------------ the locked file

/**
 * THE FIFTH CRITERION, with its own positive control.
 *
 * A launcher that could not do this would be a launcher that cannot update anything, and
 * the way that failure would present is a board stuck for ever on one version with a
 * sharing violation nobody reads. So it is measured in three parts, and the first is what
 * makes the other two mean anything: the file really is locked. Without it, "the rename
 * worked" is satisfied by a file nothing had open.
 */
static void aFileNobodyHasFinishedWith(const std::string &work, const std::string &stubs, const std::string &first,
                                       const std::string &second)
{
    std::cout << std::endl
              << "---- the swap, on a file a process has not finished with ----" << std::endl;
    const std::string install = work + "\\locked";
    launcher::removeTree(install);
    launcher::ensureDirectory(install);
    const std::string detector = install + "\\opendartboard.exe";
    const std::string incoming = install + "\\incoming.exe";
    {
        std::ofstream out(detector.c_str(), std::ios::binary | std::ios::trunc);
        const std::string body = slurp(stubs + "\\stub-" + first + ".exe");
        out.write(body.data(), (std::streamsize)body.size());
    }
    {
        std::ofstream out(incoming.c_str(), std::ios::binary | std::ios::trunc);
        const std::string body = slurp(stubs + "\\stub-" + second + ".exe");
        out.write(body.data(), (std::streamsize)body.size());
    }
    const std::string was = identityOf(detector);

    // Start it and leave it running. Four seconds is longer than everything below.
    ::SetEnvironmentVariableA("OD_STUB_LINGER", "4000");
    std::string line = launcher::buildCommandLine(detector, std::vector<std::string>());
    std::vector<char> writable(line.begin(), line.end());
    writable.push_back('\0');
    STARTUPINFOA startup;
    ::ZeroMemory(&startup, sizeof(startup));
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process;
    ::ZeroMemory(&process, sizeof(process));
    const BOOL started =
        ::CreateProcessA(detector.c_str(), &writable[0], NULL, NULL, TRUE, 0, NULL, NULL, &startup, &process);
    ::SetEnvironmentVariableA("OD_STUB_LINGER", NULL);
    note(started != 0, "the old version is running");
    if (!started)
    {
        return;
    }
    ::Sleep(300); // it is up, and it has not finished exiting

    // 1. THE CONTROL. Overwriting it must be refused, or nothing below is a measurement.
    HANDLE overwrite = ::CreateFileA(detector.c_str(), GENERIC_WRITE, 0, NULL, TRUNCATE_EXISTING,
                                     FILE_ATTRIBUTE_NORMAL, NULL);
    const DWORD refusal = ::GetLastError();
    if (overwrite != INVALID_HANDLE_VALUE)
    {
        ::CloseHandle(overwrite);
    }
    note(overwrite == INVALID_HANDLE_VALUE,
         "CONTROL: while it runs, its own file CANNOT BE OVERWRITTEN (Windows " + std::to_string((unsigned long)refusal) +
             ") -- which is why this lives in the launcher");

    // 2. And the rename is allowed anyway, which is what the swap is built out of.
    const std::string moved = install + "\\update\\previous\\opendartboard.exe";
    launcher::ensureDirectory(install + "\\update\\previous");
    note(launcher::moveFile(detector, moved), "the SAME LOCKED FILE renames out of the way");
    note(identityOf(moved) == was && !was.empty(),
         "  and it is the same file object, not a copy: " + was);
    note(launcher::moveFile(incoming, detector), "and the new version renames into its place");

    // 3. The process that was running is untouched and still ends its own way.
    ::WaitForSingleObject(process.hProcess, 10000);
    DWORD code = 1;
    ::GetExitCodeProcess(process.hProcess, &code);
    ::CloseHandle(process.hThread);
    ::CloseHandle(process.hProcess);
    note(code == 0, "the process that had not finished exiting finished, undisturbed, with its own exit code");

    // And the install is now the new version, which is what the whole dance was for.
    launcher::Outcome outcome = launcher::runAndWait(detector, std::vector<std::string>());
    note(outcome.started && outcome.code == 0, "the board now starts, and what starts is the file that was moved in");
    note(identityOf(detector) != was, "  a different file object from the one that was running a moment ago");
}

// ------------------------------------------------------------------ the whole chain

struct Windows
{
    std::string fixtures;
    std::string work;
    std::string install() const { return work + "\\install"; }
    std::string detector() const { return install() + "\\opendartboard.exe"; }
    launcher::Layout layout() const { return launcher::layoutFor(detector()); }
};

static launcher::Surroundings surroundingsFor(const Windows &world, const std::string &manifest, long long now)
{
    launcher::Surroundings surroundings;
    surroundings.layout = world.layout();
    surroundings.address = "http://127.0.0.1:1"; // never asked: the fetch below is the seam
    surroundings.channel = "stable";
    std::string anchor = slurp(world.fixtures + "\\anchor.hex");
    while (!anchor.empty() && (anchor[anchor.size() - 1] == '\n' || anchor[anchor.size() - 1] == '\r'))
    {
        anchor.erase(anchor.size() - 1);
    }
    surroundings.anchors.push_back(update_manifest::anchorFromHex(anchor));

    const std::string body = slurp(world.fixtures + "\\" + manifest);
    surroundings.fetch_manifest = [body](const std::string &, const std::string &)
    {
        odhttp::Response response;
        response.status = 200;
        response.body = body;
        return response;
    };
    const std::string releases = world.fixtures + "\\rel";
    surroundings.fetch_artefact = [releases](const std::string &url)
    {
        odhttp::Response response;
        response.status = 200;
        response.body = slurp(releases + "\\" + basenameOf(url));
        return response;
    };
    surroundings.unpack = launcher::unpackWithSystemTool; // the real System32\tar.exe
    surroundings.clock = [now]() { return now; };
    return surroundings;
}

class Quiet : public console_prompt::Console
{
public:
    void say(const console_prompt::Text &text) override { lines.push_back(text.en); }
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
    void print() const
    {
        for (size_t i = 0; i < lines.size(); i++)
        {
            std::cout << "     | " << lines[i] << std::endl;
        }
    }
    std::vector<std::string> lines;
};

int main(int argc, char **argv)
{
    if (argc < 7)
    {
        std::cerr << "usage: i1306_windows_check <fixtures> <work> <vA> <vB> <vC> <vBAD>" << std::endl;
        return 2;
    }
    Windows world;
    world.fixtures = argv[1];
    world.work = argv[2];
    const std::string first = argv[3], second = argv[4], third = argv[5], bad = argv[6];
    const std::string stubs = world.work + "\\stubs";
    const std::string ran = world.work + "\\ran.txt";

    ::SetEnvironmentVariableA("OD_STUB_RAN_TO", ran.c_str());
    ::SetEnvironmentVariableA("OD_STUB_BAD", bad.c_str());

    // ---- tar.exe reads what Compress-Archive wrote -----------------------------------
    std::cout << "---- the in-box unpacker, on an archive Compress-Archive wrote ----" << std::endl;
    const std::string into = world.work + "\\tarprobe";
    launcher::removeTree(into);
    launcher::ensureDirectory(into);
    const std::string archive = world.fixtures + "\\rel\\opendartboard-" + first + ".zip";
    note(launcher::fileExists(archive), "the archive is there: " + archive);
    note(launcher::unpackWithSystemTool(archive, into), "%SystemRoot%\\System32\\tar.exe unpacks it");
    note(launcher::fileExists(into + "\\opendartboard.exe"), "  and opendartboard.exe is in what came out");
    note(launcher::fileExists(into + "\\LICENSE") && launcher::fileExists(into + "\\BUILD-INFO.txt"),
         "  along with the licence and the build information a release carries (#808)");

    aFileNobodyHasFinishedWith(world.work, stubs, first, second);

    // ---- and the whole chain, on this platform ----------------------------------------
    launcher::removeTree(world.install());
    launcher::ensureDirectory(world.install());
    {
        std::ofstream out(world.detector().c_str(), std::ios::binary | std::ios::trunc);
        const std::string body = slurp(stubs + "\\stub-" + first + ".exe");
        out.write(body.data(), (std::streamsize)body.size());
    }
    long long now = 1758300000;
    std::vector<std::string> arguments;
    arguments.push_back("--cams");
    arguments.push_back("0,1,2");

    std::cout << std::endl << "---- 1. " << first << " is installed, and " << second << " is published ----" << std::endl;
    std::remove(ran.c_str());
    const std::string was = identityOf(world.detector());
    Quiet one;
    launcher::Report r1 = launcher::carry(arguments, one, false, launcher::runAndWait,
                                          surroundingsFor(world, "stable-" + second + ".json", now));
    one.print();
    note(r1.applied_an_update && r1.ran_version == second, "the update was applied and " + second + " ran");
    note(slurp(ran) == second + "\r\n" || slurp(ran) == second + "\n",
         "  and the version that really ran, as the program itself recorded it, is " + second);
    note(identityOf(world.layout().previous_exe) == was && !was.empty(),
         "the kept version is the same file object the install had: the swap was a rename");

    std::cout << std::endl << "---- 2. four minutes later ----" << std::endl;
    now += 4 * 60;
    Quiet two;
    launcher::Report r2 = launcher::carry(arguments, two, false, launcher::runAndWait,
                                          surroundingsFor(world, "stable-" + third + ".json", now));
    note(!r2.looked_for_an_update && r2.moment == launcher::Moment::QuickRestart,
         "ADR-0077 §7: a quick restart does not look at all");

    std::cout << std::endl << "---- 3. sixteen minutes later: two updates in a row ----" << std::endl;
    std::remove(ran.c_str());
    now += 16 * 60;
    Quiet three;
    launcher::Report r3 = launcher::carry(arguments, three, false, launcher::runAndWait,
                                          surroundingsFor(world, "stable-" + third + ".json", now));
    three.print();
    note(r3.applied_an_update && r3.ran_version == third,
         "TWO UPDATES IN A ROW on real Windows: " + first + " -> " + second + " -> " + third);
    note(launcher::readState(world.layout().state_file).previous_version == second,
         "  and what is kept is " + second);

    std::cout << std::endl << "---- 4. " << second << ", superseded, is published again ----" << std::endl;
    now += 16 * 60;
    Quiet four;
    launcher::Report r4 = launcher::carry(arguments, four, false, launcher::runAndWait,
                                          surroundingsFor(world, "stable-" + second + ".json", now));
    note(r4.applied_an_update && r4.ran_version == second,
         "a superseded release installs like any other -- nothing orders two versions");

    std::cout << std::endl << "---- 5. " << bad << " is published, and it will not start ----" << std::endl;
    std::remove(ran.c_str());
    now += 16 * 60;
    Quiet five;
    launcher::Report r5 = launcher::carry(arguments, five, false, launcher::runAndWait,
                                          surroundingsFor(world, "stable-" + bad + ".json", now));
    five.print();
    note(r5.applied_an_update, "it was installed, because nothing could have known");
    note(r5.rolled_back && r5.starts == 3, "IT WENT BACK, after exactly two attempts");
    note(five.said("Going back to version " + second), "  saying which version is running and why");
    note(r5.exit_code == launcher::kRanCleanly, "and the launcher exits on what the restored version did");
    note(launcher::readState(world.layout().state_file).detector_version == second,
         "the state names " + second + " installed");

    std::cout << std::endl << "---- 6. a digest that does not match, verified through CNG ----" << std::endl;
    std::remove(ran.c_str());
    now += 16 * 60;
    Quiet six;
    launcher::Report r6 = launcher::carry(arguments, six, false, launcher::runAndWait,
                                          surroundingsFor(world, "baddigest-" + third + ".json", now));
    six.print();
    note(!r6.applied_an_update && six.said("CHECKSUM DOES NOT MATCH"), "the artefact is discarded by name");
    note(r6.ran_version == second, "and the board starts the version it had");
    note(!launcher::directoryExists(world.layout().staging_dir) && !launcher::fileExists(world.layout().download_file),
         "with nothing half-installed");

    std::cout << std::endl << "---- 7. a manifest signed by somebody else, refused by CNG ----" << std::endl;
    now += 16 * 60;
    Quiet seven;
    launcher::Report r7 = launcher::carry(arguments, seven, false, launcher::runAndWait,
                                          surroundingsFor(world, "stranger-" + third + ".json", now));
    note(!r7.applied_an_update && seven.said("SIGNATURE VERIFICATION FAILED"),
         "BCryptVerifySignature refuses it, and the sentence is the one #1305 wrote");

    std::cout << std::endl << failures << " failed" << std::endl;
    return failures == 0 ? 0 : 1;
}
