#include <deque>
#include "websocket_service.hpp"
#include "score_token.hpp"
#include "utils.hpp"
#include <httplib.h>
#include "utils/od_fix.hpp"
#include <mutex>
#include <sstream>
#include <atomic>
#include <nlohmann/json.hpp>
#include <set>
#include <mutex>
#include <sstream>
#include <iomanip>
#include <condition_variable>
#include <optional>
#include <cerrno>
#include <cstring>
#ifndef _WIN32
// #1249: the Winsock half of these arrives with od_platform_first.hpp on MSVC, and the
// /proc walk below is Linux's alone.
#include <dirent.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif

using namespace std;
using json = nlohmann::json;

// Proper SHA1 implementation for WebSocket handshake
class SHA1
{
private:
    uint32_t h[5];
    uint64_t len;
    uint8_t buffer[64];
    uint8_t bufferPos;

    uint32_t leftRotate(uint32_t value, int amount)
    {
        return (value << amount) | (value >> (32 - amount));
    }

    void processBlock()
    {
        uint32_t w[80];

        // Copy buffer to w[0..15]
        for (int i = 0; i < 16; i++)
        {
            w[i] = (buffer[i * 4] << 24) | (buffer[i * 4 + 1] << 16) |
                   (buffer[i * 4 + 2] << 8) | buffer[i * 4 + 3];
        }

        // Extend w[16..79]
        for (int i = 16; i < 80; i++)
        {
            w[i] = leftRotate(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        }

        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];

        for (int i = 0; i < 80; i++)
        {
            uint32_t f, k;
            if (i < 20)
            {
                f = (b & c) | ((~b) & d);
                k = 0x5A827999;
            }
            else if (i < 40)
            {
                f = b ^ c ^ d;
                k = 0x6ED9EBA1;
            }
            else if (i < 60)
            {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8F1BBCDC;
            }
            else
            {
                f = b ^ c ^ d;
                k = 0xCA62C1D6;
            }

            uint32_t temp = leftRotate(a, 5) + f + e + k + w[i];
            e = d;
            d = c;
            c = leftRotate(b, 30);
            b = a;
            a = temp;
        }

        h[0] += a;
        h[1] += b;
        h[2] += c;
        h[3] += d;
        h[4] += e;
    }

public:
    SHA1()
    {
        h[0] = 0x67452301;
        h[1] = 0xEFCDAB89;
        h[2] = 0x98BADCFE;
        h[3] = 0x10325476;
        h[4] = 0xC3D2E1F0;
        len = 0;
        bufferPos = 0;
    }

    void update(const uint8_t *data, size_t size)
    {
        for (size_t i = 0; i < size; i++)
        {
            buffer[bufferPos++] = data[i];
            len++;

            if (bufferPos == 64)
            {
                processBlock();
                bufferPos = 0;
            }
        }
    }

    vector<uint8_t> finalize()
    {
        // Padding
        buffer[bufferPos++] = 0x80;

        if (bufferPos > 56)
        {
            while (bufferPos < 64)
                buffer[bufferPos++] = 0;
            processBlock();
            bufferPos = 0;
        }

        while (bufferPos < 56)
            buffer[bufferPos++] = 0;

        // Length in bits
        uint64_t bitLen = len * 8;
        for (int i = 7; i >= 0; i--)
        {
            buffer[56 + i] = bitLen & 0xFF;
            bitLen >>= 8;
        }

        processBlock();

        vector<uint8_t> result(20);
        for (int i = 0; i < 5; i++)
        {
            result[i * 4] = (h[i] >> 24) & 0xFF;
            result[i * 4 + 1] = (h[i] >> 16) & 0xFF;
            result[i * 4 + 2] = (h[i] >> 8) & 0xFF;
            result[i * 4 + 3] = h[i] & 0xFF;
        }

        return result;
    }
};

// Base64 encode
string base64_encode(const vector<uint8_t> &data)
{
    const string chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    string result;

    for (size_t i = 0; i < data.size(); i += 3)
    {
        uint32_t tmp = 0;
        int padding = 0;

        for (int j = 0; j < 3; j++)
        {
            tmp <<= 8;
            if (i + j < data.size())
            {
                tmp |= data[i + j];
            }
            else
            {
                padding++;
            }
        }

        for (int j = 0; j < 4; j++)
        {
            if (j < 4 - padding)
            {
                result += chars[(tmp >> (6 * (3 - j))) & 0x3F];
            }
            else
            {
                result += '=';
            }
        }
    }

    return result;
}

// Generate proper WebSocket accept key
string generate_websocket_accept(const string &key)
{
    string combined = key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

    SHA1 sha1;
    sha1.update((const uint8_t *)combined.c_str(), combined.length());
    vector<uint8_t> hash = sha1.finalize();

    return base64_encode(hash);
}

