#pragma once
// #1306: the risky half. Fetching a release the manifest named, proving it is ours, and
// putting it in place without ever leaving the board unable to start.
//
// ORDER IS THE WHOLE DESIGN, so it is written here before any code. Every step below is
// arranged so that STOPPING AT IT leaves a board that starts the version it already had.
// There is no step that is half a swap.
//
//   1. is this launcher allowed to install this?   minimumLauncher, ADR-0077 §1
//   2. is the stated size sane?                    so an absurd download is refused
//                                                  BEFORE it is made, not after
//   3. clear update\staging and update\download.zip  an interrupted earlier attempt
//                                                  leaves bytes; they are never inherited
//   4. fetch the artefact INTO MEMORY              nothing is written yet, so a download
//                                                  cut halfway has written nothing
//   5. length, then SHA-256, against the manifest  the signed chain closes here:
//                                                  signature -> digest -> these bytes
//   6. only now, write update\download.zip
//   7. unpack it into update\staging
//   8. is opendartboard.exe really in there?       a well-signed zip of the wrong thing
//                                                  is still refused
//   9. clear update\previous, rename the CURRENT   a rename, never an overwrite: the
//      opendartboard.exe into it                   fifth criterion, see moveFile()
//  10. rename the staged one into place
//  11. write the state: what is installed, what is kept
//
// A failure at 1-8 has touched nothing outside update\, and the board starts what it had.
// A failure at 9 has moved the detector out of the way and nothing in: the code below
// puts it straight back, and if even that fails it says so in the loudest sentence this
// program has. A failure at 10 is the same case and recovered the same way. Nothing after
// 10 can fail in a way that stops a board starting -- the state file is a memory, and a
// launcher that cannot write it forgets rather than breaks.
//
// THE URL IS UNTRUSTED AND THAT IS NOT A WEAKNESS (ADR-0077 §6). The manifest is signed
// and carries the artefact's SHA-256, so a hostile answer at that address fails step 5, a
// truncated one fails step 5, and the wrong release fails step 5. What the address gets
// to decide is whether the download happens at all.
//
// WHAT UNPACKS IT, AND WHY THAT IS NOT A NEW DEPENDENCY. The release zip is what
// release.yml's Compress-Archive wrote, so its entries are deflated and a stored-only
// reader would not do. Rather than vendor an inflate into the one program whose failure
// mode is a board that will not start, the unpacking is handed to `tar.exe` in
// %SystemRoot%\System32 -- bsdtar, in-box on every Windows 10 since 1803, which reads zip.
// It is named by its absolute path and never found on PATH, so nothing planted beside the
// install can stand in for it. It is run on bytes that have ALREADY passed step 5, which
// is what makes handing them to anything at all defensible: they are the bytes we signed.
// And it costs the artefact nothing -- no link, no DLL, so #1299's one-file property and
// the dumpbin census in release.yml are untouched.
//
// The POSIX branch of the same seam names python3's zipfile module. It exists so that
// testers/i1306_check.sh can measure every step above on the platform this repository's
// harnesses run on; CMake builds the launcher on WIN32 only (ADR-0077, "Scope"), so it
// ships nowhere.

#include "../update/manifest.hpp"
#include "../update/sha256.hpp"
#include "../update/update_check.hpp"
#include "../utils/console_prompt.hpp"
#include "command_line.hpp"
#include "install_layout.hpp"

#include <cstdlib>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

