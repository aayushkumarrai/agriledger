// Internal to E4: request validation, error mapping, login throttling, password hashing, small HTTP helpers.
#pragma once
#include <crow.h>

#include <chrono>
#include <deque>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "e2/engine.hpp"

namespace e4 {

using json = nlohmann::json;
using i64 = std::int64_t;

// A request the server refuses before it reaches E2. `code` is a reason code, never shown to the user as such:
// the browser turns codes into plain sentences (CLAUDE.md rule 4).
struct HttpError : std::runtime_error {
    int status;
    std::string code;
    HttpError(int s, std::string c) : std::runtime_error(c), status(s), code(std::move(c)) {}
};

// ---- strict JSON field readers: integers only, never floating point (CLAUDE.md rule 1) ----------------------------

inline json body_object(const crow::request& req) {
    json parsed;
    if (req.body.size() > 64 * 1024) throw HttpError(413, "BODY_TOO_LARGE");
    const std::string& ct = req.get_header_value("Content-Type");
    if (ct.compare(0, 16, "application/json") != 0) throw HttpError(415, "UNSUPPORTED_MEDIA_TYPE");   // blocks cross-site form posts
    parsed = json::parse(req.body, nullptr, /*allow_exceptions=*/false);
    if (!parsed.is_object()) throw HttpError(400, "BAD_JSON");
    return parsed;
}

inline i64 need_int(const json& j, const char* key, i64 lo, i64 hi) {
    const auto it = j.find(key);
    if (it == j.end()) throw HttpError(400, "MISSING_FIELD");
    if (!(it->is_number_integer() || it->is_number_unsigned())) throw HttpError(400, "INVALID_FIELD");   // 2500.5 is rejected
    if (it->is_number_unsigned() && it->get<std::uint64_t>() > static_cast<std::uint64_t>(hi)) throw HttpError(400, "INVALID_FIELD");
    const i64 v = it->get<i64>();
    if (v < lo || v > hi) throw HttpError(400, "INVALID_FIELD");
    return v;
}

inline std::optional<i64> opt_int(const json& j, const char* key, i64 lo, i64 hi) {
    if (!j.contains(key) || j.at(key).is_null()) return std::nullopt;
    return need_int(j, key, lo, hi);
}

inline std::string need_str(const json& j, const char* key, std::size_t min_len, std::size_t max_len) {
    const auto it = j.find(key);
    if (it == j.end()) throw HttpError(400, "MISSING_FIELD");
    if (!it->is_string()) throw HttpError(400, "INVALID_FIELD");
    std::string s = it->get<std::string>();
    if (s.size() < min_len || s.size() > max_len) throw HttpError(400, "INVALID_FIELD");
    return s;
}

inline std::optional<std::string> opt_str(const json& j, const char* key, std::size_t max_len) {
    if (!j.contains(key) || j.at(key).is_null()) return std::nullopt;
    return need_str(j, key, 0, max_len);
}

// ---- responses -------------------------------------------------------------------------------------------------------

inline crow::response json_response(int status, const json& body) {
    crow::response r(status, body.dump());
    r.set_header("Content-Type", "application/json");
    r.set_header("Cache-Control", "no-store");
    r.set_header("X-Content-Type-Options", "nosniff");
    r.set_header("X-Frame-Options", "DENY");
    return r;
}

inline crow::response error_response(int status, const std::string& code, const json& extra = json::object()) {
    json j = extra;
    j["error"] = code;
    return json_response(status, j);
}

// ---- login throttling ------------------------------------------------------------------------------------------------

class LoginLimiter {
public:
    LoginLimiter(int max_failures, int window_seconds) : max_(max_failures), window_(window_seconds) {}
    bool blocked(const std::string& key) {
        std::lock_guard<std::mutex> lock(m_);
        const auto it = failures_.find(key);   // a lookup must not add an entry: random usernames would grow the map
        if (it == failures_.end()) return false;
        prune(it->second);
        return static_cast<int>(it->second.size()) >= max_;
    }
    void failure(const std::string& key) {
        std::lock_guard<std::mutex> lock(m_);
        if (failures_.size() > 20000) {   // random usernames must not grow memory without bound
            for (auto it = failures_.begin(); it != failures_.end();) { prune(it->second); it = it->second.empty() ? failures_.erase(it) : std::next(it); }
        }
        auto& q = failures_[key];
        prune(q);
        q.push_back(Clock::now());
    }
    void success(const std::string& key) {
        std::lock_guard<std::mutex> lock(m_);
        failures_.erase(key);
    }
    int window_seconds() const { return window_; }
private:
    using Clock = std::chrono::steady_clock;
    void prune(std::deque<Clock::time_point>& q) {
        const auto cutoff = Clock::now() - std::chrono::seconds(window_);
        while (!q.empty() && q.front() < cutoff) q.pop_front();
    }
    int max_, window_;
    std::mutex m_;
    std::unordered_map<std::string, std::deque<Clock::time_point>> failures_;
};

// A browser always sends Origin on cross-site requests. If it is present it must name this server (Host).
inline bool same_origin(const crow::request& req) {
    const std::string& origin = req.get_header_value("Origin");
    if (origin.empty()) return true;
    const auto scheme = origin.find("://");
    const std::string host = scheme == std::string::npos ? origin : origin.substr(scheme + 3);
    const std::string& want = req.get_header_value("Host");
    if (host.size() != want.size()) return false;
    for (std::size_t i = 0; i < host.size(); ++i) if (std::tolower(static_cast<unsigned char>(host[i])) != std::tolower(static_cast<unsigned char>(want[i]))) return false;
    return true;
}

// ---- passwords (argon2id through libsodium) --------------------------------------------------------------------------

class PasswordHasher {
public:
    PasswordHasher(unsigned long long opslimit, std::size_t memlimit);
    std::string hash(const std::string& password) const;                     // argon2id string, includes salt and parameters
    bool verify(const std::string& hash, const std::string& password) const;
    // Verifies against a throw-away hash so an unknown username takes as long as a wrong password.
    void burn_time(const std::string& password) const;
private:
    unsigned long long ops_;
    std::size_t mem_;
    std::string dummy_;
};

// 32 random bytes from the operating system's CSPRNG (libsodium), as 64 lowercase hex characters.
std::string new_session_token();

}  // namespace e4
