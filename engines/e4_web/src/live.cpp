#include "live.hpp"

#include <pqxx/pqxx>

#include <chrono>
#include <cstdlib>
#include <iostream>

namespace e4 {
using json = nlohmann::json;
using i64 = std::int64_t;

namespace {

class Receiver : public pqxx::notification_receiver {
public:
    Receiver(pqxx::connection& c, std::function<void(const std::string&)> on)
        : pqxx::notification_receiver(c, "ledger_events"), on_(std::move(on)) {}
    void operator()(const std::string& payload, int) override { on_(payload); }
private:
    std::function<void(const std::string&)> on_;
};

i64 to_i64(const json& v) {
    try { return std::stoll(v.get<std::string>()); } catch (...) { return 0; }
}

}  // namespace

std::vector<json> messages_for(const e2::Viewer& viewer, const json& ev, bool reconciliation_ok) {
    std::vector<json> out;
    const bool manager = viewer.role == e2::Viewer::Role::Manager;
    if (!manager && ev["status"] == "draft") return out;   // draft pools are the Manager's business only
    const i64 pool_id = ev["pool_id"].get<i64>();
    const std::string type = ev["event_type"].get<std::string>();

    if (type == "POOL_CREATED" || type == "CONTRIBUTION" || type == "SALE")
        out.push_back({{"type", "stock_changed"}, {"pool_id", pool_id}, {"stock_g", ev["stock_g"]}, {"at", ev["at"]}});
    if (type == "PRICE_CHANGED" && ev["change"].is_object())
        out.push_back({{"type", "price_changed"}, {"pool_id", pool_id},
                       {"old_price_paise_per_kg", to_i64(ev["change"]["old_value"])}, {"new_price_paise_per_kg", to_i64(ev["change"]["new_value"])}});
    if (type == "STATUS_CHANGED")
        out.push_back({{"type", "status_changed"}, {"pool_id", pool_id}, {"status", ev["status"]}});

    if (manager) {
        json inv = ev["inventory"];
        if (ev["change"].is_object()) inv.push_back(ev["change"]);   // price/status change rows, marked kind "change"
        if (!inv.empty()) out.push_back({{"type", "ledger_rows"}, {"pool_id", pool_id}, {"ledger", "inventory"}, {"rows", inv}});
        if (!ev["ownership"].empty()) out.push_back({{"type", "ledger_rows"}, {"pool_id", pool_id}, {"ledger", "ownership"}, {"rows", ev["ownership"]}});
        if (!ev["financial"].empty()) out.push_back({{"type", "ledger_rows"}, {"pool_id", pool_id}, {"ledger", "financial"}, {"rows", ev["financial"]}});
    } else if (viewer.role == e2::Viewer::Role::Farmer) {
        for (const char* ledger : {"ownership", "financial"}) {
            json mine = json::array();
            for (const json& row : ev[ledger])
                if (row["seller_id"].is_number() && row["seller_id"].get<i64>() == viewer.user_id) mine.push_back(row);   // only their own rows
            if (!mine.empty()) out.push_back({{"type", "ledger_rows"}, {"pool_id", pool_id}, {"ledger", ledger}, {"rows", mine}});
        }
    }

    out.push_back({{"type", "reconciliation"}, {"pool_id", pool_id}, {"ok", reconciliation_ok}});

    if (viewer.role == e2::Viewer::Role::Buyer && ev["sale"].is_object() && ev["sale"]["buyer_id"].get<i64>() == viewer.user_id) {
        json sale = ev["sale"];
        sale.erase("buyer_id");
        out.push_back({{"type", "my_purchase"}, {"pool_id", pool_id}, {"sale", sale}});   // only to the buyer who bought
    }
    return out;
}

void LiveHub::add(crow::websocket::connection& c, const ConnInfo& info) {
    std::lock_guard<std::mutex> lock(m_);
    conns_[&c] = info;
}

void LiveHub::close_token(const std::string& token) {
    std::lock_guard<std::mutex> lock(m_);
    for (auto& kv : conns_) if (kv.second.token == token) kv.first->close("session ended");
}

void LiveHub::sweep() {
    std::vector<std::string> tokens;
    {
        std::lock_guard<std::mutex> lock(m_);
        for (auto& kv : conns_) tokens.push_back(kv.second.token);
    }
    std::vector<std::string> dead;
    for (const std::string& t : tokens) {
        try { if (!engine_.find_session(t)) dead.push_back(t); } catch (...) {}
    }
    for (const std::string& t : dead) close_token(t);
}

void LiveHub::remove(crow::websocket::connection& c) {
    std::lock_guard<std::mutex> lock(m_);
    conns_.erase(&c);
}

void LiveHub::on_payload(const std::string& payload) {
    const auto colon = payload.find(':');
    if (colon == std::string::npos) return;
    i64 event_id = 0, pool_id = 0;
    try {
        event_id = std::stoll(payload.substr(0, colon));
        pool_id = std::stoll(payload.substr(colon + 1));
    } catch (...) { return; }
    try {
        const json ev = engine_.event_view(event_id);
        if (ev.is_null()) return;
        bool ok = false;
        try { ok = engine_.reconciliation(pool_id).ok; } catch (...) { ok = false; }
        std::lock_guard<std::mutex> lock(m_);
        for (auto& kv : conns_)
            for (const json& msg : messages_for(kv.second.viewer, ev, ok)) kv.first->send_text(msg.dump());
    } catch (const std::exception& e) {
        std::cerr << "live feed: could not publish event " << event_id << ": " << e.what() << "\n";
    }
}

void LiveHub::broadcast_resync() {
    std::lock_guard<std::mutex> lock(m_);
    for (auto& kv : conns_) kv.first->send_text(json({{"type", "resync"}}).dump());   // events may have been missed: re-fetch
}

void LiveHub::run() {
    bool first = true;
    while (!stop_) {
        try {
            pqxx::connection c(conninfo_);
            Receiver receiver(c, [this](const std::string& p) { on_payload(p); });   // LISTEN is issued here
            if (first) first = false; else broadcast_resync();
            {
                std::lock_guard<std::mutex> lock(ready_m_);
                ready_ = true;
            }
            ready_cv_.notify_all();
            int ticks = 0;
            while (!stop_) {
                c.await_notification(1, 0);
                if (++ticks % 15 == 0) sweep();   // every ~15 s: sockets of expired or revoked sessions are closed
            }
        } catch (const std::exception& e) {
            if (stop_) break;
            std::cerr << "live feed: listener connection lost (" << e.what() << "), retrying\n";
            first = false;
            for (int i = 0; i < 10 && !stop_; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

void LiveHub::start() {
    thread_ = std::thread([this] { run(); });
    std::unique_lock<std::mutex> lock(ready_m_);
    ready_cv_.wait_for(lock, std::chrono::seconds(5), [this] { return ready_; });
}

void LiveHub::stop() {
    if (stop_.exchange(true)) return;
    if (thread_.joinable()) thread_.join();
}

}  // namespace e4
