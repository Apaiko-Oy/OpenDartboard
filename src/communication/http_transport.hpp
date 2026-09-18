#pragma once
// #822: one outbound POST, and the two ways this program is allowed to make one.
//
// The seam exists for the same reason capture.hpp's does (#802): the thing that differs
// between Linux and Windows is a library, not a policy, so the policy above it is written
// once. There are exactly two implementations and neither of them may block for longer
// than the timeouts stated here.
//
//   POSIX    httplib::Client, which the tree already fetches for the score server.
//            Built here WITHOUT CPPHTTPLIB_OPENSSL_SUPPORT, because od-amd64:bullseye
//            has no libssl-dev. So this transport cannot speak TLS and says so by name.
//   Windows  WinHTTP. winhttp.dll is an in-box Windows DLL, so TLS costs the artefact
//            nothing -- no OpenSSL, no extra file, no redistributable. This is the
//            transport the deployed board uses and it is the one that can do https.
//
// A transport that cannot do TLS refuses an https:// URL rather than downgrading it.
// That refusal is the point: a bearer token on a plaintext wire is the failure this
// whole file exists to make impossible to reach by accident.

#include "../utils/od_socket.hpp" // winsock2 before anything can pull in windows.h
#include <string>
#include <map>

#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")
#else
#include <httplib.h>
#endif

namespace odhttp
{
    struct Response
    {
        /** 0 means the request never reached a server: DNS, connect, timeout, TLS. */
        int status = 0;
        std::string body;
        /** Why status is 0. Never carries a request header, so never carries a token. */
        std::string transport_error;
        /**
         * #1259: the seconds a 429 names in `Retry-After`, or -1 when the answer named none.
         * Read because a person typing a pairing code is told how long to wait, and the
         * `api-pairing` limiter says it there and nowhere else. A response header, never a
         * request one, so still never a token.
         */
        int retry_after_s = -1;

        bool reached_a_server() const { return status != 0; }
    };

    struct Url
    {
        bool tls = false;
        std::string host;
        int port = 0;
        std::string path_prefix; // "" or "/something", never a trailing slash
        bool valid = false;
    };

    /** Split "https://host:port/prefix" without pulling a URL library in for it. */
    inline Url parseUrl(const std::string &url)
    {
        Url u;
        std::string rest;
        if (url.rfind("https://", 0) == 0)
        {
            u.tls = true;
            rest = url.substr(8);
        }
        else if (url.rfind("http://", 0) == 0)
        {
            u.tls = false;
            rest = url.substr(7);
        }
        else
        {
            return u;
        }
        size_t slash = rest.find('/');
        std::string authority = slash == std::string::npos ? rest : rest.substr(0, slash);
        if (slash != std::string::npos)
        {
            u.path_prefix = rest.substr(slash);
            while (!u.path_prefix.empty() && u.path_prefix.back() == '/')
            {
                u.path_prefix.pop_back();
            }
        }
        size_t colon = authority.rfind(':');
        if (colon != std::string::npos && authority.find(']') == std::string::npos)
        {
            u.host = authority.substr(0, colon);
            u.port = atoi(authority.c_str() + colon + 1);
        }
        else
        {
            u.host = authority;
            u.port = u.tls ? 443 : 80;
        }
        u.valid = !u.host.empty() && u.port > 0;
        return u;
    }

    /** True when this build can put a request on a TLS wire. */
    inline bool tlsAvailable()
    {
#ifdef _WIN32
        return true;
#else
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
        return true;
#else
        return false;
#endif
#endif
    }

    inline const char *transportName()
    {
#ifdef _WIN32
        return "winhttp";
#else
        return "httplib";
#endif
    }