// WebSocket frame creation
vector<uint8_t> create_websocket_frame(const string &payload)
{
    vector<uint8_t> frame;

    // FIN + TEXT opcode
    frame.push_back(0x81);

    size_t payload_len = payload.length();
    if (payload_len < 126)
    {
        frame.push_back(payload_len);
    }
    else if (payload_len < 65536)
    {
        frame.push_back(126);
        frame.push_back((payload_len >> 8) & 0xFF);
        frame.push_back(payload_len & 0xFF);
    }
    else
    {
        frame.push_back(127);
        for (int i = 7; i >= 0; i--)
        {
            frame.push_back((payload_len >> (i * 8)) & 0xFF);
        }
    }

    // Add payload
    for (char c : payload)
    {
        frame.push_back(c);
    }

    return frame;
}

// A control frame - ping, pong, close - from the board to a subscriber. A control
// frame's payload is at most 125 bytes (RFC 6455 5.5), so the header is two bytes.
vector<uint8_t> create_control_frame(uint8_t opcode, const vector<uint8_t> &payload)
{
    vector<uint8_t> frame;
    frame.push_back(0x80 | (opcode & 0x0F));
    frame.push_back((uint8_t)(payload.size() & 0x7F));
    frame.insert(frame.end(), payload.begin(), payload.end());
    return frame;
}

// #1188: one subscriber is one outbox and one writer. The broadcast loop puts each
// frame on every subscriber's outbox and never writes to a socket itself, so a
// subscriber that has stopped taking frames costs the others nothing: its own writer
// blocks, its own outbox grows, and the loop has already moved on. The writer is the
// connection's thread - the one httplib runs the content provider on - and it is the
// only thread that ever writes to that socket, which also removes the race the shipped
// code had between the broadcast write and the ping written from the provider.
// #1282: what a socket is called on each platform. A Winsock SOCKET is a UINT_PTR and
// does not fit in an int -- storing one in the `int fd` this struct used to carry would
// truncate the handle on a 64-bit build and then use the truncated value. On Linux this
// is `int`, `-1` and `>= 0`, which is exactly what the code below said before, so the
// Linux half of every line that touches it is unmoved.
#ifdef _WIN32
using subscriber_socket_t = SOCKET;
static const subscriber_socket_t kNoSubscriberSocket = INVALID_SOCKET;
static inline bool haveSocket(subscriber_socket_t s) { return s != INVALID_SOCKET; }
#else
using subscriber_socket_t = int;
static const subscriber_socket_t kNoSubscriberSocket = -1;
static inline bool haveSocket(subscriber_socket_t s) { return s >= 0; }
#endif

struct Subscriber
{
    string peer;                   // address:port, for the log
    subscriber_socket_t fd = kNoSubscriberSocket; // the socket, resolved from the peer
    mutex m;
    condition_variable cv;
    deque<vector<uint8_t>> outbox; // frames not yet handed to the kernel, in publication order
    atomic<bool> active{true};
};

static vector<shared_ptr<Subscriber>> subscribers;
static mutex subscribers_mutex;

// #1188: the bounds a subscriber is held to. A ping goes to every subscriber every
// kPingInterval (the shipped code's interval, unchanged); one that has not answered it
// with a pong within kPongWait is dropped, and so is one whose socket does not accept a
// frame within kWriteWait - the server's write timeout, which the shipped code left at
// httplib's default and never named. A subscriber that stops reading is therefore gone
// within kPingInterval + kPongWait of doing so, and docs/api.md states that number.
static const chrono::seconds kPingInterval{30};
static const chrono::seconds kPongWait{10};
static const chrono::seconds kWriteWait{5};

