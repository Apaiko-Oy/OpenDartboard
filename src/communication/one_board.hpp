#pragma once
// #1473: one board per host, refused on a lock rather than on a port.
//
// THE DEFECT. httplib::detail::default_socket_options sets SO_REUSEPORT and NOT
// SO_REUSEADDR on Linux. Linux lets two sockets share a port when every one of them asked
// for SO_REUSEPORT, so two opendartboard processes on one host both bind 13520 and the
// kernel then hands each arriving connection to one of them. Nothing fails and nothing is
// logged. A phone subscribing to the score stream reaches one of the two boards, chosen
// per connection, and sees roughly half the darts.
//
// WHY A LOCK AND NOT THE FLAG. Clearing SO_REUSEPORT is less code and would fold this into
// #1295's existing refusal -- the second bind would fail, listen() would say so, and the
// board would not be announced. It was refused as the mechanism because it is a local
// patch to VENDORED LIBRARY CONFIGURATION: the flag is upstream's default, and an upgrade,
// a re-vendor or a tidy-up silently restores the bug, whose failure mode is the one above
// -- two boards, both looking fine, nothing logged. A lock depends on no library default
// surviving anything. It also catches the second board BEFORE it reaches its socket, which
// is the window a restarting board overlaps its predecessor in and which neither the flag
// nor a check made after binding can see.
//
// So SO_REUSEPORT is left exactly as upstream set it, and this is taken first.
//
// HOW IT IS TAKEN, ON LINUX. An abstract AF_UNIX socket -- a name in the kernel's abstract
// namespace, with a leading NUL and no filesystem entry. Compared with a pidfile:
//
//   * it needs no writable directory, so an operator running the binary by hand as `pi`
//     is refused exactly as the systemd unit running as root is. A pidfile under /run
//     that the second board cannot write is a lock that silently is not there, which is
//     the failure this issue is about;
//   * the kernel releases it when the last descriptor closes, SIGKILL included, so there
//     is no stale lock and nothing to clean up. A pidfile outlives the process that wrote
//     it and every reader of one has to guess whether the pid in it is still that board;
//   * it is scoped to the network namespace, which is exactly "this host": two boards in
//     two containers are two hosts and do not collide, and two boards on one Pi do.
//
// WHO HOLDS IT. listen() is called on the lock socket and accept() never is. That is not
// an oversight: a second board can then connect() to the same name -- which completes into
// the backlog without anybody accepting -- and read SO_PEERCRED, which for a socket created
// by connect() carries the credentials of the process that called listen(). So the refusal
// names the pid of the board that is already running, with no thread, no protocol and no
// /proc walk. If the backlog ever fills, or the probe fails for any other reason, the pid
// is reported as 0 and the refusal still stands: the lock is the bind, not the probe.
//
// WINDOWS. No abstract sockets, so a named mutex, which the kernel releases on process
// exit in the same way. Global\ first, because two sessions on one machine are still one
// host; Local\ when creating a Global object is refused, which is what an unprivileged
// interactive start meets. The holder's pid is not knowable from a mutex, so it is 0 and
// the words say "another opendartboard" without naming one.
#include <string>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <cerrno>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

namespace one_board
{
    // The name the lock is taken under. It carries the score port because the lock is
    // about that port: two boards deliberately given different ports are not two boards
    // fighting over one socket. It holds no slash and no backslash, so the same string
    // is a legal abstract socket name and a legal Windows object name.
    inline std::string lockName(int port)
    {
        return "opendartboard-score-" + std::to_string(port);
    }

    // #1473's falsifier, in the shape od_fix, #1339 and #1392 established: one binary, the
    // lock selected at run time, so that "different build" is never a confound when the
    // tester measures what the lock changes. OD_ONE_BOARD=off makes take() succeed without
    // taking anything, which is the tree as it was before this issue. Nothing but a tester
    // sets it, and a board started with it can be the second board on a host.
    inline bool switchedOff()
    {
        const char *e = std::getenv("OD_ONE_BOARD");
        return e && std::string(e) == "off";
    }

    // The claim on this host's score port. Held for as long as this object lives, which in
    // main() is the whole of the run: it is declared above the Scorer, so it is released
    // after the Scorer has been destroyed and the socket really has been given up.
    class Claim
    {
    public:
        Claim() = default;
        ~Claim() { release(); }
        Claim(const Claim &) = delete;
        Claim &operator=(const Claim &) = delete;

