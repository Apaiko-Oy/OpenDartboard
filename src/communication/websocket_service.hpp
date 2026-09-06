#pragma once
#include "score_queue.hpp"
#include <thread>
#include <atomic>
#include <memory>
#include <httplib.h>

class WebSocketService
{
public:
    // #812: debug_mode decides whether the saved camera frames under debug_frames/
    // are reachable over this listener. The score API is the documented product
    // interface and is not gated here; the images are.
    WebSocketService(std::shared_ptr<ScoreQueue> queue, int port = 13520, bool debug_mode = false);
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
    int port_;
    bool debug_mode_; // #812
};
