#pragma once
// #1187: the credential a subscriber presents on the score socket.
//
// One token per board, generated on the first run and kept in a file only the
// user running the detector can read. It is presented as ?token=... on the
// upgrade request, because a browser's WebSocket cannot set a header, and it is
// printed by --show-token and nowhere else: no log line carries it in full.
//
// Where it lives is a decision this fork had to make on its own. The issue says
// "beside the pairing credentials, with the same permissions", and this fork has
// no pairing credentials and no config file: the only things the detector writes
// are cache/ and debug_frames/, both relative to the working directory. A cache
// is a thing people are told to delete, so the token is not put there - a
// deleted token silently breaks every subscriber. It is score_token in the
// working directory, mode 0600, and --token-file moves it when a credential
// store exists to move it beside.
#include <string>
#include <fstream>
#include <random>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

namespace score_token
{
    constexpr const char *kDefaultPath = "score_token";
    constexpr size_t kBytes = 16; // 128 bits, written as 32 hex characters

    inline std::string generate()
    {
        unsigned char bytes[kBytes];
        std::ifstream urandom("/dev/urandom", std::ios::binary);
        if (!urandom.read(reinterpret_cast<char *>(bytes), sizeof bytes))
        {
            std::random_device rd;
            for (auto &b : bytes)
                b = static_cast<unsigned char>(rd());
        }
        static const char hex[] = "0123456789abcdef";
        std::string out;
        for (unsigned char b : bytes)
        {
            out += hex[b >> 4];
            out += hex[b & 15];
        }
        return out;
    }

    inline std::string trimmed(const std::string &s)
    {
        size_t a = s.find_first_not_of(" \t\r\n");
        if (a == std::string::npos)
            return "";
        size_t b = s.find_last_not_of(" \t\r\n");
        return s.substr(a, b - a + 1);
    }

    // True when the token file is readable by anybody but its owner. Reported, not
    // repaired: a permission somebody loosened on purpose is theirs to tighten.
    inline bool isReadableByOthers(const std::string &path)
    {
        struct stat st;
        if (::stat(path.c_str(), &st) != 0)
            return false;
        return (st.st_mode & (S_IRWXG | S_IRWXO)) != 0;
    }

    // What happened when the token was resolved, so the caller can say it.
    struct Resolved
    {
        std::string token;   // empty when neither read nor created
        bool created = false;
        std::string error;   // set when token is empty
    };

    inline Resolved readExisting(const std::string &path)
    {
        Resolved r;
        std::ifstream in(path);
        if (!in)
        {
            r.error = std::strerror(errno);
            return r;
        }
        std::string line;
        std::getline(in, line);
        r.token = trimmed(line);
        if (r.token.empty())
            r.error = "the file is empty";
        return r;
    }

    // Reads the token at `path`, creating it with mode 0600 on the first run.
    inline Resolved loadOrCreate(const std::string &path)
    {
        {
            struct stat st;
            if (::stat(path.c_str(), &st) == 0)
                return readExisting(path);
        }
        std::string token = generate();
        int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
        if (fd < 0)
        {
            if (errno == EEXIST) // a second process won the race; read what it wrote
                return readExisting(path);
            Resolved r;
            r.error = std::strerror(errno);
            return r;
        }
        std::string line = token + "\n";
        ssize_t n = ::write(fd, line.data(), line.size());
        ::close(fd);
        if (n != static_cast<ssize_t>(line.size()))
        {
            ::unlink(path.c_str());
            Resolved r;
            r.error = "short write";
            return r;
        }
        Resolved r;
        r.token = token;
        r.created = true;
        return r;
    }

    // Length-independent comparison, so a wrong token costs the same as a right one.
    inline bool equals(const std::string &presented, const std::string &expected)
    {
        if (expected.empty())
            return false;
        unsigned char diff = static_cast<unsigned char>(presented.size() != expected.size());
        for (size_t i = 0; i < expected.size(); i++)
        {
            unsigned char p = i < presented.size() ? presented[i] : 0;
            diff |= static_cast<unsigned char>(p ^ static_cast<unsigned char>(expected[i]));
        }
        return diff == 0;
    }
}
