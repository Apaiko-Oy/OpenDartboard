// streamer.hpp — MJPEG streamer with once‑per‑second stats
// ------------------------------------------------------------------
// * Streams the most recent frame at up to `fps` (default 30).
// * Disables Nagle (TCP_NODELAY) for low latency.
// * Prints one concise line per second: pushes‑per‑sec, sends‑per‑sec, average push→send latency.
//   Example:  `[stats] push 15  send 15  lag 7 ms`.
//
#pragma once

// Before OpenCV, because on Windows this is where <winsock2.h> comes from and it
// cannot follow <windows.h> into a translation unit.
#include "od_socket.hpp"

#include <opencv2/opencv.hpp>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <algorithm>
#include <mutex>
#include <numeric>
#include <sstream>
#include <thread>
#include <vector>

class streamer
{
public:
    explicit streamer(uint16_t port = 8081, int fps = 30)
        : port_(port), periodUs_(1'000'000 / std::max(1, fps)),
          lastReport_(std::chrono::steady_clock::now())
    {
        odnet::startup();
        std::cout << "[streamer] start on port " << port_ << ", fps " << fps << '\n';
        srvThread_ = std::thread([this]
                                 { serve(); });
    }

    ~streamer()
    {
        stop_ = true;
        if (srvThread_.joinable())
            srvThread_.join();
        std::cout << "[streamer] stopped\n";
    }

    // Push a raw BGR frame (thread‑safe)
    void push(const cv::Mat &bgr)
    {
        auto now = std::chrono::steady_clock::now();
        lastPushMs_.store(std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count());

        std::vector<uchar> jpg;
        cv::imencode(".jpg", bgr, jpg, {cv::IMWRITE_JPEG_QUALITY, 80});
        {
            std::lock_guard<std::mutex> lk(mut_);
            lastJPEG_.swap(jpg);
        }
        pushCount_++;

        // print stats once per second
        if (now - lastReport_ >= std::chrono::seconds(1))
        {
            int pc = pushCount_.exchange(0);
            int sc = sendCount_.exchange(0);
            int lag = 0;
            if (!latencyAcc_.empty())
            {
                lag = std::accumulate(latencyAcc_.begin(), latencyAcc_.end(), 0) / static_cast<int>(latencyAcc_.size());
                latencyAcc_.clear();
            }

            // tempory commented out
            // to avoid too much output in the console
            // std::cout << "[streamer][stats] push=" << pc << "fps, send=" << sc << "fps | lag " << lag << " ms\n";
            lastReport_ = now;
        }
    }

private:
    static bool sendAll(odnet::socket_t fd, const void *buf, size_t len)
    {
        const char *p = static_cast<const char *>(buf);
        while (len)
        {
            int n = odnet::sendSome(fd, p, len);
            if (n <= 0)
                return false;
            p += n;
            len -= static_cast<size_t>(n);
        }
        return true;
    }

    void serve()
    {
        odnet::socket_t srv = ::socket(AF_INET, SOCK_STREAM, 0);
        if (!odnet::valid(srv))
        {
            std::cout << "[streamer] socket() failed on port " << port_ << '\n';
            return;
        }
        int one = 1;
        ::setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char *>(&one), sizeof(one));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = ::htons(port_);
        addr.sin_addr.s_addr = ::htonl(INADDR_ANY);
        if (::bind(srv, reinterpret_cast<sockaddr *>(&addr), sizeof addr) != 0)
        {
            std::cout << "[streamer] bind() failed on port " << port_ << '\n';
            odnet::closeSocket(srv);
            return;
        }
        ::listen(srv, 10);

        while (!stop_)
        {
            fd_set rfds;
            FD_ZERO(&rfds);
            FD_SET(srv, &rfds);
            timeval tv{0, 100'000};
            if (::select(static_cast<int>(srv) + 1, &rfds, nullptr, nullptr, &tv) > 0)
            {
                odnet::socket_t cli = ::accept(srv, nullptr, nullptr);
                if (odnet::valid(cli))
                    std::thread(&streamer::client, this, cli).detach();
            }
        }
        odnet::closeSocket(srv);
    }

    // ------------------------------------------------------------------ per‑client loop
    void client(odnet::socket_t sock)
    {
        int one = 1;
        ::setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char *>(&one), sizeof(one));

        char req[1024];
        odnet::recvSome(sock, req, sizeof req); // discard HTTP request
        static constexpr char hdr[] =
            "HTTP/1.0 200 OK\r\n"
            "Cache-Control: no-cache\r\n"
            "Pragma: no-cache\r\n"
            "Content-Type: multipart/x-mixed-replace; boundary=frame\r\n\r\n";
        if (!sendAll(sock, hdr, sizeof(hdr) - 1))
        {
            odnet::closeSocket(sock);
            return;
        }

        std::vector<uchar> cached;
        auto lastSent = std::chrono::steady_clock::now() - std::chrono::microseconds(periodUs_);

        while (!stop_)
        {
            {
                std::lock_guard<std::mutex> lk(mut_);
                if (!lastJPEG_.empty())
                    cached = lastJPEG_;
            }
            auto now = std::chrono::steady_clock::now();
            if (now - lastSent >= std::chrono::microseconds(periodUs_))
            {
                if (!cached.empty())
                {
                    std::ostringstream head;
                    head << "--frame\r\nContent-Type: image/jpeg\r\nContent-Length: "
                         << cached.size() << "\r\n\r\n";
                    if (!sendAll(sock, head.str().c_str(), head.str().size()))
                        break;
                    if (!sendAll(sock, cached.data(), cached.size()))
                        break;
                    if (!sendAll(sock, "\r\n", 2))
                        break;

                    sendCount_++;
                    int pushMs = static_cast<int>(lastPushMs_.load());
                    int nowMs = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count());
                    latencyAcc_.push_back(nowMs - pushMs);
                    lastSent = now;
                }
            }
            std::this_thread::sleep_for(std::chrono::microseconds(1000));
        }
        odnet::closeSocket(sock);
    }

    // ------------------------------------------------------------------ data members
    uint16_t port_;
    int periodUs_;

    std::atomic<bool> stop_{false};
    std::thread srvThread_;

    std::mutex mut_;
    std::vector<uchar> lastJPEG_;

    // stats
    std::atomic<int> pushCount_{0};
    std::atomic<int> sendCount_{0};
    std::vector<int> latencyAcc_;
    std::atomic<long long> lastPushMs_{0};
    std::chrono::steady_clock::time_point lastReport_;
};
