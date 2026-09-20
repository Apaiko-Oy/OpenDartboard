#pragma once
// #1383: the one sentence this board can say to the machine it is running on.
//
// THE PROBLEM. A board that cannot see never exits -- #895's fault vigil, and it is
// deliberate: a camera that comes back should find the detector still there. Every
// consequence of that is written in scorer.cpp. This header is about the one nobody had
// written down. `Restart=` fires on EXIT, so a blind board is never restarted and the
// unit's StartLimitBurst never counts a thing; to systemd, and to anybody reading
// `systemctl status`, a board that sees nothing and a board scoring darts are the same
// `active (running)`. The board already says ERROR -- board_sight's ladder, over the
// beat, to Turnaus (#892) -- and that is a sentence addressed to a server, over a
// network, by a board that has been paired. It says nothing to the supervisor standing
// over it on the same box.
//
// WHAT THIS IS. sd_notify(3)'s protocol, which is a documented wire format and not a
// library: a datagram of `KEY=value` lines to the AF_UNIX socket named by $NOTIFY_SOCKET.
// systemd keeps the last STATUS= it was sent and prints it as the `Status:` line of
// `systemctl status`, readable by a machine as `systemctl show -p StatusText`.
//
// WHY NOT libsystemd. It would be this binary's first dependency on the platform -- it
// links OpenCV, httplib and json and nothing of systemd -- for forty lines of sendto().
// The protocol is documented so that it can be reimplemented, and the .deb would gain a
// dependency that the Windows port would then have to be told about.
//
// WHY NOT WatchdogSec= AND A PING THIS STOPS SENDING. That was the other mechanism
// offered on the issue, and it is the one that cannot be taken: a missed watchdog makes
// systemd KILL the service, `Restart=always` starts it again, and the board that was
// waiting for a camera to come back is gone. That is the fourth option the maintainer
// refused -- letting a blind board exit -- arriving by a side door.
//
// WHY NOT Type=notify. Because then READY=1 would have to mean something, and the only
// honest thing it could mean is "I can see" -- which would leave a blind board in
// `activating` until TimeoutStartSec killed it. The same exit, one more door.
//
// MEASURED, 2026-09-20, on a systemd 255 session manager: a `Type=simple` unit with
// `NotifyAccess=main` has $NOTIFY_SOCKET set and its STATUS= is kept and printed. No
// `Type=notify` and no READY=1 are needed for a status line, and this changes nothing
// about when the unit is considered started. testers/i1383_units.sh is that measurement.
//
// IT IS BEST-EFFORT AND SAYS SO. Nothing in this program's behaviour may depend on a
// datagram arriving: no $NOTIFY_SOCKET (every run that is not under systemd -- a tester,
// a laptop, `make build`) is not an error and is not logged, and a send that fails is a
// supervisor that will go on showing the last thing it was told.

#include <string>

#if defined(__linux__)
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

namespace od_notify
{
    /** The socket systemd put in the environment, or nullptr when nothing is supervising. */
    inline const char *socketAddress()
    {
#if defined(__linux__)
        const char *address = std::getenv("NOTIFY_SOCKET");
        return (address && *address) ? address : nullptr;
#else
        return nullptr;
#endif
    }

    /** Whether there is a supervisor listening. Nothing here branches on it but a tester. */
    inline bool supervised()
    {
        return socketAddress() != nullptr;
    }

    /**
     * One datagram of `KEY=value` lines. True when it went out.
     *
     * The address is either a filesystem path or, where it begins with `@`, a name in
     * Linux's abstract namespace -- which is a leading NUL byte rather than a character,
     * so the `@` is replaced and the length is counted rather than measured with strlen.
     */
    inline bool send(const std::string &payload)
    {
#if defined(__linux__)
        const char *address = socketAddress();
        if (!address)
        {
            return false;
        }

        const size_t length = std::strlen(address);
        struct sockaddr_un target;
        std::memset(&target, 0, sizeof(target));
        if (length == 0 || length >= sizeof(target.sun_path))
        {
            return false;
        }
        target.sun_family = AF_UNIX;
        std::memcpy(target.sun_path, address, length);
        if (target.sun_path[0] == '@')
        {
            target.sun_path[0] = '\0';
        }
        const socklen_t size = (socklen_t)(offsetof(struct sockaddr_un, sun_path) + length);

        // SOCK_CLOEXEC: this process spawns nothing today, and a descriptor that outlives
        // the call it was opened for is how that stops being true quietly.
        const int fd = ::socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
        if (fd < 0)
        {
            return false;
        }
        const ssize_t sent = ::sendto(fd, payload.data(), payload.size(), MSG_NOSIGNAL,
                                      (struct sockaddr *)&target, size);
        ::close(fd);
        return sent == (ssize_t)payload.size();
#else
        (void)payload;
        return false;
#endif
    }

    /**
     * The board's own word about itself, where a supervisor can read it.
     *
     * The word is board_sight's -- ERROR, READY -- because there is one ladder and a
     * second vocabulary would drift from it, and the sentence after it is the same
     * sentence the log line beside the call already says. #1321's rule one surface over:
     * a status that offers the reader two possibilities and commits to neither is the
     * same as no status at all.
     */
    inline bool status(const std::string &word, const std::string &detail)
    {
        return send("STATUS=" + word + ": " + detail);
    }
}