    /**
     * One request. Blocks for at most connect_timeout_s + read_timeout_s and never
     * longer; it is called only from a worker thread or from a one-shot command, never
     * from the scoring loop.
     *
     * `headers` may carry Authorization. Nothing in this function logs, and nothing in
     * it copies a header into Response.
     *
     * #1305 gave it a verb. It was `postJson` and had "POST" written into it in two
     * places; the update check reads a manifest, which is a GET of nobody's business at
     * an open address, and a second copy of this function to say so would have been a
     * second copy of the TLS refusal above it.
     */
    inline Response perform(const Url &u, const char *method, const std::string &path, const std::string &body,
                            const std::map<std::string, std::string> &headers,
                            int connect_timeout_s, int read_timeout_s)
    {
        Response r;
        if (!u.valid)
        {
            r.transport_error = "malformed base url";
            return r;
        }
        if (u.tls && !tlsAvailable())
        {
            r.transport_error = "this build has no TLS transport";
            return r;
        }

#ifdef _WIN32
        auto widen = [](const std::string &s)
        {
            std::wstring w(s.begin(), s.end());
            return w;
        };
        HINTERNET session = WinHttpOpen(L"OpenDartboard", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!session)
        {
            r.transport_error = "WinHttpOpen failed";
            return r;
        }
        WinHttpSetTimeouts(session, connect_timeout_s * 1000, connect_timeout_s * 1000,
                           read_timeout_s * 1000, read_timeout_s * 1000);
        HINTERNET connection = WinHttpConnect(session, widen(u.host).c_str(), (INTERNET_PORT)u.port, 0);
        if (!connection)
        {
            WinHttpCloseHandle(session);
            r.transport_error = "WinHttpConnect failed";
            return r;
        }
        std::wstring wpath = widen(u.path_prefix + path);
        std::wstring wmethod = widen(std::string(method));
        HINTERNET request = WinHttpOpenRequest(connection, wmethod.c_str(), wpath.c_str(), nullptr,
                                               WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                               u.tls ? WINHTTP_FLAG_SECURE : 0);
        if (!request)
        {
            WinHttpCloseHandle(connection);
            WinHttpCloseHandle(session);
            r.transport_error = "WinHttpOpenRequest failed";
            return r;
        }
        std::string header_block = body.empty() ? std::string() : std::string("Content-Type: application/json\r\n");
        for (const auto &kv : headers)
        {
            header_block += kv.first + ": " + kv.second + "\r\n";
        }
        std::wstring wheaders = widen(header_block);
        BOOL sent = WinHttpSendRequest(request, header_block.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : wheaders.c_str(),
                                       header_block.empty() ? 0 : (DWORD)-1,
                                       (LPVOID)body.data(), (DWORD)body.size(),
                                       (DWORD)body.size(), 0);
        if (sent)
        {
            sent = WinHttpReceiveResponse(request, nullptr);
        }
        if (!sent)
        {
            r.transport_error = "WinHttp send/receive failed, GetLastError=" + std::to_string((unsigned long)GetLastError());
            WinHttpCloseHandle(request);
            WinHttpCloseHandle(connection);
            WinHttpCloseHandle(session);
            return r;
        }
        DWORD code = 0, code_size = sizeof(code);
        WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &code, &code_size, WINHTTP_NO_HEADER_INDEX);
        r.status = (int)code;
        DWORD retry_after = 0, retry_after_size = sizeof(retry_after);
        if (WinHttpQueryHeaders(request, WINHTTP_QUERY_RETRY_AFTER | WINHTTP_QUERY_FLAG_NUMBER,
                                WINHTTP_HEADER_NAME_BY_INDEX, &retry_after, &retry_after_size, WINHTTP_NO_HEADER_INDEX))
        {
            r.retry_after_s = (int)retry_after;
        }
        for (;;)
        {
            DWORD available = 0;
            if (!WinHttpQueryDataAvailable(request, &available) || available == 0)
            {
                break;
            }
            std::string chunk(available, '\0');
            DWORD read = 0;
            if (!WinHttpReadData(request, &chunk[0], available, &read))
            {
                break;
            }
            chunk.resize(read);
            r.body += chunk;
        }
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        return r;
#else
        httplib::Client client(u.host, u.port);
        client.set_connection_timeout(connect_timeout_s, 0);
        client.set_read_timeout(read_timeout_s, 0);
        client.set_write_timeout(read_timeout_s, 0);
        client.set_keep_alive(false);
        httplib::Headers h;
        for (const auto &kv : headers)
        {
            h.emplace(kv.first, kv.second);
        }
        auto res = std::string(method) == "GET" ? client.Get((u.path_prefix + path).c_str(), h)
                                                : client.Post((u.path_prefix + path).c_str(), h, body, "application/json");
        if (!res)
        {
            r.transport_error = httplib::to_string(res.error());
            return r;
        }
        r.status = res->status;
        r.body = res->body;
        if (res->has_header("Retry-After"))
        {
            r.retry_after_s = atoi(res->get_header_value("Retry-After").c_str());
        }
        return r;
#endif
    }

    /** One POST of a JSON body. */
    inline Response postJson(const Url &u, const std::string &path, const std::string &body,
                             const std::map<std::string, std::string> &headers,
                             int connect_timeout_s, int read_timeout_s)
    {
        return perform(u, "POST", path, body, headers, connect_timeout_s, read_timeout_s);
    }

    /** One GET. #1305 reads a signed manifest with it and sends no credential. */
    inline Response get(const Url &u, const std::string &path, int connect_timeout_s, int read_timeout_s)
    {
        return perform(u, "GET", path, std::string(), std::map<std::string, std::string>(), connect_timeout_s,
                       read_timeout_s);
    }
}
