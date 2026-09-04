#pragma once
// The socket calls the debug MJPEG streamer makes, named once so that
// streamer.hpp is the same file on both platforms. This is the only place in
// the program that knows Winsock exists.
//
// It must be included before anything that may pull in <windows.h>, because
// <winsock2.h> and <winsock.h> cannot both be in one translation unit.

// ---------------------------------------------------------------------------
// #824: where a listener may bind, said once, at the receiving end.
//
// THE RULE: a listener opened by this program may only ever name an address
// that is not reachable from the network. It is not a default that a caller
// may override — there is no parameter to override. `bindLoopbackOnly` is the
// only `::bind` in the tree and it names 127.0.0.1 itself, so no call site
// anywhere can express a routable address, and a refactor cannot reintroduce
// one by changing an argument.
//
// This is the shape #805 needed and did not have. Eight MJPEG listeners bound
// INADDR_ANY because the streamer's `serve()` said INADDR_ANY, and every gate
// that was supposed to keep them shut was a build define or a flag somewhere
// else. The gate stays (a listener still needs to be asked for); the address
// stops being anybody's choice.
//
// `scripts/check-listeners.sh` fails if a second bind, a second address
// literal, or a new INADDR_ANY appears anywhere under src/.
// ---------------------------------------------------------------------------

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>

namespace odnet
{
    using socket_t = SOCKET;
    inline bool valid(socket_t s) { return s != INVALID_SOCKET; }
    inline void closeSocket(socket_t s) { ::closesocket(s); }
    inline int sendSome(socket_t s, const char *p, size_t n) { return ::send(s, p, (int)n, 0); }
    inline int recvSome(socket_t s, char *p, size_t n) { return ::recv(s, p, (int)n, 0); }

    // Winsock has to be started per process. Every streamer calls this; the
    // second and later calls are a load of a bool.
    inline void startup()
    {
        static bool started = []
        {
            WSADATA data;
            return WSAStartup(MAKEWORD(2, 2), &data) == 0;
        }();
        (void)started;
    }
}

#else

#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <unistd.h>

namespace odnet
{
    using socket_t = int;
    inline bool valid(socket_t s) { return s >= 0; }
    inline void closeSocket(socket_t s) { ::close(s); }
    inline int sendSome(socket_t s, const char *p, size_t n) { return (int)::send(s, p, n, MSG_NOSIGNAL); }
    inline int recvSome(socket_t s, char *p, size_t n) { return (int)::recv(s, p, n, 0); }
    inline void startup() {}
}

#endif

namespace odnet
{
    // The only bind in this program. It takes a port and no address, because the
    // address is not a thing a caller of this function gets to have an opinion
    // about. Returns false and says why; the caller closes the socket.
    inline bool bindLoopbackOnly(socket_t fd, unsigned short port)
    {
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = ::htons(port);
        addr.sin_addr.s_addr = ::htonl(INADDR_LOOPBACK);
        return ::bind(fd, reinterpret_cast<sockaddr *>(&addr), sizeof addr) == 0;
    }

    // What to print, so that a log line cannot claim an address the bind did not use.
    inline const char *loopbackName() { return "127.0.0.1"; }
}
