#pragma once
// #1306: where an install keeps its two versions, and the six facts the launcher
// remembers between runs.
//
// WHAT IS WHERE, AND WHY NONE OF IT IS BESIDE THE CREDENTIAL. od_paths.hpp put the
// credential in %APPDATA% because the question there was who else on the box can read a
// bearer token (#822). The question here is the opposite one: these files belong to THIS
// INSTALL, not to this Windows account. Two accounts on a pub PC share one
// opendartboard.exe and must share the version it is at; a board whose update state lived
// per-user would roll back for one barman and not the other. So everything below sits in
// the install directory, beside the .exe:
//
//   opendartboard.exe            what the launcher starts
//   update\state.txt             the six facts, below
//   update\previous\             the version that worked, kept whole
//   update\staging\              where an artefact is unpacked before anything is swapped
//   update\download.zip          the verified bytes on their way to staging
//
// THE CONSEQUENCE IS THE SIXTH CRITERION, FOR FREE. `credentials.json`, `cameras.json`
// and `channel.json` are in the config directory and NOTHING on this path writes there --
// not the download, not the swap, not the rollback, not the state. A board that has
// updated pairs with the credential it already held because the update never had a reason
// to touch it, which is a stronger guarantee than remembering to copy it across.
// testers/i1306_inside.sh fingerprints that directory around every case and asserts it
// byte-identical.
//
// THE STATE FILE IS KEY=VALUE AND NOT JSON, ON PURPOSE. Two reasons and the second is the
// load-bearing one. A tester reporting a board that will not start is asked to quote this
// file, and `failed_starts=2` is a line somebody can read down a phone. And a corrupt or
// half-written state file must degrade to "I remember nothing" rather than to a launcher
// that will not run: every field below has a safe default, an unreadable file IS those
// defaults, and there is no parse that can fail. A missing state file is a board that has
// never updated, which is every board today.

#include "command_line.hpp"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

