#pragma once
// #822: where a paired board keeps the credential it was given, and how a secret is
// written on each platform.
//
// The decision, recorded here because the header is where somebody looks:
//
//   Windows  %APPDATA%\OpenDartboard\            (C:\Users\<u>\AppData\Roaming\...)
//   POSIX    $XDG_CONFIG_HOME/opendartboard/ or $HOME/.config/opendartboard/
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
     * working directory.
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
