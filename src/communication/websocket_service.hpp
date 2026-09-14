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
    WebSocketService(std::shared_ptr<ScoreQueue> queue, ScoreSocketSettings settings = ScoreSocketSettings());
    ~WebSocketService();

    void start();
    void stop();
    void stopServerOnce(); // #816
    bool isRunning() const { return running_; }

private:
    void run();
    void broadcastScore(const std::string &json_message); // Updated signature
    std::string formatScoreJson(const DetectorResult &result);

    std::shared_ptr<ScoreQueue> score_queue_;
    std::thread worker_thread_;
    std::atomic<bool> running_{false};
    std::unique_ptr<httplib::Server> server_; // Use httplib, not libwebsockets
    ScoreSocketSettings settings_;
    int port_;
};
