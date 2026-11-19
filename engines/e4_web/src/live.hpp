// Live feed (spec §7): one LISTEN connection, role-filtered JSON messages to WebSocket clients.
#pragma once
#include <crow.h>

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

#include "e2/engine.hpp"

namespace e4 {

// Stored in the WebSocket's userdata between accept and close.
struct ConnInfo {
    e2::Viewer viewer;
    std::string token;   // the session this socket belongs to: when it ends, so does the socket
};

// The messages one viewer should receive for one committed event (`ev` is E2's event_view). Pure function.
std::vector<nlohmann::json> messages_for(const e2::Viewer& viewer, const nlohmann::json& ev, bool reconciliation_ok);

class LiveHub {
public:
    LiveHub(e2::Engine& engine, std::string conninfo) : engine_(engine), conninfo_(std::move(conninfo)) {}
    ~LiveHub() { stop(); }

    void start();   // returns when LISTEN is active (or after a few seconds if the database is not reachable)
    void stop();
    void add(crow::websocket::connection& c, const ConnInfo& info);
    void remove(crow::websocket::connection& c);
    void close_token(const std::string& token);   // logout: end that session's sockets at once
    void sweep();                                 // close sockets whose session expired or was revoked

private:
    void run();
    void on_payload(const std::string& payload);
    void broadcast_resync();

    e2::Engine& engine_;
    std::string conninfo_;
    std::mutex m_;                                                      // guards conns_; held while sending
    std::unordered_map<crow::websocket::connection*, ConnInfo> conns_;
    std::thread thread_;
    std::atomic<bool> stop_{false};
    std::mutex ready_m_;
    std::condition_variable ready_cv_;
    bool ready_ = false;
};

}  // namespace e4
