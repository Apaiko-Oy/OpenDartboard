#pragma once
// The socket calls the debug MJPEG streamer makes, named once so that
// streamer.hpp is the same file on both platforms. This is the only place in
// the program that knows Winsock exists.
//
// It must be included before anything that may pull in <windows.h>, because
// <winsock2.h> and <winsock.h> cannot both be in one translation unit.

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
