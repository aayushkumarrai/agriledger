// E4 Request Orchestration Engine: REST + WebSocket server (Crow), sessions, role checks on every request, input
// validation, photo upload, and the live feed. E4 never touches SQL: it calls E2 (reads and non-sale writes) and sends
// every sale through E3 into E2.
#pragma once
#include <cstdint>
#include <memory>
#include <string>

#include "e2/engine.hpp"
#include "e3/pool_queue.hpp"

namespace e4 {

struct Config {
    std::string db_conninfo;                      // libpq string of the application LOGIN user
    std::string bind_address = "127.0.0.1";
    std::uint16_t port = 8080;                    // 0 = pick a free port (tests); read it back with Server::port()
    std::string photo_dir = "data/photos";        // uploaded lot photos (git-ignored)
    std::string web_dir = "web";                  // the static frontend (HTML, CSS, JS, fonts), served without a login
    int http_threads = 8;
    std::size_t db_pool_size = 8;
    int lock_timeout_ms = 3000;
    e3::Options queue;                            // per-pool FIFO workers, retry, queue timeout

    int session_ttl_seconds = 8 * 3600;           // spec §6: 8 hours
    bool cookie_secure = false;                   // set true when served over HTTPS
    int login_max_failures = 5;                   // per username + address, then 429 until the window passes
    int signup_max_attempts_per_hour = 10;        // sign-up attempts from one address; every attempt counts, then 429
    int login_max_failures_per_ip = 50;           // across all usernames from one address (credential stuffing)
    int login_window_seconds = 300;
    std::size_t max_photo_bytes = 5 * 1024 * 1024;
    unsigned long long pwhash_opslimit = 0;       // 0 = libsodium "interactive" (argon2id); tests lower it for speed
    std::size_t pwhash_memlimit = 0;              // bytes; 0 = libsodium "interactive"
};

class Server {
public:
    explicit Server(Config config);
    ~Server();
    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    void start();                 // returns once the server is listening and the live feed is running
    void stop();
    std::uint16_t port() const;   // the bound port
    e2::Engine& engine();         // for tests

    // Creates an account with a temporary password (hashed here with argon2id). Used by the create-manager command, the demo
    // seed and tests; over the API only a Manager can create Farmer and Buyer accounts. Returns the user id.
    std::int64_t create_account(const std::string& name, const std::string& username, const std::string& type,
                                const std::string& temporary_password);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace e4
