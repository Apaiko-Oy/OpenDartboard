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
#include <dirent.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

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
struct Subscriber
{
    string peer;                   // address:port, for the log
    int fd = -1;                   // the socket, resolved from the peer; -1 when it could not be
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
// a close frame, or the end of the stream. On Linux every open descriptor is listed
// under /proc/self/fd, and the one whose peer is this request's peer is this request's
// socket. -1 when it is not found (an IPv6 peer, or no /proc), in which case the
// subscriber is held to the write bound alone and the log says so.
static int findSocketOf(const string &remote_addr, int remote_port, int local_port)
{
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

WebSocketService::WebSocketService(shared_ptr<ScoreQueue> queue, ScoreSocketSettings settings)
    : score_queue_(queue), settings_(std::move(settings)), port_(settings_.port) {}

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
                const int fd = findSocketOf(req.remote_addr, req.remote_port, req.local_port);

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
                                 (fd < 0 ? " (its socket was not found; held to the write bound only)" : ""));

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
                            if (fd >= 0) {
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
                                if (fd >= 0)
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
            string path = "debug_frames/" + req.matches[1].str();
            
            ifstream file(path, ios::binary);
            if (!file) {
                res.status = 404;
                return;
            }
            
            string content((istreambuf_iterator<char>(file)), istreambuf_iterator<char>());
            res.set_content(content, "image/jpeg"); });

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
        const string bind = settings_.bind_address;
        const bool on_network = bind != "127.0.0.1" && bind != "localhost" && bind != "::1";
        thread server_thread([&, bind, on_network]()
                             {
            log_info("WebSocket server listening on ws://" + bind + ":" + to_string(port_) + "/scores" +
                     (on_network ? " (open on the network; a subscriber presents ?token=)"
                                 : " (loopback only; --listen opens it on the network, a subscriber presents ?token=)"));
            log_info("Rest server listening on http://" + bind + ":" + to_string(port_) + "/");
            if (!server_->listen(bind.c_str(), port_))
                log_error("score socket: could not listen on " + bind + ":" + to_string(port_)); });

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
    }
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