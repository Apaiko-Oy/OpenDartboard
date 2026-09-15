#pragma once
#include <csignal>
#include "logging.hpp"

namespace signals
{

    // #825: the handler's whole job is to record which signal arrived and return.
    //
    // It used to call a std::function that cleared the scorer's running flag and then
    // call exit(signal) on the very next line, so the loop never observed the flag it
    // had just been handed, main was never unwound, ~Scorer never ran, and every
    // destructor in the program either ran from inside exit() -- racing threads nobody
    // had joined -- or did not run at all. #803 read it off the source, #816 measured
    // Scorer stopped x0 and WebSocket service stopped x0, and #817 caught a still-running
    // thread logging through a destroyed std::string because of it.
    //
    // `inline`, not `static`: `static` at namespace scope in a header is internal
    // linkage, so every translation unit that included this file got its own flag and
    // its own std::function, and which copy the handler wrote was a link-order
    // coincidence -- #803's finding and #817's seventeen copies, in a second header.
    // One object for the whole program, constant-initialised, and with no destructor
    // for exit() to run.
    inline volatile std::sig_atomic_t received_signal = 0;

    // The number of the signal that asked the program to stop, or 0. The loop reads this.
    inline int shutdownRequested()
    {
        return (int)received_signal;
    }

    // Async-signal-safe by construction: one store to a sig_atomic_t, and on a second
    // signal the default disposition and a re-raise, both of which are on the standard's
    // list of what a handler may do. Nothing here logs, allocates or takes a lock. The
    // handler this replaces called log_warning -- std::cout and an ofstream -- and then
    // exit(), which runs static destructors, from a signal context; either can deadlock
    // against a lock the interrupted thread was holding.
    inline void signalHandler(int sig)
    {
        if (received_signal != 0)
        {
            // Asked twice. Stop being polite: hand the signal back to the default
            // disposition so a wedged loop can still be killed without a SIGKILL.
            ::signal(sig, SIG_DFL);
            ::raise(sig);
            return;
        }
        received_signal = sig;
    }

    // Register the handlers. There is no callback any more: the thing that has to
    // happen on a signal is that the loop notices, and the loop is the only place that
    // can say so safely.
    inline void setupSignalHandlers()
    {
        ::signal(SIGINT, signalHandler);
        ::signal(SIGTERM, signalHandler);
        log_info("Signal handlers registered for graceful shutdown");
    }

} // namespace signals
