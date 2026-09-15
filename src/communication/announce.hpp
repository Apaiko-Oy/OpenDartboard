#pragma once
// #1189: how a board announces itself on the network, and when it does not.
//
// The announcement is made by the host's own responder - Avahi on the Pi - and
// not by a library in the binary: the detector writes one service file into
// Avahi's services directory (/etc/avahi/services by default, --announce-dir
// moves it) while the score socket is open on the network, and removes it when
// the socket closes. Avahi watches that directory and publishes what it finds,
// so nothing else has to run and nothing new has to link. When the socket is
// loopback-only nothing is written, and a file left by an earlier --listen run
// is removed, so a board taken off the network stops being announced.
//
// The service is _opendartboard._tcp; its instance name is the board's label
// (--label, or the hostname); the SRV record carries the socket's port and the
// TXT records carry path=/scores, the label and the version. The token is not
// in it, and must never be: an announcement is readable by every device on the
// wifi, and the token is what says which of them may subscribe.
//
// A directory the detector cannot write to - not root, no Avahi installed - is
// logged once and the board runs unannounced; the socket does not depend on it.
//
// Windows has no Avahi and no services directory, and announcing there would
// need a library the binary does not carry (Bonjour's dns_sd), so on Windows
// this file compiles to two functions that say "not announced" and nothing is
// published. The QR code in --setup does not depend on any of this.
#include <string>
#include <fstream>
#include <cerrno>
#include <cstring>
#include <cstdio>
#ifdef _WIN32
#include <winsock2.h>
#else
#include <unistd.h>
#endif

namespace announce
{
    constexpr const char *kServiceType = "_opendartboard._tcp";
    constexpr const char *kDefaultDir = "/etc/avahi/services";
    constexpr const char *kFileName = "opendartboard.service";

    // The board's label: --label wins; otherwise the hostname, which is what a
    // club officer already calls the machine. Never empty.
    inline std::string defaultLabel()
    {
        char name[256] = {0};
        if (::gethostname(name, sizeof name - 1) != 0 || name[0] == '\0')
            return "opendartboard";
        return name;
    }

    inline std::string xmlEscaped(const std::string &s)
    {
        std::string out;
        for (char c : s)
        {
            switch (c)
            {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            default: out += c;
            }
        }
        return out;
    }

    inline std::string filePath(const std::string &dir)
    {
        return dir + "/" + kFileName;
    }

    // The Avahi service file for this board. What it holds is the whole of what a
    // device on the wifi learns: type, name, port, path, label, version.
    inline std::string serviceFile(const std::string &label, int port, const std::string &version)
    {
        std::string name = xmlEscaped(label);
        return "<?xml version=\"1.0\" standalone='no'?>\n"
               "<!DOCTYPE service-group SYSTEM \"avahi-service.dtd\">\n"
               "<!-- written by opendartboard while the score socket is on the network; removed when it stops."
               " XML forbids a double hyphen inside a comment, and Avahi drops a file that has one. -->\n"
               "<service-group>\n"
               "  <name>" + name + "</name>\n"
               "  <service>\n"
               "    <type>" + std::string(kServiceType) + "</type>\n"
               "    <port>" + std::to_string(port) + "</port>\n"
               "    <txt-record>path=/scores</txt-record>\n"
               "    <txt-record>label=" + name + "</txt-record>\n"
               "    <txt-record>version=" + xmlEscaped(version) + "</txt-record>\n"
               "    <txt-record>auth=token</txt-record>\n"
               "  </service>\n"
               "</service-group>\n";
    }

    struct Outcome
    {
        bool done = false;
        std::string detail; // the path written or removed, or strerror
    };

#ifdef _WIN32
    inline Outcome publish(const std::string &, const std::string &, int, const std::string &)
    {
        return {false, "not announced on Windows: no Avahi, and Bonjour would be a new dependency"};
    }
    inline Outcome withdraw(const std::string &)
    {
        return {false, "nothing to withdraw on Windows"};
    }
    inline void withdrawPath(const std::string &) {}
#else
    // Writes the service file. Only ever called while the socket is on the network.
    inline Outcome publish(const std::string &dir, const std::string &label, int port, const std::string &version)
    {
        std::string path = filePath(dir);
        std::ofstream out(path, std::ios::trunc);
        if (!out)
            return {false, path + ": " + std::strerror(errno)};
        out << serviceFile(label, port, version);
        out.close();
        if (!out)
            return {false, path + ": " + std::strerror(errno)};
        return {true, path};
    }

    // Removes the service file if it is there. Called on every stop and on every
    // loopback-only start, so a file from an earlier run never outlives its socket.
    inline Outcome withdraw(const std::string &dir)
    {
        std::string path = filePath(dir);
        if (std::remove(path.c_str()) == 0)
            return {true, path};
        return {false, path + ": " + std::strerror(errno)};
    }

    // The same removal from a signal handler: unlink(2) only, nothing that allocates.
    // A process killed with SIGKILL or aborted leaves the file behind; the next start
    // overwrites it (--listen) or removes it (loopback), and the unit restarts always.
    inline void withdrawPath(const std::string &path)
    {
        ::unlink(path.c_str());
    }
#endif
}
