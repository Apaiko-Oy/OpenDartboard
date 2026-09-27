#pragma once
// #822: where a paired board keeps the credential it was given, and how a secret is
// written on each platform.
//
// The decision, recorded here because the header is where somebody looks:
//
//   Windows  %APPDATA%\OpenDartboard\            (C:\Users\<u>\AppData\Roaming\...)
//   POSIX    $STATE_DIRECTORY, then $XDG_CONFIG_HOME/opendartboard/ or
//            $HOME/.config/opendartboard/
//
// #1660: $STATE_DIRECTORY comes first because it is the only one of the three a Pi board
// under systemd has. opendartboard.service sets no User=, and systemd gives a system unit
// with no User= neither $HOME nor $XDG_CONFIG_HOME, so this used to answer "" there and
// every caller joined "" onto a leaf -- credentials.json, owed.jsonl, channel.json --
// which is a path relative to the working directory, and a unit's working directory is
// `/`. MEASURED on 47ea89d under `env -i` against the stub: --pair SPENT the code, then
// ensureDir("") refused to keep the credential it was given, so the board stayed unpaired
// and the code was gone; --channel wrote channel.json into the working directory. The
// unit now says StateDirectory=opendartboard, systemd creates
// /var/lib/opendartboard root-only and exports it as $STATE_DIRECTORY, and the answer is
// that directory itself (no /opendartboard appended: systemd has already named it).
//
// And "" is no longer something a caller can join onto a leaf by accident: configFile()
// answers "" with it, so a board with no directory has no credential path at all rather
// than one in whatever directory it was started in.
//
// Not next to the .exe. Both answers lose the credential when a pub PC is re-imaged,
// so re-imaging does not choose between them; what chooses is who else on the box can
// read a bearer token. A file beside the .exe under Program Files inherits a DACL that
// grants Users read, so every account on the machine can read it, and an install under
// a hand-made directory is usually writable by them too. %APPDATA% inherits Roaming's
// DACL -- the owning user, SYSTEM and Administrators -- so a second pub account cannot
// read it. The cost is that a board run under a different Windows account, or after a
// profile reset, is unpaired and needs one new code; that is the cheap failure, and a
// leaked token is not.
//
// --credentials <path> overrides both, which is what a board running as a service under
// a machine account or from a USB stick should use.

#include <string>
#include <cstdlib>
#include <fstream>
#include <sys/stat.h>
#include <sys/types.h>

#ifdef _WIN32
#include <direct.h>
// windows.h arrives through od_platform_first.hpp, force-included on MSVC.
#include "od_platform_first.hpp"
#else
#include <unistd.h>
#endif

namespace od_paths
{
    inline std::string env(const char *name)
    {
        const char *v = std::getenv(name);
        return v ? std::string(v) : std::string();
    }

    /**
     * The per-user directory this board keeps its credential and its address in.
     * Returns an empty string when no home is discoverable, which the caller must
     * treat as "unpaired and cannot be paired" rather than falling back to the
     * working directory. #1660: that sentence was the rule and nothing kept it; every
     * caller joined "" onto a leaf. Ask configFile() for a file, which keeps it.
     */
    inline std::string configDir()
    {
#ifdef _WIN32
        std::string base = env("APPDATA");
        if (base.empty())
        {
            return "";
        }
        return base + "\\OpenDartboard";
#else
        // systemd's StateDirectory=. It may name several directories separated by ':';
        // the unit names one, and the first is taken. Only an absolute path is honoured,
        // because a relative one is the working directory again by another name.
        std::string state = env("STATE_DIRECTORY");
        state = state.substr(0, state.find(':'));
        if (!state.empty() && state[0] == '/')
        {
            return state;
        }
        std::string base = env("XDG_CONFIG_HOME");
        if (base.empty())
        {
            std::string home = env("HOME");
            if (home.empty())
            {
                return "";
            }
            base = home + "/.config";
        }
        return base + "/opendartboard";
#endif
    }

#ifdef _WIN32
    /**
     * #1259: the directory the running .exe is in, or "" if Windows will not say.
     *
     * A double-clicked program starts in its own folder, but a shortcut's "Start in", a
     * console opened elsewhere or a scheduled task each start it somewhere else, and a
     * relative default then names a file in whatever directory that was. What ships beside
     * the .exe is therefore looked for beside the .exe. Windows only: the Linux build
     * installs to fixed absolute paths and never had the problem.
     */
    inline std::string exeDir()
    {
        char buffer[MAX_PATH] = {0};
        DWORD length = ::GetModuleFileNameA(NULL, buffer, MAX_PATH);
        if (length == 0 || length >= MAX_PATH)
        {
            return "";
        }
        std::string path(buffer, length);
        size_t slash = path.find_last_of("\\/");
        return slash == std::string::npos ? std::string() : path.substr(0, slash);
    }
#endif