// httplib 0.14.3 hands a handler the peer's address and port and not the socket, and
// its DataSink only writes, so nothing ever read what a subscriber sent back - a pong,
// a close frame, or the end of the stream. The question this answers is not a Linux
// idiom: it is "enumerate the descriptors THIS process owns, ask each one who it is
// connected to, and take the one whose peer is this request's peer and whose local port
// is the port this request arrived on". Both platforms are asked exactly that, with the
// same two calls in the same order, matched on the same three numbers. Not found is
// kNoSubscriberSocket, in which case the subscriber is held to the write bound alone and
// the log says so.
//
// #1282 -- WHAT THE TWO CANDIDATE SETS ARE, AND WHERE THE EQUIVALENCE STOPS.
//
//   Linux  /proc/self/fd IS the descriptor table. The walk is exact, finite, and lists
//          every descriptor and no others.
//
//   Windows a Winsock socket is a kernel handle, and the handles a process owns live in
//          its handle table. Handle values are multiples of four, so the table is
//          sweepable by value; getpeername() on a value that is not a socket this process
//          owns answers WSAENOTSOCK and touches nothing. So the candidate set is the
//          handle table, asked the same two questions.
//
// The difference is that Windows publishes no enumeration of that table outside ntdll's
// NtQuerySystemInformation, which allocates the SYSTEM-WIDE handle list to answer a
// question about one process. So this walk has a CEILING rather than an end: the table
// is swept from the bottom up to a bound taken from the live handle count -- handles are
// reused, so the table stays a small multiple of what is open at once -- with a floor so
// a quiet process is still swept properly.
//
// Falling off the ceiling returns kNoSubscriberSocket, which is precisely the answer this
// function already gave when /proc held no match. So the failure mode of the Windows half
// is the behaviour Windows has today -- held to the write bound, and the connect line
// says so -- and never anything worse.
static subscriber_socket_t findSocketOf(const string &remote_addr, int remote_port, int local_port)
{
#ifdef _WIN32
    // How far up the handle table to sweep. A handle value is a multiple of four, so
    // slot n is handle 4n. The live count is what is open now; the table does not shrink,
    // so eight times it is a generous allowance for slots freed earlier, and the floor
    // covers a board with a handful of handles whose table is still a page.
    DWORD live = 0;
    if (!GetProcessHandleCount(GetCurrentProcess(), &live))
        live = 0;
    DWORD slots = (live > 512) ? live * 8 : 4096;
    if (slots > 65536)
        slots = 65536;

    const auto started = chrono::steady_clock::now();
    subscriber_socket_t found = kNoSubscriberSocket;
    DWORD sockets_seen = 0;
    for (DWORD slot = 1; slot <= slots; slot++)
    {
        const SOCKET candidate = (SOCKET)(ULONG_PTR)(slot * 4);
        sockaddr_in peer{};
        int len = (int)sizeof(peer);
        if (getpeername(candidate, (sockaddr *)&peer, &len) != 0 || peer.sin_family != AF_INET)
            continue;
        sockets_seen++;
        char text[INET_ADDRSTRLEN];
        if (!inet_ntop(AF_INET, &peer.sin_addr, text, sizeof(text)))
            continue;
        if (remote_addr != text || ntohs(peer.sin_port) != remote_port)
            continue;
        sockaddr_in local{};
        len = (int)sizeof(local);
        if (getsockname(candidate, (sockaddr *)&local, &len) != 0 || ntohs(local.sin_port) != local_port)
            continue;
        found = candidate;
        break;
    }
    // What the sweep cost, said in the log rather than argued in a comment: a reader on a
    // board that has grown a large handle table can see the number rather than guess it.
    const long long swept_us = chrono::duration_cast<chrono::microseconds>(
                                   chrono::steady_clock::now() - started)
                                   .count();
    log_debug("score socket: handle sweep for " + remote_addr + ":" + to_string(remote_port) +
              " looked at " + to_string((unsigned long)slots) + " slot(s), found " +
              to_string((unsigned long)sockets_seen) + " connected socket(s) in " +
              to_string(swept_us) + " us" + (haveSocket(found) ? "" : " and no match"));
    return found;
#else
    DIR *dir = opendir("/proc/self/fd");
    if (!dir)
        return -1;
    int found = -1;
    while (struct dirent *entry = readdir(dir))
    {
        if (entry->d_name[0] < '0' || entry->d_name[0] > '9')
            continue;
        int fd = atoi(entry->d_name);
        sockaddr_in peer{};
        socklen_t len = sizeof(peer);
        if (getpeername(fd, (sockaddr *)&peer, &len) != 0 || peer.sin_family != AF_INET)
            continue;
        char text[INET_ADDRSTRLEN];
        if (!inet_ntop(AF_INET, &peer.sin_addr, text, sizeof(text)))
            continue;
        if (remote_addr != text || ntohs(peer.sin_port) != remote_port)
            continue;
        sockaddr_in local{};
        len = sizeof(local);
        if (getsockname(fd, (sockaddr *)&local, &len) != 0 || ntohs(local.sin_port) != local_port)
            continue;
        found = fd;
        break;
    }
    closedir(dir);
    return found;
#endif
}

// What a subscriber sends back. A client's frames are masked (RFC 6455 5.1).
struct ClientFrame
{
    uint8_t opcode = 0;
    vector<uint8_t> payload;
};

