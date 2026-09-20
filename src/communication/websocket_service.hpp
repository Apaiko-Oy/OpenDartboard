#pragma once
#include "score_queue.hpp"
#include <thread>
#include <atomic>
#include <memory>
#include <string>
#include <httplib.h>

// #1187: where the score socket listens and what a subscriber presents. Loopback
// unless --listen says otherwise; the token is required on every upgrade, on
// loopback as well as on the network, so a local tool and a phone use one path.
struct ScoreSocketSettings
{
    std::string bind_address = "127.0.0.1";
    int port = 13520;
    std::string token; // presented as ?token=... on the upgrade request
};

class WebSocketService
{
public:
    // #1187: where the socket binds, its port and the token a subscriber presents.
    // #812: debug_mode decides whether the saved camera frames under debug_frames/
    // are reachable over this listener. The score API is the documented product
    // interface and is not gated here; the images are.
    WebSocketService(std::shared_ptr<ScoreQueue> queue, ScoreSocketSettings settings = ScoreSocketSettings(),
                     bool debug_mode = false);
    ~WebSocketService();

    void start();
    void stop();
    void stopServerOnce(); // #816

    // #1295: whether the score socket really came up, which is not the question the
    // isRunning() that used to be declared here answered. That one returned `running_` --
    // set by start(), cleared by stop(), and never touched by listen() -- so it said "the
    // service was started" while reading like "the socket is open". It had no callers
    // repo-wide, which is the only reason nothing was ever announced on it.
    //
    // httplib's own flag is the honest one. Server::listen() is
    // `bind_to_port(...) && listen_internal()` and listen_internal() sets is_running_ as
    // its first statement, so a port already in use never reaches it. The worker thread
    // reads that flag once, when listen() has answered, and records it here.
    //
    // It answers a question about the START. A socket that dies mid-run is deliberately
    // out of scope -- #1274's carve-out, kept by #1295 -- and the live question, for
    // whoever picks that up, is server_->is_running().
    bool scoreSocketOpened() const { return socket_resolved_ && socket_listening_; }

    // The same answer, waited for: returns once listen() has been attempted and answered,
    // so a caller asking straight after start() is not told "no" about a socket that has
    // simply not bound yet. It waits on the attempt and never on a clock -- the attempt
    // resolves when the socket comes up, when listen() returns, or when run() gives up.
    bool awaitScoreSocket();

private:
    void run();
    void broadcastScore(const std::string &json_message); // Updated signature
    std::string formatScoreJson(const DetectorResult &result);

    std::shared_ptr<ScoreQueue> score_queue_;
    std::thread worker_thread_;
    std::atomic<bool> running_{false};

    // #1295: set by the worker thread when listen() has answered, and then never again.
    std::atomic<bool> socket_resolved_{false};
    std::atomic<bool> socket_listening_{false};
    std::unique_ptr<httplib::Server> server_; // Use httplib, not libwebsockets
    ScoreSocketSettings settings_;
    int port_;
    bool debug_mode_; // #812
};