        bool held() const { return held_; }
        // The pid of the board that already holds it, or 0 when this platform or this
        // moment could not say. Only meaningful when held() is false.
        int holderPid() const { return holder_pid_; }
        // What the mechanism did, for the log: never the sentence an operator reads.
        const std::string &detail() const { return detail_; }

#ifdef _WIN32
        bool take(int port)
        {
            if (switchedOff())
            {
                held_ = true;
                detail_ = "OD_ONE_BOARD=off: no lock was taken";
                return true;
            }
            const std::string name = lockName(port);
            for (const char *scope : {"Global\\", "Local\\"})
            {
                const std::string object = std::string(scope) + name;
                HANDLE h = ::CreateMutexA(NULL, TRUE, object.c_str());
                const DWORD err = ::GetLastError();
                if (h && err == ERROR_ALREADY_EXISTS)
                {
                    ::CloseHandle(h);
                    held_ = false;
                    detail_ = "the mutex " + object + " is already held";
                    return false;
                }
                if (h)
                {
                    mutex_ = h;
                    held_ = true;
                    detail_ = "holding the mutex " + object;
                    return true;
                }
                if (err != ERROR_ACCESS_DENIED)
                {
                    held_ = false;
                    detail_ = "could not create the mutex " + object + ": error " + std::to_string(err);
                    return false;
                }
                // Global objects are refused to an unprivileged interactive start; a
                // Local one still catches the two boards a person can start by hand.
            }
            held_ = false;
            detail_ = "could not create a mutex in either object namespace";
            return false;
        }

        void release()
        {
            if (mutex_)
            {
                ::ReleaseMutex((HANDLE)mutex_);
                ::CloseHandle((HANDLE)mutex_);
                mutex_ = nullptr;
            }
            held_ = false;
        }

    private:
        void *mutex_ = nullptr;
#else
        bool take(int port)
        {
            if (switchedOff())
            {
                held_ = true;
                detail_ = "OD_ONE_BOARD=off: no lock was taken";
                return true;
            }
            const std::string name = lockName(port);
            sockaddr_un addr;
            std::memset(&addr, 0, sizeof addr);
            addr.sun_family = AF_UNIX;
            if (name.size() + 1 > sizeof addr.sun_path)
            {
                held_ = false;
                detail_ = "the lock name " + name + " does not fit a unix socket address";
                return false;
            }
            // The abstract namespace: sun_path[0] stays NUL, the name follows it, and the
            // address length says where the name ends. There is no NUL terminator and no
            // file anywhere.
            std::memcpy(addr.sun_path + 1, name.data(), name.size());
            const socklen_t len = static_cast<socklen_t>(offsetof(sockaddr_un, sun_path) + 1 + name.size());

            fd_ = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
            if (fd_ < 0)
            {
                held_ = false;
                detail_ = std::string("could not create the lock socket: ") + std::strerror(errno);
                return false;
            }
            if (::bind(fd_, reinterpret_cast<const sockaddr *>(&addr), len) == 0)
            {
                // Never accepted; see the header comment. The backlog is generous because
                // every refused board leaves one unaccepted connection in it, and a board
                // restarted in a loop against a live one can leave several.
                ::listen(fd_, 128);
                held_ = true;
                detail_ = "holding the abstract lock @" + name;
                return true;
            }
            const int why = errno;
            if (why == EADDRINUSE)
            {
                holder_pid_ = askWhoHolds(addr, len);
            }
            ::close(fd_);
            fd_ = -1;
            held_ = false;
            detail_ = "@" + name + " is already taken: " + std::strerror(why);
            return false;
        }

        void release()
        {
            if (fd_ >= 0)
            {
                ::close(fd_); // the kernel forgets the name with the last descriptor
                fd_ = -1;
            }
            held_ = false;
        }

    private:
        // Who is listening on the lock's name, asked of the kernel rather than of a file.
        // SOCK_NONBLOCK so a full backlog answers at once instead of waiting for an
        // accept() that is never coming.
        static int askWhoHolds(const sockaddr_un &addr, socklen_t len)
        {
            int probe = ::socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
            if (probe < 0)
            {
                return 0;
            }
            int pid = 0;
            if (::connect(probe, reinterpret_cast<const sockaddr *>(&addr), len) == 0)
            {
                struct ucred who;
                std::memset(&who, 0, sizeof who);
                socklen_t n = sizeof who;
                if (::getsockopt(probe, SOL_SOCKET, SO_PEERCRED, &who, &n) == 0)
                {
                    pid = static_cast<int>(who.pid);
                }
            }
            ::close(probe);
            return pid;
        }

        int fd_ = -1;
#endif
        bool held_ = false;
        int holder_pid_ = 0;
        std::string detail_;
    };

    // The sentence the operator reads, kept beside the mechanism so a tester can hold the
    // words to the thing that produces them. Two lines: what happened, and what to do.
    inline std::string refusedBecause(const Claim &claim, int port)
    {
        const std::string who = claim.holderPid() > 0
                                    ? "another opendartboard (pid " + std::to_string(claim.holderPid()) + ")"
                                    : "another opendartboard";
        return "not starting: " + who + " is already running on this host and holds the score port " +
               std::to_string(port) + ". Two boards would both bind that port and the darts would be split " +
               "between them with nothing logged, so this one is stopping instead. (" + claim.detail() + ")";
    }

    inline std::string refusalRemedy(const Claim &claim)
    {
        const std::string how = claim.holderPid() > 0
                                    ? "stop it first -- 'systemctl stop opendartboard', or 'kill " +
                                          std::to_string(claim.holderPid()) + "' -- and start this one again"
                                    : "stop it first -- 'systemctl stop opendartboard', or 'pkill opendartboard' -- "
                                      "and start this one again";
        return "one host runs one board: " + how + ".";
    }
}