// Takes one complete frame off the front of `buffer`; false while there is not one.
// A frame claiming more than a megabyte is not something a subscriber sends a board,
// and is answered as a close so the connection ends rather than the buffer growing.
static bool takeClientFrame(vector<uint8_t> &buffer, ClientFrame &out)
{
    if (buffer.size() < 2)
        return false;
    size_t offset = 2;
    uint64_t length = buffer[1] & 0x7F;
    const bool masked = buffer[1] & 0x80;
    if (length == 126)
    {
        if (buffer.size() < 4)
            return false;
        length = ((uint64_t)buffer[2] << 8) | buffer[3];
        offset = 4;
    }
    else if (length == 127)
    {
        if (buffer.size() < 10)
            return false;
        length = 0;
        for (int i = 2; i < 10; i++)
            length = (length << 8) | buffer[i];
        offset = 10;
    }
    if (length > (1u << 20))
    {
        out.opcode = 0x8;
        out.payload.clear();
        buffer.clear();
        return true;
    }
    if (masked)
        offset += 4;
    if (buffer.size() < offset + length)
        return false;
    out.opcode = buffer[0] & 0x0F;
    out.payload.assign(buffer.begin() + offset, buffer.begin() + offset + length);
    if (masked)
        for (size_t i = 0; i < length; i++)
            out.payload[i] ^= buffer[offset - 4 + (i % 4)];
    buffer.erase(buffer.begin(), buffer.begin() + offset + length);
    return true;
}

static string describeSeconds(chrono::steady_clock::duration d)
{
    ostringstream s;
    s << fixed << setprecision(1) << chrono::duration<double>(d).count() << " s";
    return s.str();
}

WebSocketService::WebSocketService(shared_ptr<ScoreQueue> queue, ScoreSocketSettings settings, bool debug_mode)
    : score_queue_(queue), settings_(std::move(settings)), port_(settings_.port), debug_mode_(debug_mode) {}

WebSocketService::~WebSocketService()
{
    stop();
}

void WebSocketService::start()
{
    if (running_)
        return;

    // #816: shipped code constructs server_ inside the worker thread and reads the
    // same pointer from the main thread's stop(). Constructing it here makes the
    // write happen-before the thread that reads it.
    if (od_fix::shutdownFix())
    {
        server_ = make_unique<httplib::Server>();
    }

    running_ = true;
    worker_thread_ = thread(&WebSocketService::run, this);
    log_debug("WebSocket service starting on port " + to_string(port_));
}

// #816: httplib::Server::stop() asserts svr_sock_ != INVALID_SOCKET while is_running_
// is still true. Two callers - this class's stop() on the main thread and run()'s own
// tail on the worker thread - call it on one server, and the second one to arrive
// asserts if the listening thread has not yet cleared is_running_. Under load it has
// not. One flag, so the socket is closed exactly once.
static std::once_flag od816_server_stop_once;

static std::atomic<int> od816_stop_calls{0};

void WebSocketService::stopServerOnce()
{
    if (od_fix::shutdownTrace())
    {
        int n = ++od816_stop_calls;
        std::ostringstream tid;
        tid << std::this_thread::get_id();
        log_info("SHUTDOWN TRACE: server stop call #" + to_string(n) +
                 " thread=" + tid.str() +
                 " server_=" + (server_ ? "set" : "null") +
                 " is_running=" + to_string(server_ ? server_->is_running() : false));
    }
    if (!server_)
        return;
    if (od_fix::shutdownFix())
    {
        std::call_once(od816_server_stop_once, [this]
                       { server_->stop(); });
        return;
    }
    server_->stop();
}

void WebSocketService::stop()
{
    running_ = false;

    // Mark all subscribers as inactive, and wake their writers so they leave
    {
        lock_guard<mutex> lock(subscribers_mutex);
        for (auto &subscriber : subscribers)
        {
            subscriber->active = false;
            subscriber->cv.notify_all();
        }
    }

    stopServerOnce();
    if (worker_thread_.joinable())
    {
        worker_thread_.join();
    }
    log_info("WebSocket service stopped");
}

