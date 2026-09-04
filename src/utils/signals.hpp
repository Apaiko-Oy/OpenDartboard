#pragma once
#include <csignal>
#include <functional>
#include "logging.hpp"

namespace signals
{

    // Global callback for signal handling
    static std::function<void()> shutdownCallback;

    // Signal handler function
    inline void signalHandler(int signal)
    {
        log_warning("Received signal " + std::to_string(signal) + ", shutting down...");

        // Call the registered shutdown callback if it exists
        if (shutdownCallback)
        {
            shutdownCallback();
        }

        exit(signal);
    }

    // Register signal handlers and shutdown callback
    inline void setupSignalHandlers(std::function<void()> callback)
    {
        shutdownCallback = callback;
        signal(SIGINT, signalHandler);
        signal(SIGTERM, signalHandler);
        log_info("Signal handlers registered for graceful shutdown");
    }

    // #824: the handler --setup registers. It sets the flag and RETURNS, so the
    // capture loop leaves `while (running)` of its own accord, `run()` returns,
    // `main` falls into its own destructors, and ~Scorer releases the cameras.
    //
    // This is deliberately not the production path. The shutdown study measured
    // what `exit(signal)` costs — `Scorer stopped` 0, `WebSocket service stopped` 0,
    // exit code 15, and function-local streamers destroyed out from under a
    // running capture thread — and concluded that giving the program a real exit
    // is new work rather than a port. --setup is the one mode where a person is
    // sitting at the machine, will press Ctrl+C, and must get the camera back; so
    // it gets the cooperative handler and nothing else's behaviour moves.
    //
    // The cost, said out loud: the loop observes the flag only after the capture
    // read in flight returns, so a stop takes up to one cycle. That is the price
    // of releasing the device instead of abandoning its handle.
    inline void cooperativeShutdownHandler(int signal)
    {
        log_warning("Received signal " + std::to_string(signal) +
                    ", finishing the cycle in flight and releasing the cameras...");
        if (shutdownCallback)
        {
            shutdownCallback();
        }
    }

    inline void setupCooperativeSignalHandlers(std::function<void()> callback)
    {
        shutdownCallback = callback;
        signal(SIGINT, cooperativeShutdownHandler);
        signal(SIGTERM, cooperativeShutdownHandler);
        log_info("Signal handlers registered for cooperative shutdown (--setup)");
    }

} // namespace signals
