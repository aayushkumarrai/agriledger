// Minimal HTTP/1.1 and WebSocket test clients over POSIX sockets (no extra dependencies).
#pragma once
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <map>
#include <optional>
#include <random>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace testhttp {

using json = nlohmann::json;

struct Response {
    int status = 0;
    std::map<std::string, std::string> headers;   // lower-case names
    std::vector<std::string> set_cookies;
    std::string body;
    json j() const { return json::parse(body, nullptr, false); }
};

inline int connect_local(std::uint16_t port) {
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    ::inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    timeval tv{10, 0};
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0) { ::close(fd); return -1; }
    return fd;
}

inline void send_all(int fd, const std::string& data) {
    std::size_t off = 0;
    while (off < data.size()) {
        const ssize_t n = ::send(fd, data.data() + off, data.size() - off, MSG_NOSIGNAL);
        if (n <= 0) return;
        off += static_cast<std::size_t>(n);
    }
}

inline std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}

inline Response parse_response(const std::string& raw) {
    Response r;
    const auto split = raw.find("\r\n\r\n");
    const std::string head = raw.substr(0, split);
    r.body = split == std::string::npos ? "" : raw.substr(split + 4);
    std::size_t pos = head.find("\r\n");
    r.status = std::atoi(head.substr(9, 3).c_str());
    while (pos != std::string::npos && pos < head.size()) {
        const std::size_t end = head.find("\r\n", pos + 2);
        const std::string line = head.substr(pos + 2, end == std::string::npos ? std::string::npos : end - pos - 2);
        const auto colon = line.find(':');
        if (colon != std::string::npos) {
            std::string value = line.substr(colon + 1);
            while (!value.empty() && value.front() == ' ') value.erase(value.begin());
            const std::string name = lower(line.substr(0, colon));
            if (name == "set-cookie") r.set_cookies.push_back(value);
            r.headers[name] = value;
        }
        pos = end;
    }
    return r;
}

using Headers = std::vector<std::pair<std::string, std::string>>;

inline Response request(std::uint16_t port, const std::string& method, const std::string& path, const std::string& body = "",
                        const Headers& headers = {}) {
    const int fd = connect_local(port);
    if (fd < 0) return {};
    std::string req = method + " " + path + " HTTP/1.1\r\nHost: 127.0.0.1:" + std::to_string(port) + "\r\nConnection: close\r\n";
    for (const auto& h : headers) req += h.first + ": " + h.second + "\r\n";
    if (!body.empty() || method == "POST" || method == "PATCH") req += "Content-Length: " + std::to_string(body.size()) + "\r\n";
    req += "\r\n" + body;
    send_all(fd, req);
    std::string raw;
    char buf[8192];
    for (;;) {
        const ssize_t n = ::recv(fd, buf, sizeof buf, 0);
        if (n <= 0) break;
        raw.append(buf, static_cast<std::size_t>(n));
    }
    ::close(fd);
    return parse_response(raw);
}

// Several GET requests down ONE connection (keep-alive), the way a browser loads a page. Responses are read by Content-Length.
inline std::vector<Response> request_many(std::uint16_t port, const std::vector<std::string>& paths, const std::string& cookie = "") {
    std::vector<Response> out;
    const int fd = connect_local(port);
    if (fd < 0) return out;
    std::string pending;
    for (const std::string& path : paths) {
        std::string req = "GET " + path + " HTTP/1.1\r\nHost: 127.0.0.1:" + std::to_string(port) + "\r\nConnection: keep-alive\r\n";
        if (!cookie.empty()) req += "Cookie: " + cookie + "\r\n";
        send_all(fd, req + "\r\n");
        std::size_t split;
        while ((split = pending.find("\r\n\r\n")) == std::string::npos) {
            char buf[8192];
            const ssize_t n = ::recv(fd, buf, sizeof buf, 0);
            if (n <= 0) { ::close(fd); return out; }
            pending.append(buf, static_cast<std::size_t>(n));
        }
        Response head = parse_response(pending.substr(0, split + 4));
        const std::size_t length = head.headers.count("content-length") ? std::stoul(head.headers["content-length"]) : 0;
        while (pending.size() < split + 4 + length) {
            char buf[8192];
            const ssize_t n = ::recv(fd, buf, sizeof buf, 0);
            if (n <= 0) { ::close(fd); return out; }
            pending.append(buf, static_cast<std::size_t>(n));
        }
        head.body = pending.substr(split + 4, length);
        pending.erase(0, split + 4 + length);
        out.push_back(std::move(head));
    }
    ::close(fd);
    return out;
}

// A logged-in (or anonymous) HTTP client: remembers the session cookie.
class Client {
public:
    explicit Client(std::uint16_t port) : port_(port) {}
    std::string cookie;   // "agri_session=<token>"
    Headers extra_headers;   // sent with every request, for example a hostile Origin

    Response get(const std::string& path) { return send("GET", path, "", {}); }
    Response post(const std::string& path, const json& body) { return send("POST", path, body.dump(), {{"Content-Type", "application/json"}}); }
    Response post_raw(const std::string& path, const std::string& body, const std::string& content_type) { return send("POST", path, body, {{"Content-Type", content_type}}); }
    Response patch(const std::string& path, const json& body) { return send("PATCH", path, body.dump(), {{"Content-Type", "application/json"}}); }
    Response post_empty(const std::string& path) { return send("POST", path, "", {}); }