void WebSocketService::run()
{
    if (!server_)
    {
        server_ = make_unique<httplib::Server>();
    }

    try
    {
        // #1188: the bound a frame is given to be accepted by a subscriber's socket.
        server_->set_write_timeout(kWriteWait.count(), 0);

        // CORS headers
        server_->set_default_headers({{"Access-Control-Allow-Origin", "*"},
                                      {"Access-Control-Allow-Methods", "GET, POST, OPTIONS"},
                                      {"Access-Control-Allow-Headers", "Content-Type"}});

        // WebSocket endpoint with REAL streaming
        server_->Get("/scores", [&](const httplib::Request &req, httplib::Response &res)
                     {
            // Check for WebSocket upgrade
            if (req.get_header_value("Upgrade") == "websocket" &&
                req.get_header_value("Connection").find("Upgrade") != string::npos) {

                // #1187: a subscriber presents the token as a query parameter, on
                // loopback as on the network. Refused before the key is looked at, so
                // a refusal is one status and one log line, and the log line never
                // carries what was presented.
                if (!score_token::equals(req.get_param_value("token"), settings_.token)) {
                    res.status = 401;
                    res.set_content("a token is required: ws://<host>:" + to_string(port_) +
                                        "/scores?token=<token>  (opendartboard --show-token prints it)\n",
                                    "text/plain");
                    log_warning("score socket: upgrade from " + req.remote_addr + " refused with 401, " +
                                (req.has_param("token") ? "wrong token" : "no token"));
                    return;
                }
                
                string websocket_key = req.get_header_value("Sec-WebSocket-Key");
                if (websocket_key.empty()) {
                    res.status = 400;
                    return;
                }
                
                // Generate accept key
                string accept_key = generate_websocket_accept(websocket_key);
                
                // Send WebSocket handshake response
                res.status = 101;
                res.set_header("Upgrade", "websocket");
                res.set_header("Connection", "Upgrade");
                res.set_header("Sec-WebSocket-Accept", accept_key);
                
                // #1188: the subscriber's socket, so its pongs and its close can be read.
                const string peer = req.remote_addr + ":" + to_string(req.remote_port);
                const subscriber_socket_t fd = findSocketOf(req.remote_addr, req.remote_port, req.local_port);

                // The subscriber's own writer and reader, on httplib's thread for this
                // connection. It drains the outbox in order, reads what the subscriber
                // sends back, pings on the interval and holds the subscriber to the bounds.
                res.set_content_provider(
                    "application/octet-stream",
                    [&, peer, fd](size_t offset, httplib::DataSink& sink) -> bool {
                        auto subscriber = make_shared<Subscriber>();
                        subscriber->peer = peer;
                        subscriber->fd = fd;
                        {
                            lock_guard<mutex> lock(subscribers_mutex);
                            subscribers.push_back(subscriber);
                        }
                        log_info("score socket: subscriber " + peer + " connected" +
                                 (haveSocket(fd) ? "" : " (its socket was not found; held to the write bound only)"));

                        const auto connected = chrono::steady_clock::now();
                        auto last_ping = connected;
                        optional<chrono::steady_clock::time_point> ping_unanswered; // when the outstanding ping went out
                        vector<uint8_t> inbound;
                        string ended;            // why the connection ended; empty while it is alive
                        bool dropped = false;    // true when the board ended it, false when the subscriber did
                        size_t delivered = 0;

                        auto write_frame = [&](const vector<uint8_t>& frame) -> bool {
                            const auto started = chrono::steady_clock::now();
                            bool ok = false;
                            try {
                                ok = sink.write((const char*)frame.data(), frame.size());
                            } catch (...) {
                                ok = false;
                            }
                            if (!ok && ended.empty()) {
                                ended = "its socket did not accept a frame within " + to_string(kWriteWait.count()) +
                                        " s (the write returned after " + describeSeconds(chrono::steady_clock::now() - started) + ")";
                                dropped = true;
                            }
                            return ok;
                        };

                        while (running_ && subscriber->active && ended.empty()) {
                            // 1. everything published since the last pass, in order
                            deque<vector<uint8_t>> pending;
                            {
                                unique_lock<mutex> lock(subscriber->m);
                                subscriber->cv.wait_for(lock, chrono::milliseconds(100), [&] {
                                    return !subscriber->outbox.empty() || !subscriber->active || !running_;
                                });
                                pending.swap(subscriber->outbox);
                            }
                            while (!pending.empty() && write_frame(pending.front())) {
                                delivered++;
                                pending.pop_front();
                            }
                            if (!pending.empty()) {
                                lock_guard<mutex> lock(subscriber->m);
                                subscriber->outbox.insert(subscriber->outbox.begin(), pending.begin(), pending.end());
                                break;
                            }

                            // 2. what the subscriber sent back: a pong, a ping, a close, or nothing more
                            if (haveSocket(fd)) {
#ifdef _WIN32
                                // #1282: the same drain, with the one call Windows has for
                                // it. There is no MSG_DONTWAIT here, and ioctlsocket(FIONBIO)
                                // is not an option: this socket is httplib's and httplib
                                // writes to it, blocking, on this very thread -- putting it
                                // in non-blocking mode would change how the board's own
                                // frames are written. select() with a zero timeout asks
                                // whether a recv would block without altering the socket at
                                // all, so nothing but this loop can tell the difference.
                                char buf[4096];
                                for (;;) {
                                    fd_set readable;
                                    FD_ZERO(&readable);
                                    FD_SET(fd, &readable);
                                    timeval nowait{0, 0};
                                    const int ready = select(0, &readable, nullptr, nullptr, &nowait);
                                    if (ready == 0) {
                                        break; // nothing waiting; ask again next pass
                                    }
                                    if (ready < 0) {
                                        ended = "the connection failed: select reported Winsock error " +
                                                to_string(WSAGetLastError());
                                        break;
                                    }
                                    const int n = recv(fd, buf, (int)sizeof(buf), 0);
                                    if (n > 0) {
                                        inbound.insert(inbound.end(), (const uint8_t *)buf, (const uint8_t *)buf + n);
                                        continue;
                                    }
                                    if (n == 0) {
                                        ended = "the subscriber closed the connection";
                                    } else {
                                        const int error = WSAGetLastError();
                                        if (error != WSAEWOULDBLOCK && error != WSAEINTR)
                                            ended = "the connection failed: Winsock error " + to_string(error);
                                    }
                                    break;
                                }
#else
                                uint8_t buf[4096];
                                for (;;) {
                                    const ssize_t n = recv(fd, buf, sizeof(buf), MSG_DONTWAIT);
                                    if (n > 0) {
                                        inbound.insert(inbound.end(), buf, buf + n);
                                        continue;
                                    }
                                    if (n == 0) {
                                        ended = "the subscriber closed the connection";
                                    } else if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
                                        ended = string("the connection failed: ") + strerror(errno);
                                    }
                                    break;
                                }
#endif
                                ClientFrame frame;
                                while (ended.empty() && takeClientFrame(inbound, frame)) {
                                    if (frame.opcode == 0xA) {
                                        ping_unanswered.reset();
                                    } else if (frame.opcode == 0x9) {
                                        write_frame(create_control_frame(0xA, frame.payload));
                                    } else if (frame.opcode == 0x8) {
                                        write_frame(create_control_frame(0x8, {}));
                                        if (ended.empty())
                                            ended = "the subscriber sent a close frame";
                                    }
                                    // a data frame from a subscriber is nothing the board reads; ignored
                                }
                                if (!ended.empty())
                                    break;
                            }

                            // 3. liveness: a ping on the interval, and a pong within the bound
                            const auto now = chrono::steady_clock::now();
                            if (ping_unanswered && now - *ping_unanswered >= kPongWait) {
                                ended = "no pong within " + to_string(kPongWait.count()) + " s of the ping sent " +
                                        describeSeconds(*ping_unanswered - connected) + " after it connected";
                                dropped = true;
                                break;
                            }
                            if (now - last_ping >= kPingInterval) {
                                if (!write_frame(create_control_frame(0x9, {})))
                                    break;
                                last_ping = now;
                                if (haveSocket(fd))
                                    ping_unanswered = now;
                            }
                        }

                        // The board stopping is the one ending the subscriber is told about:
                        // a close frame, so a client reads a close rather than a cut stream.
                        if (ended.empty() && !dropped)
                            write_frame(create_control_frame(0x8, {}));

                        size_t undelivered = 0;
                        {
                            lock_guard<mutex> lock(subscribers_mutex);
                            subscribers.erase(remove(subscribers.begin(), subscribers.end(), subscriber), subscribers.end());
                            lock_guard<mutex> own(subscriber->m);
                            undelivered = subscriber->outbox.size();
                            subscriber->active = false;
                        }
                        const string lifetime = describeSeconds(chrono::steady_clock::now() - connected);
                        if (dropped) {
                            log_warning("score socket: subscriber " + peer + " dropped after " + lifetime + ": " + ended +
                                        "; " + to_string(delivered) + " message(s) delivered, " + to_string(undelivered) + " undelivered");
                        } else {
                            log_info("score socket: subscriber " + peer + " disconnected after " + lifetime + ": " +
                                     (ended.empty() ? string("the board is stopping") : ended) +
                                     "; " + to_string(delivered) + " message(s) delivered");
                        }
                        return false; // End streaming
                    }
                );

                return;
            }
            
            // Regular HTTP response
            res.set_content("WebSocket endpoint. Connect with ws://localhost:" + to_string(port_) + "/scores", "text/plain"); });

        // Health check
        server_->Get("/health", [](const httplib::Request &req, httplib::Response &res)
                     { res.set_content("{\"status\":\"ok\",\"service\":\"OpenDartboard\"}", "application/json"); });

        // Configuration endpoints
        server_->Get("/config", [](const httplib::Request &req, httplib::Response &res)
                     {
            json config;
            // TODO: return actual configuration
            config["todo"] = "yes";
            res.set_content(config.dump(), "application/json"); });

        server_->Put("/config", [](const httplib::Request &req, httplib::Response &res)
                     {
            try {
                json new_config = json::parse(req.body);
                // TODO: Apply configuration changes
                log_info("TODO: Configuration updated: " + new_config.dump());
                res.set_content("{\"status\":\"(TODO)updated\"}", "application/json");
            } catch (const exception& e) {
                res.status = 400;
                res.set_content("{\"error\":\"Invalid JSON\"}", "application/json");
            } });

        // Calibration endpoints
        server_->Post("/calibrate/start", [](const httplib::Request &req, httplib::Response &res)
                      {
            // TODO: Start calibration process
            res.set_content("{\"status\":\"(TODO)calibration_started\"}", "application/json"); });

        server_->Get("/calibrate/status", [](const httplib::Request &req, httplib::Response &res)
                     {
            json status;
            // TODO: return actual calibration status
            status["todo"] = "yes";
            res.set_content(status.dump(), "application/json"); });

        // #812: the three routes below are the saved camera frames and the log tail.
        // They were registered unconditionally, so a release build served the images
        // of somebody's room on 0.0.0.0:13520 with no credential and no flag asked
        // for -- the same exposure 8081 and 8088 were closed for, reached through a
        // listener no build configuration closes because the score API is the
        // product. They are now behind exactly the gate the MJPEG listeners are:
        // the debug build define AND --debug. A release build serves no camera
        // image on any port, whatever flags it is given. The score API above is
        // the documented interface and is untouched.
#ifdef DEBUG_VIA_VIDEO_INPUT
        if (debug_mode_)
        {
        server_->Get("/debug/list", [](const httplib::Request &req, httplib::Response &res)
                     {
            json files = json::array();
            
            for (const auto& entry : filesystem::recursive_directory_iterator("debug_frames")) {
                if (entry.is_regular_file() && entry.path().extension() == ".jpg") {
                    string path = entry.path().string();
                    // Remove debug_frames/ prefix - C++17 compatible
                    if (path.substr(0, 13) == "debug_frames/") {
                        path = path.substr(13);
                    }
                    files.push_back(path);
                }
            }
            
            res.set_content(files.dump(), "application/json"); });

        server_->Get("/debug/logs", [](const httplib::Request &req, httplib::Response &res)
                     {
            // Last N lines, read in process: no shell, no /tmp, and no second
            // request racing the first over one shared scratch file.
            ifstream file("debug_frames/opendartboard.log");
            if (!file) {
                res.set_content("[]", "application/json");
                return;
            }

            const size_t kMaxLines = 100;
            deque<string> tail;
            string line;
            while (getline(file, line)) {
                if (line.empty()) {
                    continue;
                }
                tail.push_back(line);
                if (tail.size() > kMaxLines) {
                    tail.pop_front();
                }
            }

            json logs = json::array();
            for (const auto &l : tail) {
                logs.push_back(l);
            }
            
            res.set_content(logs.dump(), "application/json"); });

        server_->Get(R"(/debug/(.+))", [](const httplib::Request &req, httplib::Response &res)
                     {
            // #812: the caller names the path and it was concatenated onto the
            // directory unnormalised, so what the handler opened was not bounded by
            // the directory it is about. Normalise, then refuse anything that does
            // not stay under it.
            const filesystem::path root("debug_frames");
            const filesystem::path want = (root / req.matches[1].str()).lexically_normal();
            const filesystem::path rel = want.lexically_relative(root);
            if (want.is_absolute() || rel.empty() || *rel.begin() == "..") {
                res.status = 404;
                return;
            }

            ifstream file(want, ios::binary);
            if (!file) {
                res.status = 404;
                return;
            }

            string content((istreambuf_iterator<char>(file)), istreambuf_iterator<char>());
            res.set_content(content, "image/jpeg"); });
        }
#else
        (void)debug_mode_;
#endif

        server_->Get("/info", [](const httplib::Request &req, httplib::Response &res)
                     {
            ifstream file("cache/info.json");
            if (!file) {
                res.set_content("{\"error\":\"info.json not found\"}", "application/json");
                return;
            }
            
            string content((istreambuf_iterator<char>(file)), istreambuf_iterator<char>());
            res.set_content(content, "application/json"); });

        // Start server thread. #1187: the address is the setting's - loopback unless
        // --listen - and the log says which, so a reader can tell from the log alone
        // whether the board is on the network.
        //
        // #1295: the thread does nothing but listen, and says nothing. Both "listening"
        // lines used to be printed by this lambda BEFORE it called listen(), so a board
        // whose port was already taken printed two confident sentences and then the
        // correction underneath them - which is the opposite of what #1187 added them for.
        // Nothing claims a socket is open until listen() has said so, below.
        const string bind = settings_.bind_address;
        const bool on_network = bind != "127.0.0.1" && bind != "localhost" && bind != "::1";
        thread server_thread([&, bind]()
                             {
            if (!server_->listen(bind.c_str(), port_))
                log_error("score socket: could not listen on " + bind + ":" + to_string(port_) +
                          " (is the port already in use?)"); });

        // #1295: wait for listen() to answer, and record the answer for main.
        //
        // wait_until_ready() ends on either outcome rather than spinning on the good one:
        // listen() carries its own scope_exit setting done_, so a bind that fails returns
        // and ends the wait, while a bind that succeeds sets is_running_ and ends it too.
        // That makes this a race-free "did the socket open?" with no future and no promise.
        server_->wait_until_ready();
        socket_listening_ = server_->is_running();
        socket_resolved_ = true;

        if (socket_listening_)
        {
            log_info("WebSocket server listening on ws://" + bind + ":" + to_string(port_) + "/scores" +
                     (on_network ? " (open on the network; a subscriber presents ?token=)"
                                 : " (loopback only; --listen opens it on the network, a subscriber presents ?token=)"));
            log_info("Rest server listening on http://" + bind + ":" + to_string(port_) + "/");
        }
        else
        {
            log_warning("score socket: nothing is listening on " + bind + ":" + to_string(port_) +
                        "; this board scores but no subscriber can reach it");
        }

        // MAIN BROADCASTING LOOP - this is where the magic happens!
        while (running_)
        {
            DetectorResult result;
            if (score_queue_->pop(result, 100))
            {
                if (result.dart_detected)
                {
                    string json_message = formatScoreJson(result);
                    broadcastScore(json_message);
                }
            }
        }

        stopServerOnce();
        if (server_thread.joinable())
        {
            server_thread.join();
        }
    }
    catch (const exception &e)
    {
        log_error("WebSocket server error: " + string(e.what()));
        // #1295: a caller waiting on the socket's answer gets one here too. Without this,
        // a throw on the way to listen() would leave awaitScoreSocket() waiting for an
        // attempt that is never going to be made.
        socket_listening_ = false;
        socket_resolved_ = true;
    }
}