#ifdef _WIN32
#include "../utils/od_platform_first.hpp"
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace launcher
{
    using console_prompt::Text;

    /** No release this project builds is anywhere near this. A sanity ceiling, not a limit. */
    const long long kLargestSensibleArtefact = 512LL * 1024 * 1024;

    // ------------------------------------------------- is this launcher new enough (§1)

    /**
     * ADR-0077 §1: "a manifest states the oldest launcher that can install it; a launcher
     * below that line says so, in Finnish and English, and does not try."
     *
     * THIS IS THE ONE PLACE IN THIS PROGRAM THAT ORDERS TWO VERSIONS, and it is worth
     * knowing why it is not a contradiction of the rule beside it. What is updated is
     * decided by DIFFERENCE and never by ordering, so nothing can disagree about v1.10
     * next to v1.9 and publishing an earlier release stays the way a bad one is undone.
     * `minimumLauncher` is a different question -- "is the program doing the installing
     * old enough to get it wrong" -- and there is no answer to that which is not an
     * ordering. So it is done here, once, over leading dotted integers, with a leading `v`
     * and any `-suffix` ignored: v1.4.2, 1.4.2 and 1.4.2-rc1 are all 1.4.2.
     *
     * ANYTHING THAT DOES NOT PARSE IS A REFUSAL, and that is the safe direction: a
     * launcher that refuses leaves a board on the version it has and a person can fix it,
     * where a launcher that installs something it cannot reason about is the fault this
     * whole slice exists to prevent. A build with no version -- 0.0.0-dev -- is below
     * every real minimum and is refused by the same rule rather than by an exception.
     */
    inline bool versionNumbers(const std::string &version, std::vector<long long> &out)
    {
        out.clear();
        size_t i = 0;
        if (i < version.size() && (version[i] == 'v' || version[i] == 'V'))
        {
            i++;
        }
        std::string number;
        for (; i <= version.size(); i++)
        {
            const char c = i < version.size() ? version[i] : '\0';
            if (c >= '0' && c <= '9')
            {
                number += c;
                continue;
            }
            if (number.empty())
            {
                return false; // ".." , "v-rc1", "" -- nothing to compare
            }
            out.push_back(std::atoll(number.c_str()));
            number.clear();
            if (c == '.')
            {
                continue;
            }
            break; // '-', '+', end of string: the rest is a suffix and is ignored
        }
        return !out.empty();
    }

    /** True when `version` is at least `minimum`. False when either cannot be read. */
    inline bool versionIsAtLeast(const std::string &version, const std::string &minimum)
    {
        std::vector<long long> mine;
        std::vector<long long> theirs;
        if (!versionNumbers(version, mine) || !versionNumbers(minimum, theirs))
        {
            return false;
        }
        for (size_t i = 0; i < mine.size() || i < theirs.size(); i++)
        {
            const long long a = i < mine.size() ? mine[i] : 0;
            const long long b = i < theirs.size() ? theirs[i] : 0;
            if (a != b)
            {
                return a > b;
            }
        }
        return true;
    }

    // ------------------------------------------------------------------ running a tool

    /**
     * Run a program and wait for its exit code. Nothing is captured and nothing is piped:
     * the tool inherits whatever console the launcher has, exactly as the detector does
     * (#1303), so there is no redirection anywhere in this program for #1258's rule to
     * trip over. `tar -x` says nothing on success.
     */
    inline bool runTool(const std::vector<std::string> &argv)
    {
        if (argv.empty())
        {
            return false;
        }
#ifdef _WIN32
        std::vector<std::string> rest(argv.begin() + 1, argv.end());
        std::string line = buildCommandLine(argv[0], rest);
        std::vector<char> writable(line.begin(), line.end());
        writable.push_back('\0');
        STARTUPINFOA startup;
        ::ZeroMemory(&startup, sizeof(startup));
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process;
        ::ZeroMemory(&process, sizeof(process));
        if (::CreateProcessA(argv[0].c_str(), &writable[0], NULL, NULL, TRUE, 0, NULL, NULL, &startup, &process) == 0)
        {
            return false;
        }
        ::CloseHandle(process.hThread);
        ::WaitForSingleObject(process.hProcess, INFINITE);
        DWORD code = 1;
        ::GetExitCodeProcess(process.hProcess, &code);
        ::CloseHandle(process.hProcess);
        return code == 0;
#else
        std::vector<char *> raw;
        for (size_t i = 0; i < argv.size(); i++)
        {
            raw.push_back(const_cast<char *>(argv[i].c_str()));
        }
        raw.push_back(NULL);
        const pid_t child = ::fork();
        if (child < 0)
        {
            return false;
        }
        if (child == 0)
        {
            ::execv(argv[0].c_str(), &raw[0]);
            ::_exit(127);
        }
        int status = 0;
        while (::waitpid(child, &status, 0) < 0)
        {
        }
        return WIFEXITED(status) && WEXITSTATUS(status) == 0;
#endif
    }

    /** How a verified zip is opened. A seam, so a harness can refuse or corrupt one. */
    typedef std::function<bool(const std::string &archive, const std::string &into)> Unpack;

    /** The real one. The header says why it is a tool and why it is named absolutely. */
    inline bool unpackWithSystemTool(const std::string &archive, const std::string &into)
    {
        std::vector<std::string> argv;
#ifdef _WIN32
        char root[MAX_PATH] = {0};
        const UINT written = ::GetSystemDirectoryA(root, MAX_PATH);
        if (written == 0 || written >= MAX_PATH)
        {
            return false;
        }
        argv.push_back(std::string(root) + "\\tar.exe");
        argv.push_back("-x");
        argv.push_back("-f");
        argv.push_back(archive);
        argv.push_back("-C");
        argv.push_back(into);
#else
        argv.push_back("/usr/bin/python3");
        argv.push_back("-m");
        argv.push_back("zipfile");
        argv.push_back("-e");
        argv.push_back(archive);
        argv.push_back(into);
#endif
        return runTool(argv);
    }

    // ------------------------------------------------------------------ fetching bytes

    /** How the artefact's bytes are fetched. Handed in, so a harness can cut one in half. */
    typedef std::function<odhttp::Response(const std::string &url)> FetchArtefact;

    /**
     * The real one: one GET at the address the verified manifest named, no credential, and
     * a read timeout that suits 38 MB over a pub's connection rather than a JSON document.
     */
    inline odhttp::Response fetchArtefactOverHttp(const std::string &url)
    {
        odhttp::Url parsed = odhttp::parseUrl(url);
        if (!parsed.valid)
        {
            odhttp::Response response;
            response.transport_error = "malformed artefact url";
            return response;
        }
        // parseUrl puts everything after the host into path_prefix, so the path is already
        // whole and the second argument adds nothing to it.
        return odhttp::get(parsed, std::string(), /*connect*/ 10, /*read*/ 600);
    }

    // ------------------------------------------------------------------ what happened

    /** The step an application stopped at. Every one is a different sentence to a tester. */
    enum class Step
    {
        Applied,
        LauncherTooOld,
        SizeRefused,
        CouldNotReach,
        WrongLength,
        DigestMismatch,
        CouldNotStage,
        CouldNotUnpack,
        NothingToInstall,
        SwapFailed,
        SwapFailedAndPutBack,
    };

    struct Application
    {
        Step step = Step::Applied;
        bool ok = false;
        std::string detail;       // a number, a path, a digest. Never bytes from the wire.
        std::string from_version; // what was running before
        std::string to_version;   // what the manifest named
        /** True only for the one case a board may not be left in: the detector is gone. */
        bool detector_is_missing = false;
    };

    inline Application stoppedAt(Step step, const std::string &detail = "")
    {
        Application application;
        application.step = step;
        application.ok = false;
        application.detail = detail;
        return application;
    }

    // ------------------------------------------------------------------ the application

    /**
     * Apply the update `answer` names. Returns what happened and, on success, leaves the
     * state describing an install whose detector is the new version and whose
     * `update\previous` holds the old one.
     *
     * It is never called unless decideMoment() said so and update_check::ask() came back
     * Available. Those two are the only doors, and both are in launcher.hpp.
     */
    inline Application applyUpdate(const Layout &layout, const update_check::Answer &answer,
                                   const std::string &launcher_version, State &state,
                                   const FetchArtefact &fetch, const Unpack &unpack)
    {
        Application application;
        application.from_version = answer.running_version;
        application.to_version = answer.published_version;

        // 1. §1: is this launcher allowed to install this release at all?
        if (!versionIsAtLeast(launcher_version, answer.published_minimum_launcher))
        {
            Application refused = stoppedAt(Step::LauncherTooOld, answer.published_minimum_launcher);
            refused.from_version = application.from_version;
            refused.to_version = application.to_version;
            return refused;
        }

        // 2. An absurd size is refused before a byte is asked for.
        if (answer.published_size <= 0 || answer.published_size > kLargestSensibleArtefact)
        {
            Application refused = stoppedAt(Step::SizeRefused, std::to_string(answer.published_size));
            refused.from_version = application.from_version;
            refused.to_version = application.to_version;
            return refused;
        }

        // 3. Nothing an earlier attempt left is ever inherited.
        removeTree(layout.staging_dir);
        std::remove(layout.download_file.c_str());

        // 4. Into memory. Nothing on this board has been written yet.
        odhttp::Response response = fetch(answer.published_url);
        if (!response.reached_a_server())
        {
            Application refused = stoppedAt(Step::CouldNotReach, response.transport_error.empty()
                                                                     ? std::string("nothing answered")
                                                                     : response.transport_error);
            refused.from_version = application.from_version;
            refused.to_version = application.to_version;
            return refused;
        }
        if (response.status != 200)
        {
            Application refused = stoppedAt(Step::CouldNotReach, "HTTP " + std::to_string(response.status));
            refused.from_version = application.from_version;
            refused.to_version = application.to_version;
            return refused;
        }

        // 5. The signed chain closes: signature -> digest -> these bytes. The length is
        //    asked first because it is the cheap half of the same question and because it
        //    is what a cut-off download fails on, which is a clearer sentence than a
        //    digest mismatch for the commonest way this goes wrong.
        if (static_cast<long long>(response.body.size()) != answer.published_size)
        {
            // The two numbers, not a sentence: `detail` is read by two languages and a
            // word joining them in one of them reads as a typo in the other.
            Application refused = stoppedAt(Step::WrongLength, std::to_string(response.body.size()) + " / " +
                                                                   std::to_string(answer.published_size));
            refused.from_version = application.from_version;
            refused.to_version = application.to_version;
            return refused;
        }
        const std::string digest = od_sha256::hex(response.body);
        if (digest != answer.published_sha256)
        {
            Application refused = stoppedAt(Step::DigestMismatch, digest);
            refused.from_version = application.from_version;
            refused.to_version = application.to_version;
            return refused;
        }

        // 6. Only now does anything reach the disk, and only inside update\.
        if (!ensureDirectory(layout.update_dir) || !ensureDirectory(layout.staging_dir))
        {
            Application refused = stoppedAt(Step::CouldNotStage, layout.update_dir + " (" + lastFileError() + ")");
            refused.from_version = application.from_version;
            refused.to_version = application.to_version;
            return refused;
        }
        {
            std::ofstream out(layout.download_file.c_str(), std::ios::binary | std::ios::trunc);
            if (!out)
            {
                Application refused = stoppedAt(Step::CouldNotStage, layout.download_file);
                refused.from_version = application.from_version;
                refused.to_version = application.to_version;
                return refused;
            }
            out.write(response.body.data(), static_cast<std::streamsize>(response.body.size()));
            out.flush();
            if (!out.good())
            {
                out.close();
                std::remove(layout.download_file.c_str());
                Application refused = stoppedAt(Step::CouldNotStage, layout.download_file + " (short write)");
                refused.from_version = application.from_version;
                refused.to_version = application.to_version;
                return refused;
            }
        }

        // 7 and 8. Unpack, then insist the one file this install needs is really in there.
        if (!unpack(layout.download_file, layout.staging_dir))
        {
            removeTree(layout.staging_dir);
            std::remove(layout.download_file.c_str());
            Application refused = stoppedAt(Step::CouldNotUnpack, layout.download_file);
            refused.from_version = application.from_version;
            refused.to_version = application.to_version;
            return refused;
        }
        std::remove(layout.download_file.c_str());
        const std::string staged = under(layout.staging_dir, kDetectorFileName);
        if (!fileExists(staged))
        {
            removeTree(layout.staging_dir);
            Application refused = stoppedAt(Step::NothingToInstall, std::string(kDetectorFileName));
            refused.from_version = application.from_version;
            refused.to_version = application.to_version;
            return refused;
        }
        makeExecutable(staged);

        // 9. The version that worked is kept, whole, under its own name. The move is a
        //    RENAME, which is what survives the file being locked by a detector that has
        //    not finished exiting.
        removeTree(layout.previous_dir);
        if (!ensureDirectory(layout.previous_dir))
        {
            removeTree(layout.staging_dir);
            Application refused = stoppedAt(Step::CouldNotStage, layout.previous_dir + " (" + lastFileError() + ")");
            refused.from_version = application.from_version;
            refused.to_version = application.to_version;
            return refused;
        }
        const bool had_a_detector = fileExists(layout.detector);
        if (had_a_detector && !moveFile(layout.detector, layout.previous_exe))
        {
            removeTree(layout.staging_dir);
            Application refused = stoppedAt(Step::SwapFailed, layout.detector + " (" + lastFileError() + ")");
            refused.from_version = application.from_version;
            refused.to_version = application.to_version;
            return refused;
        }

        // 10. And the new one in. This is the only moment a board has no detector, and it
        //     is one rename wide. If it fails, the old one goes straight back.
        if (!moveFile(staged, layout.detector))
        {
            const std::string why = lastFileError();
            if (had_a_detector && moveFile(layout.previous_exe, layout.detector))
            {
                removeTree(layout.previous_dir);
                removeTree(layout.staging_dir);
                Application refused = stoppedAt(Step::SwapFailedAndPutBack, why);
                refused.from_version = application.from_version;
                refused.to_version = application.to_version;
                return refused;
            }
            removeTree(layout.staging_dir);
            Application refused = stoppedAt(Step::SwapFailed, why);
            refused.from_version = application.from_version;
            refused.to_version = application.to_version;
            refused.detector_is_missing = !fileExists(layout.detector);
            return refused;
        }
        removeTree(layout.staging_dir);

        // 11. What is installed and what is kept. A version that has just been installed
        //     has no failed starts against it, whatever the one it replaced had.
        state.previous_version = had_a_detector ? answer.running_version : std::string();
        state.detector_version = answer.published_version;
        state.failed_starts = 0;
        application.ok = true;
        application.step = Step::Applied;
        return application;
    }

    /**
     * Go back to the version that worked. Renames again, both ways, so it is subject to
     * the same locked-file argument as the swap and to no other.
     *
     * The failed version is NOT kept. There is one previous and it is the one that
     * started; a board that has rolled back has nothing to roll back to, which is why the
     * launcher says which version is running and why rather than trying again for ever.
     */
    inline bool rollBack(const Layout &layout, State &state)
    {
        if (!hasSomethingToGoBackTo(state) || !fileExists(layout.previous_exe))
        {
            return false;
        }
        const std::string failed = under(layout.update_dir, "failed-" + std::string(kDetectorFileName));
        std::remove(failed.c_str());
        if (fileExists(layout.detector) && !moveFile(layout.detector, failed))
        {
            return false;
        }
        if (!moveFile(layout.previous_exe, layout.detector))
        {
            // Put the failed one back rather than leave a board with no detector at all.
            // A board that will not start and says why is bad; a board with no program
            // beside the launcher is worse, and unrecoverable without a download.
            moveFile(failed, layout.detector);
            return false;
        }
        std::remove(failed.c_str());
        removeTree(layout.previous_dir);
        state.detector_version = state.previous_version;
        state.previous_version.clear();
        state.failed_starts = 0;
        return true;
    }

    // ------------------------------------------------------------------ the words

    /** Why an update was not applied, in one sentence a tester can act on. */
    inline Text applicationText(const Application &application)
    {
        const std::string to = application.to_version;
        switch (application.step)
        {
        case Step::Applied:
            return {"Versio " + to + " asennettiin. Edellinen versio " + application.from_version +
                        " säilytettiin siltä varalta, ettei uusi käynnisty.",
                    "Version " + to + " was installed. The previous version " + application.from_version +
                        " was kept in case the new one does not start."};
        case Step::LauncherTooOld:
            return {"Julkaisu " + to + " vaatii käynnistysohjelman " + application.detail +
                        " tai uudemman, eikä tämä ole sellainen. Uusi käynnistysohjelma haetaan käsin.",
                    "Release " + to + " needs launcher " + application.detail +
                        " or newer and this is not one. A new launcher is fetched by hand."};
        case Step::SizeRefused:
            return {"Julkaisun ilmoitettu koko (" + application.detail + " tavua) ei ole uskottava, joten sitä ei "
                                                                         "haettu.",
                    "The release states a size (" + application.detail + " bytes) that is not credible, so it was "
                                                                         "not fetched."};
        case Step::CouldNotReach:
            return {"Julkaisua ei saatu haettua: " + application.detail + ".",
                    "The release could not be fetched: " + application.detail + "."};
        case Step::WrongLength:
            return {"Lataus katkesi: saatiin " + application.detail + " tavua.",
                    "The download was cut short: " + application.detail + " bytes arrived."};
        case Step::DigestMismatch:
            return {"LADATUN TIEDOSTON TARKISTUSSUMMA EI TÄSMÄÄ allekirjoitettuun päivitystiedostoon, joten "
                    "tavut eivät ole ne jotka allekirjoitettiin. Tiedosto hylättiin.",
                    "THE DOWNLOADED FILE'S CHECKSUM DOES NOT MATCH the signed manifest, so these are not the bytes "
                    "that were signed. The file was discarded."};
        case Step::CouldNotStage:
            return {"Päivitystä ei voitu valmistella: " + application.detail +
                        ". Onko asennushakemistoon kirjoitusoikeus?",
                    "The update could not be staged: " + application.detail +
                        ". Is the install directory writable?"};
        case Step::CouldNotUnpack:
            return {"Ladattua julkaisupakettia ei saatu purettua.", "The downloaded release could not be unpacked."};
        case Step::NothingToInstall:
            return {"Julkaisupaketissa ei ollut tiedostoa " + application.detail + ", joten siinä ei ole mitään "
                                                                                   "asennettavaa.",
                    "The release package holds no " + application.detail + ", so there is nothing in it to install."};
        case Step::SwapFailedAndPutBack:
            return {"Vaihto ei onnistunut (" + application.detail + "), ja edellinen versio palautettiin paikalleen.",
                    "The swap did not work (" + application.detail + "), and the previous version was put back."};
        case Step::SwapFailed:
        default:
            return {"VAIHTO EI ONNISTUNUT (" + application.detail + "). Ilmoita tämä ikkuna sellaisenaan.",
                    "THE SWAP DID NOT WORK (" + application.detail + "). Report this window as it is."};
        }
    }

    /** Every line said about one application: what happened, then what the board will do. */
    inline std::vector<Text> applicationLines(const Application &application)
    {
        std::vector<Text> said;
        said.push_back(applicationText(application));
        if (!application.ok)
        {
            if (application.detector_is_missing)
            {
                said.push_back({"Taulussa ei nyt ole ohjelmaa lainkaan. Asenna julkaisu uudelleen käsin.",
                                "This board now has no program at all. Install the release again by hand."});
            }
            else
            {
                said.push_back({"Taulu käynnistyy versiolla " + application.from_version + ", joka sillä jo oli.",
                                "The board starts version " + application.from_version + ", the one it already had."});
            }
        }
        return said;
    }
}
