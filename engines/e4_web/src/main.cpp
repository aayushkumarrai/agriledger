// agriledger_server
//   agriledger_server                      run the server
//   agriledger_server create-manager USERNAME "FULL NAME"
//        creates a Pool Manager account (there is no self-registration); the temporary password is read from the
//        first line of standard input. The Manager must choose a new password at first login.
// Settings come from the environment: AGRILEDGER_DB (libpq connection string), AGRILEDGER_PORT, AGRILEDGER_BIND,
// AGRILEDGER_PHOTO_DIR, AGRILEDGER_COOKIE_SECURE=1 when served over HTTPS.
#include <atomic>
#include <chrono>
#include <csignal>
#include <fstream>
#include <cstdlib>
#include <iostream>
#include <iterator>
#include <string>
#include <thread>

#include "e4/server.hpp"

namespace {
std::atomic<bool> g_stop{false};
void on_signal(int) { g_stop = true; }

std::string env(const char* name, const std::string& fallback = "") {
    const char* v = std::getenv(name);
    return v && *v ? v : fallback;
}
}  // namespace

int main(int argc, char** argv) {
    e4::Config cfg;
    cfg.db_conninfo = env("AGRILEDGER_DB");
    if (cfg.db_conninfo.empty()) {
        std::cerr << "Set AGRILEDGER_DB to the libpq connection string of the application login (see README).\n";
        return 2;
    }
    cfg.bind_address = env("AGRILEDGER_BIND", "127.0.0.1");
    cfg.photo_dir = env("AGRILEDGER_PHOTO_DIR", "data/photos");
    cfg.web_dir = env("AGRILEDGER_WEB_DIR", "web");
    cfg.cookie_secure = env("AGRILEDGER_COOKIE_SECURE") == "1";
    // Offline mode: AGRILEDGER_MARKET_FEED_FILE names a saved price file (the same JSON the service sends). The Update market prices
    // button then reads that file instead of calling the internet. Used by the browser tests and for demos without a connection.
    if (const std::string file = env("AGRILEDGER_MARKET_FEED_FILE"); !file.empty())
        cfg.market_feed = [file](const std::string&, int offset, int) -> std::string {
            if (offset > 0) return R"({"total":0,"count":0,"records":[]})";
            std::ifstream in(file);
            if (!in) throw std::runtime_error("the saved price file could not be read");
            return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        };
    cfg.market_auto_update_hours = std::stoi(env("AGRILEDGER_MARKET_AUTO_HOURS", "6"));   // prices refresh by themselves; 0 turns that off
    cfg.market_api_key = env("DATA_GOV_API_KEY");   // optional: without it the Update market prices button says it is not set up

    try {
        cfg.port = static_cast<std::uint16_t>(std::stoi(env("AGRILEDGER_PORT", "8080")));   // inside the try: a bad value is an error message, not a crash
        e4::Server server(cfg);
        if (argc >= 2 && std::string(argv[1]) == "create-manager") {
            if (argc != 4) { std::cerr << "usage: agriledger_server create-manager USERNAME \"FULL NAME\"  (password on stdin)\n"; return 2; }
            std::string password;
            std::getline(std::cin, password);
            if (password.size() < 8) { std::cerr << "the temporary password must be at least 8 characters\n"; return 2; }
            const std::int64_t id = server.create_account(argv[3], argv[2], "MANAGER", password);
            std::cout << "created Pool Manager " << argv[2] << " (user " << id << "); they must change the password at first login\n";
            return 0;
        }
        server.start();
        std::cout << "AgriLedger listening on " << cfg.bind_address << ":" << server.port() << "\n" << std::flush;
        std::signal(SIGINT, on_signal);
        std::signal(SIGTERM, on_signal);
        while (!g_stop) std::this_thread::sleep_for(std::chrono::milliseconds(200));
        server.stop();
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