// #1295: the socket's own answer, waited for. Returns when listen() has answered - or at
// once if this service was never started or has already been stopped, which are both
// honestly "no socket". Nothing here reads a clock.
bool WebSocketService::awaitScoreSocket()
{
    while (running_ && !socket_resolved_)
    {
        this_thread::sleep_for(chrono::milliseconds(1));
    }
    return socket_resolved_ && socket_listening_;
}

void WebSocketService::broadcastScore(const string &json_message)
{
    // #1188: one frame on every subscriber's outbox, under a lock held for as long as
    // that takes and no longer. No socket is written here: a subscriber that is not
    // taking frames blocks its own writer, and this loop never waits for anybody.
    const vector<uint8_t> frame = create_websocket_frame(json_message);
    size_t count = 0;
    {
        lock_guard<mutex> lock(subscribers_mutex);
        for (auto &subscriber : subscribers)
        {
            if (!subscriber->active)
                continue;
            {
                lock_guard<mutex> own(subscriber->m);
                subscriber->outbox.push_back(frame);
            }
            subscriber->cv.notify_one();
            count++;
        }
    }
    if (count > 0)
    {
        log_debug("Broadcasted score '" + json_message + "' to " + to_string(count) + " WebSocket clients");
    }
}

string WebSocketService::formatScoreJson(const DetectorResult &result)
{
    json j;
    j["score"] = result.score;
    j["position"] = {
        {"x", (int)result.position.x},
        {"y", (int)result.position.y}};
    j["confidence"] = result.confidence;
    j["camera"] = result.camera_index;
    // #1186: the dart in the board's frame. Every field is either a value or null; an
    // absence is never a zero, because 0 is a real angle and a real radius.
    if (result.segment >= 1 && result.segment <= 20)
        j["segment"] = result.segment;
    else
        j["segment"] = nullptr;
    if (!result.ring.empty())
        j["ring"] = result.ring;
    else
        j["ring"] = nullptr;
    if (result.board_radius_known)
    {
        json board;
        board["radius"] = result.board_radius;
        if (result.board_angle_known)
            board["angle"] = result.board_angle;
        else
            board["angle"] = nullptr;
        j["board"] = board;
    }
    else
    {
        j["board"] = nullptr;
    }
    j["processing_time"] = result.processing_time_ms;
    j["timestamp"] = chrono::duration_cast<chrono::milliseconds>(
                         chrono::system_clock::now().time_since_epoch())
                         .count();

    return j.dump();
}