    inline char sep()
    {
#ifdef _WIN32
        return '\\';
#else
        return '/';
#endif
    }

    inline std::string join(const std::string &dir, const std::string &leaf)
    {
        if (dir.empty())
        {
            return leaf;
        }
        return dir + sep() + leaf;
    }

    /**
     * #1660: a file in configDir(), or "" when there is no configDir(). Never the bare
     * leaf: join("", leaf) is `leaf`, a path relative to the working directory, and that
     * is how a board under systemd came to be writing its credential into `/`. A caller
     * given "" has nowhere to keep the file and must say so, not write it somewhere else.
     */
    inline std::string configFile(const std::string &leaf)
    {
        const std::string dir = configDir();
        return dir.empty() ? std::string() : join(dir, leaf);
    }

    /** The sentence a board says when configDir() is "", so every caller says the same. */
    inline const char *noConfigDirReason()
    {
#ifdef _WIN32
        return "there is no %APPDATA%, so this board has nowhere to keep a credential";
#else
        return "there is no $STATE_DIRECTORY, $XDG_CONFIG_HOME or $HOME, so this board has "
               "nowhere to keep a credential";
#endif
    }

    /** mkdir -p over one path. Silent on "already exists"; false on anything else. */
    inline bool ensureDir(const std::string &path)
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
                if (built.empty() || (built.size() == 2 && built[1] == ':'))
                {
                    if (i < path.size())
                    {
                        built += path[i];
                        continue;
                    }
                }
                if (!built.empty())
                {
#ifdef _WIN32
                    _mkdir(built.c_str());
#else
                    ::mkdir(built.c_str(), 0700);
#endif
                }
            }
            if (i < path.size())
            {
                built += path[i];
            }
        }
        struct stat st;
        return ::stat(path.c_str(), &st) == 0;
    }

    /**
     * Write a secret. The file is created before it is written to, so the mode is in
     * place before the bytes are: on POSIX 0600, on Windows _S_IREAD|_S_IWRITE plus
     * whatever %APPDATA% inherits, which is the argument at the top of this file. No
     * explicit Windows ACL is set and none is claimed.
     */
    inline bool writeSecret(const std::string &path, const std::string &contents)
    {
        {
            std::ofstream create(path.c_str(), std::ios::binary | std::ios::trunc);
            if (!create)
            {
                return false;
            }
        }
#ifndef _WIN32
        if (::chmod(path.c_str(), S_IRUSR | S_IWUSR) != 0)
        {
            return false;
        }
#endif
        std::ofstream out(path.c_str(), std::ios::binary | std::ios::trunc);
        if (!out)
        {
            return false;
        }
        out << contents;
        out.flush();
        return out.good();
    }

    inline bool readFile(const std::string &path, std::string &out)
    {
        std::ifstream in(path.c_str(), std::ios::binary);
        if (!in)
        {
            return false;
        }
        out.assign((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        return true;
    }

    /**
     * True when the file is readable by somebody other than its owner. POSIX only --
     * on Windows this answers false, because a DACL is not a mode and this function
     * will not pretend to have read one.
     */
    inline bool worldReadable(const std::string &path)
    {
#ifdef _WIN32
        (void)path;
        return false;
#else
        struct stat st;
        if (::stat(path.c_str(), &st) != 0)
        {
            return false;
        }
        return (st.st_mode & (S_IRGRP | S_IROTH | S_IWGRP | S_IWOTH)) != 0;
#endif
    }
}