#ifdef _WIN32
#include "../utils/od_platform_first.hpp"
#else
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace launcher
{
    /** The file the launcher starts, beside itself (detector_process.hpp names it too). */
#ifdef _WIN32
    const char *const kDetectorFileName = "opendartboard.exe";
#else
    const char *const kDetectorFileName = "opendartboard";
#endif

    /** Every path this slice touches, derived from one: the detector's own. */
    struct Layout
    {
        std::string detector;      // ...\opendartboard.exe
        std::string install_dir;   // the directory that is in
        std::string update_dir;    // ...\update
        std::string state_file;    // ...\update\state.txt
        std::string previous_dir;  // ...\update\previous
        std::string previous_exe;  // ...\update\previous\opendartboard.exe
        std::string staging_dir;   // ...\update\staging
        std::string download_file; // ...\update\download.zip
        /**
         * #1796: the detector the PACKAGE put on this board, which the launcher may read
         * and copy but never move or write over. Empty on Windows and in every layout
         * layoutFor() makes, where the detector beside the launcher is the one that is
         * swapped; set by layoutUnderStateDir() on a Linux board, where it is dpkg's
         * /usr/local/bin/opendartboard. See that function for what it is for.
         */
        std::string shipped;
    };

    /** A path under a directory, with the platform's separator. */
    inline std::string under(const std::string &directory, const std::string &leaf)
    {
        if (directory.empty())
        {
            return leaf;
        }
#ifdef _WIN32
        return directory + "\\" + leaf;
#else
        return directory + "/" + leaf;
#endif
    }

    inline Layout layoutFor(const std::string &detector_path)
    {
        Layout layout;
        layout.detector = detector_path;
        layout.install_dir = directoryOf(detector_path);
        layout.update_dir = under(layout.install_dir, "update");
        layout.state_file = under(layout.update_dir, "state.txt");
        layout.previous_dir = under(layout.update_dir, "previous");
        layout.previous_exe = under(layout.previous_dir, kDetectorFileName);
        layout.staging_dir = under(layout.update_dir, "staging");
        // #1796: on POSIX the file is named for what it is -- the seam reads a zip or a
        // tar.gz by its first bytes (apply_update.hpp), and a tar.gz called download.zip
        // would be a thing to explain for ever. Windows keeps the name it had.
#ifdef _WIN32
        layout.download_file = under(layout.update_dir, "download.zip");
#else
        layout.download_file = under(layout.update_dir, "download.archive");
#endif
        return layout;
    }

    /**
     * #1796: the layout on a Raspberry Pi, and why it is not "beside the launcher".
     *
     * On Windows the install directory is the launcher's own, because the install IS a
     * directory somebody unzipped. On a Pi the install is a .deb, and the two binaries it
     * puts in /usr/local/bin are dpkg's: a launcher that renamed /usr/local/bin/opendartboard
     * into update/previous would leave `dpkg -V` reporting a missing file for ever, and a
     * later `dpkg -i` would write over whatever the launcher had put there. So the
     * launcher's whole tree lives under the one directory a Linux board already keeps
     * everything in (#1660, od_paths::configDir(): /var/lib/opendartboard, systemd's
     * StateDirectory=), in exactly the Windows shape:
     *
     *   /var/lib/opendartboard/opendartboard            the detector the launcher installed
     *   /var/lib/opendartboard/update/state.txt         the six facts
     *   /var/lib/opendartboard/update/previous/         the version that worked
     *   /var/lib/opendartboard/update/staging/          where the archive is unpacked
     *   /var/lib/opendartboard/update/download.zip      the verified bytes on their way
     *   /usr/local/bin/opendartboard                    `shipped`: the deb's own, read-only
     *
     * The board STARTS /var/lib/opendartboard/opendartboard when it exists and `shipped`
     * when it does not (programToStart()), so a board that has never updated runs the
     * deb's detector, as it did before #1796. The first update copies `shipped` into
     * update/previous -- a copy, because dpkg's file is not ours to move -- so that the
     * very first release a Pi takes over the wire can be rolled back like every later
     * one. credentials.json, channel.json and score_token are in the same directory and
     * nothing on this path writes them (install_layout.hpp's sixth criterion, measured by
     * testers/i1796_inside.sh the way i1306's measures it).
     *
     * And `dpkg -i` of a newer deb: postinst clears this tree, so a board somebody updates
     * by hand is again on the deb's version with nothing kept, and the channel decides
     * from there. One rule either way: what the package put there is what runs until the
     * launcher installs something, and the launcher's own files are the only ones it moves.
     */
    inline Layout layoutUnderStateDir(const std::string &state_dir, const std::string &shipped)
    {
        Layout layout = layoutFor(under(state_dir, kDetectorFileName));
        layout.shipped = shipped;
        return layout;
    }

    // ------------------------------------------------------------------ the six facts

    /**
     * What one launch tells the next. Every field's default is the honest answer for a
     * board that has never updated, so a missing or unreadable file needs no special case.
     *
     * `detector_version` is the one thing here that is not an observation: it is written
     * when an update is applied and names what was installed. It is EMPTY on a board that
     * has never updated, and the launcher then answers with its own APP_VERSION -- which
     * is true by construction of the release zip, because release.yml builds both
     * executables from one tag and packages them together. A tester who replaces
     * opendartboard.exe by hand without the launcher makes that false, and the manifest
     * check then offers them an update they already have; a download and a swap, not a
     * broken board.
     */
    struct State
    {
        std::string detector_version; // what the last applied update installed
        std::string previous_version; // what is kept in update\previous, empty when nothing is
        long long last_started = 0;   // unix seconds, 0 when never
        long long last_stopped = 0;   // unix seconds, 0 when never
        std::string last_ending;      // "cleanly" | "faulted" | "killed" | "never-started" | "scheduled" (#1797) | ""
        int failed_starts = 0;        // consecutive fast bad endings of the version now installed
    };

    /** True when a version that worked is kept and can be gone back to. */
    inline bool hasSomethingToGoBackTo(const State &state) { return !state.previous_version.empty(); }

    namespace state_file
    {
        inline std::string valueOf(const std::string &line)
        {
            const size_t equals = line.find('=');
            if (equals == std::string::npos)
            {
                return std::string();
            }
            std::string value = line.substr(equals + 1);
            while (!value.empty() && (value[value.size() - 1] == '\r' || value[value.size() - 1] == '\n'))
            {
                value.erase(value.size() - 1);
            }
            return value;
        }

        inline bool keyIs(const std::string &line, const char *key)
        {
            const size_t equals = line.find('=');
            return equals != std::string::npos && line.compare(0, equals, key) == 0;
        }
    }

    /** Whatever the file says, and the defaults for everything it does not. Never fails. */
    inline State readState(const std::string &path)
    {
        State state;
        std::ifstream in(path.c_str(), std::ios::binary);
        if (!in)
        {
            return state;
        }
        std::string line;
        while (std::getline(in, line))
        {
            if (state_file::keyIs(line, "detector_version"))
            {
                state.detector_version = state_file::valueOf(line);
            }
            else if (state_file::keyIs(line, "previous_version"))
            {
                state.previous_version = state_file::valueOf(line);
            }
            else if (state_file::keyIs(line, "last_started"))
            {
                state.last_started = std::atoll(state_file::valueOf(line).c_str());
            }
            else if (state_file::keyIs(line, "last_stopped"))
            {
                state.last_stopped = std::atoll(state_file::valueOf(line).c_str());
            }
            else if (state_file::keyIs(line, "last_ending"))
            {
                state.last_ending = state_file::valueOf(line);
            }
            else if (state_file::keyIs(line, "failed_starts"))
            {
                state.failed_starts = std::atoi(state_file::valueOf(line).c_str());
            }
        }
        return state;
    }

    /** mkdir -p, so the launcher can write its state on a board that has never updated. */
    inline bool ensureDirectory(const std::string &path)
    {
        if (path.empty())
        {
            return false;
        }
        std::string built;
        for (size_t i = 0; i <= path.size(); i++)
        {
            if (i == path.size() || path[i] == '/' || path[i] == '\\')
            {
                if (!built.empty() && !(built.size() == 2 && built[1] == ':'))
                {
#ifdef _WIN32
                    ::CreateDirectoryA(built.c_str(), NULL);
#else
                    ::mkdir(built.c_str(), 0755);
#endif
                }
                if (i < path.size())
                {
                    built += path[i];
                }
                continue;
            }
            built += path[i];
        }
#ifdef _WIN32
        const DWORD attributes = ::GetFileAttributesA(path.c_str());
        return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
#else
        struct stat info;
        return ::stat(path.c_str(), &info) == 0 && S_ISDIR(info.st_mode);
#endif
    }

    /**
     * Write it whole, or leave what was there. False when it could not be written, which
     * a caller may not treat as fatal: a board whose install directory is read-only is a
     * board that cannot update, and it must still START.
     */
    inline bool writeState(const std::string &path, const State &state)
    {
        ensureDirectory(directoryOf(path));
        const std::string temporary = path + ".new";
        {
            std::ofstream out(temporary.c_str(), std::ios::binary | std::ios::trunc);
            if (!out)
            {
                return false;
            }
            out << "# opendartboard-launcher (#1306). Written by the launcher; safe to delete.\n";
            out << "detector_version=" << state.detector_version << "\n";
            out << "previous_version=" << state.previous_version << "\n";
            out << "last_started=" << state.last_started << "\n";
            out << "last_stopped=" << state.last_stopped << "\n";
            out << "last_ending=" << state.last_ending << "\n";
            out << "failed_starts=" << state.failed_starts << "\n";
            out.flush();
            if (!out.good())
            {
                return false;
            }
        }
        // Rename over, so a launcher killed mid-write leaves the old state rather than
        // half of a new one. Windows' rename refuses an existing target, so remove first;
        // the window between the two is the one moment state can be lost, and losing it
        // means "I remember nothing", which is the safe default by construction.
        std::remove(path.c_str());
        if (std::rename(temporary.c_str(), path.c_str()) != 0)
        {
            std::remove(temporary.c_str());
            return false;
        }
        return true;
    }

    // ------------------------------------------------------------------ files and trees

    inline bool fileExists(const std::string &path)
    {
#ifdef _WIN32
        const DWORD attributes = ::GetFileAttributesA(path.c_str());
        return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
#else
        struct stat info;
        return ::stat(path.c_str(), &info) == 0 && S_ISREG(info.st_mode);
#endif
    }

    inline bool directoryExists(const std::string &path)
    {
#ifdef _WIN32
        const DWORD attributes = ::GetFileAttributesA(path.c_str());
        return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
#else
        struct stat info;
        return ::stat(path.c_str(), &info) == 0 && S_ISDIR(info.st_mode);
#endif
    }

    /**
     * #1796: copy a file whole. Used for one thing: keeping the deb's detector as
     * `previous` before the first update on a Pi, because that file is dpkg's and may be
     * read but never moved. The swap and the rollback are still renames (moveFile); this
     * is the one copy on the path and it is never of a file the launcher installed.
     */
    inline bool copyFile(const std::string &from, const std::string &to)
    {
        std::ifstream in(from.c_str(), std::ios::binary);
        if (!in)
        {
            return false;
        }
        std::ofstream out(to.c_str(), std::ios::binary | std::ios::trunc);
        if (!out)
        {
            return false;
        }
        out << in.rdbuf();
        out.flush();
        return out.good() && !in.bad();
    }

    /**
     * #1796: which file the launcher starts. The installed detector when there is one,
     * else the shipped one on a board that has `shipped` at all. On Windows `shipped` is
     * empty and the answer is layout.detector, exactly as before -- a missing detector is
     * then #1303's NeverStarted and its sentence, byte for byte.
     */
    inline std::string programToStart(const Layout &layout)
    {
        if (!layout.shipped.empty() && !fileExists(layout.detector))
        {
            return layout.shipped;
        }
        return layout.detector;
    }

    /**
     * Delete a directory and everything under it. Used on `update\staging` and on
     * `update\download.zip` BEFORE an attempt and after one, so an interrupted download
     * leaves nothing that a later run could mistake for a finished one -- the second
     * criterion, arranged rather than hoped for.
     */
    inline void removeTree(const std::string &path)
    {
        if (path.empty())
        {
            return;
        }
#ifdef _WIN32
        WIN32_FIND_DATAA found;
        const std::string pattern = path + "\\*";
        HANDLE handle = ::FindFirstFileA(pattern.c_str(), &found);
        if (handle != INVALID_HANDLE_VALUE)
        {
            do
            {
                const std::string name = found.cFileName;
                if (name == "." || name == "..")
                {
                    continue;
                }
                const std::string child = path + "\\" + name;
                if ((found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
                {
                    removeTree(child);
                }
                else
                {
                    ::SetFileAttributesA(child.c_str(), FILE_ATTRIBUTE_NORMAL);
                    ::DeleteFileA(child.c_str());
                }
            } while (::FindNextFileA(handle, &found) != 0);
            ::FindClose(handle);
        }
        ::RemoveDirectoryA(path.c_str());
#else
        DIR *directory = ::opendir(path.c_str());
        if (directory != NULL)
        {
            struct dirent *entry = NULL;
            while ((entry = ::readdir(directory)) != NULL)
            {
                const std::string name = entry->d_name;
                if (name == "." || name == "..")
                {
                    continue;
                }
                const std::string child = path + "/" + name;
                struct stat info;
                if (::lstat(child.c_str(), &info) == 0 && S_ISDIR(info.st_mode))
                {
                    removeTree(child);
                }
                else
                {
                    ::unlink(child.c_str());
                }
            }
            ::closedir(directory);
        }
        ::rmdir(path.c_str());
#endif
    }

    /**
     * Move a file, even one a process still has open. THIS IS THE FIFTH CRITERION AND THE
     * WHOLE REASON THE SWAP LIVES IN THE LAUNCHER.
     *
     * A running .exe cannot be OVERWRITTEN -- the image is mapped and the write is refused
     * with a sharing violation. It can be RENAMED, on the same volume, because a rename
     * changes the directory entry and not the mapped file object, and the running process
     * goes on executing the bytes it already has. So the swap never writes over
     * opendartboard.exe; it renames it out of the way and renames the new one in. A
     * detector that has not finished exiting keeps running from `update\previous`, under
     * its new name, and notices nothing.
     *
     * MOVEFILE_REPLACE_EXISTING is deliberately NOT passed: every destination here is one
     * this code has just made sure is empty, and a replace would silently overwrite a file
     * somebody else put there. MOVEFILE_COPY_ALLOWED is not passed either, because a copy
     * across volumes would reintroduce exactly the overwrite this exists to avoid -- an
     * install split across two volumes fails loudly instead.
     */
    inline bool moveFile(const std::string &from, const std::string &to)
    {
#ifdef _WIN32
        return ::MoveFileExA(from.c_str(), to.c_str(), 0) != 0;
#else
        return std::rename(from.c_str(), to.c_str()) == 0;
#endif
    }

    /**
     * Make a file runnable. A no-op on Windows, where there is no such bit and an .exe is
     * an .exe; a chmod on POSIX, where there is and a zip need not carry one.
     *
     * MEASURED RATHER THAN ANTICIPATED. The first run of testers/i1306_check.sh installed
     * a release and then reported `did not start (0 s, exit code 13)` -- EACCES, because
     * Python's ZipFile.extract deliberately drops an entry's mode and the unpacked
     * detector came out 0644. On Windows tar.exe the question does not arise, so this line
     * changes nothing that ships; what it removes is a way for the POSIX half of the seam
     * to make a board look broken for a reason that is not the board's.
     */
    inline bool makeExecutable(const std::string &path)
    {
#ifdef _WIN32
        (void)path;
        return true;
#else
        return ::chmod(path.c_str(), 0755) == 0;
#endif
    }

    /** What the platform said about the last failure, for a line a tester can quote. */
    inline std::string lastFileError()
    {
#ifdef _WIN32
        return "Windows " + std::to_string(static_cast<unsigned long>(::GetLastError()));
#else
        return std::string(std::strerror(errno));
#endif
    }
}