    // POST /auth/login and keep the cookie from Set-Cookie.
    Response login(const std::string& username, const std::string& password) {
        cookie.clear();
        Response r = post("/auth/login", {{"username", username}, {"password", password}});
        for (const std::string& c : r.set_cookies)
            if (c.compare(0, 12, "agri_session") == 0) cookie = c.substr(0, c.find(';'));
        return r;
    }
    std::uint16_t port() const { return port_; }

private:
    Response send(const std::string& method, const std::string& path, const std::string& body, Headers headers) {
        if (!cookie.empty()) headers.emplace_back("Cookie", cookie);
        for (const auto& h : extra_headers) headers.push_back(h);
        return request(port_, method, path, body, headers);
    }
    std::uint16_t port_;
};

inline std::string multipart_body(const std::string& boundary, const std::string& field, const std::string& filename,
                                  const std::string& content_type, const std::string& bytes) {
    return "--" + boundary + "\r\nContent-Disposition: form-data; name=\"" + field + "\"; filename=\"" + filename + "\"\r\nContent-Type: " +
           content_type + "\r\n\r\n" + bytes + "\r\n--" + boundary + "--\r\n";
}

// ---- WebSocket ---------------------------------------------------------------------------------------------------------

class WsClient {
public:
    // Opens /live with the given cookie. connected() is false if the server refused the handshake.
    WsClient(std::uint16_t port, const std::string& cookie, const std::string& origin = "") {
        fd_ = connect_local(port);
        if (fd_ < 0) return;
        static const char* b64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::mt19937 rng{std::random_device{}()};
        unsigned char raw[16];
        for (auto& b : raw) b = static_cast<unsigned char>(rng());
        std::string key;
        for (int i = 0; i < 15; i += 3) {
            const unsigned v = (raw[i] << 16) | (raw[i + 1] << 8) | raw[i + 2];
            key += b64[(v >> 18) & 63]; key += b64[(v >> 12) & 63]; key += b64[(v >> 6) & 63]; key += b64[v & 63];
        }
        key += b64[raw[15] >> 2]; key += b64[(raw[15] & 3) << 4]; key += "==";
        std::string req = "GET /live HTTP/1.1\r\nHost: 127.0.0.1:" + std::to_string(port) +
                          "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: " + key + "\r\nSec-WebSocket-Version: 13\r\n";
        if (!cookie.empty()) req += "Cookie: " + cookie + "\r\n";
        if (!origin.empty()) req += "Origin: " + origin + "\r\n";
        send_all(fd_, req + "\r\n");
        std::string head;
        char c;
        while (head.find("\r\n\r\n") == std::string::npos) {
            if (::recv(fd_, &c, 1, 0) <= 0) break;
            head += c;
        }
        connected_ = head.compare(0, 12, "HTTP/1.1 101") == 0;
    }
    ~WsClient() { if (fd_ >= 0) ::close(fd_); }
    WsClient(const WsClient&) = delete;
    WsClient& operator=(const WsClient&) = delete;
    bool connected() const { return connected_; }
    bool closed() const { return closed_; }   // the server closed the socket (a close frame or end of stream was seen)

    // The next text message as JSON, or nothing if none arrives within the timeout (or the server closed).
    std::optional<json> next(int timeout_ms) {
        std::string payload;
        if (!read_frame(timeout_ms, payload)) return std::nullopt;
        return json::parse(payload, nullptr, false);
    }

    // Everything that arrives until the line has been quiet for `quiet_ms`.
    std::vector<json> collect(int quiet_ms, int max_ms = 5000) {
        std::vector<json> out;
        const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(max_ms);
        while (std::chrono::steady_clock::now() < end) {
            auto m = next(quiet_ms);
            if (!m) break;
            out.push_back(*m);
        }
        return out;
    }

private:
    bool read_exact(int timeout_ms, std::string& out, std::size_t n) {
        out.clear();
        while (out.size() < n) {
            pollfd p{fd_, POLLIN, 0};
            if (::poll(&p, 1, timeout_ms) <= 0) return false;
            char buf[4096];
            const ssize_t got = ::recv(fd_, buf, std::min(n - out.size(), sizeof buf), 0);
            if (got <= 0) { closed_ = true; return false; }
            out.append(buf, static_cast<std::size_t>(got));
        }
        return true;
    }
    bool read_frame(int timeout_ms, std::string& payload) {
        for (;;) {
            std::string h;
            if (!read_exact(timeout_ms, h, 2)) return false;
            const unsigned opcode = static_cast<unsigned char>(h[0]) & 0x0F;
            std::uint64_t len = static_cast<unsigned char>(h[1]) & 0x7F;
            std::string ext;
            if (len == 126) { if (!read_exact(timeout_ms, ext, 2)) return false; len = (static_cast<unsigned char>(ext[0]) << 8) | static_cast<unsigned char>(ext[1]); }
            else if (len == 127) { if (!read_exact(timeout_ms, ext, 8)) return false; len = 0; for (char ch : ext) len = (len << 8) | static_cast<unsigned char>(ch); }
            if (!read_exact(timeout_ms, payload, static_cast<std::size_t>(len))) return false;
            if (opcode == 0x8) { closed_ = true; return false; }          // close
            if (opcode == 0x1) return true;           // text
            // ping/pong/binary: ignore and read on
        }
    }
    int fd_ = -1;
    bool connected_ = false;
    bool closed_ = false;
};

}  // namespace testhttp
