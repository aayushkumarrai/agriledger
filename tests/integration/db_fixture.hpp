// Shared helpers for tests that run against the real PostgreSQL container (scripts/test.sh prepares the database
// and sets AGRI_TEST_CONNINFO for the application login and AGRI_TEST_SUPER_CONNINFO for the superuser).
#pragma once
#include <gtest/gtest.h>
#include <pqxx/pqxx>

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <random>
#include <string>

#include "e2/engine.hpp"

namespace testdb {

using i64 = std::int64_t;

inline std::string env_or_empty(const char* name) {
    const char* v = std::getenv(name);
    return v ? v : "";
}

inline std::string app_conninfo() { return env_or_empty("AGRI_TEST_CONNINFO"); }
inline std::string super_conninfo() { return env_or_empty("AGRI_TEST_SUPER_CONNINFO"); }

// Version-4 UUID text from a random 128-bit value (integers only).
inline std::string new_uuid() {
    static std::mt19937_64 rng{std::random_device{}()};
    static std::mutex m;
    std::uint64_t a, b;
    { std::lock_guard<std::mutex> lock(m); a = rng(); b = rng(); }
    a = (a & 0xFFFFFFFFFFFF0FFFULL) | 0x0000000000004000ULL;
    b = (b & 0x3FFFFFFFFFFFFFFFULL) | 0x8000000000000000ULL;
    char buf[40];
    std::snprintf(buf, sizeof buf, "%08x-%04x-%04x-%04x-%012llx", static_cast<unsigned>(a >> 32),
                  static_cast<unsigned>((a >> 16) & 0xFFFF), static_cast<unsigned>(a & 0xFFFF),
                  static_cast<unsigned>(b >> 48), static_cast<unsigned long long>(b & 0xFFFFFFFFFFFFULL));
    return buf;
}

inline std::string unique_name(const std::string& prefix) {
    static std::atomic<unsigned long long> n{0};
    static const unsigned long long salt = std::random_device{}();
    return prefix + "-" + std::to_string(salt) + "-" + std::to_string(++n);
}

// One pool with a manager, three farmers (A, B, C) and two buyers, all made through E2.
struct World {
    e2::Engine& eng;
    i64 manager = 0, farmer[3] = {0, 0, 0}, buyer = 0, buyer2 = 0, commodity = 0;

    explicit World(e2::Engine& e) : eng(e) {
        if (super_conninfo().empty() || app_conninfo().empty())
            throw std::runtime_error("AGRI_TEST_CONNINFO / AGRI_TEST_SUPER_CONNINFO are not set; run tests with scripts/test.sh");
        pqxx::connection admin(super_conninfo());
        pqxx::nontransaction n(admin);
        commodity = n.exec1("SELECT commodity_id FROM commodity WHERE name = 'Paddy'")[0].as<i64>();
        manager = eng.create_user({"Manager", unique_name("mgr"), "MANAGER", "hash"});
        for (int i = 0; i < 3; ++i) farmer[i] = eng.create_user({std::string("Farmer ") + char('A' + i), unique_name("farmer"), "FARMER", "hash"});
        buyer = eng.create_user({"Buyer 1", unique_name("buyer"), "BUYER", "hash"});
        buyer2 = eng.create_user({"Buyer 2", unique_name("buyer"), "BUYER", "hash"});
    }

    // Draft pool with a photo; opened if `open`. Price in paise per kg, minimum order in grams.
    i64 make_pool(i64 price = 2500, i64 min_order_g = 100000, bool open = true) {
        e2::NewPool np;
        np.commodity_id = commodity;
        np.variety = "Sona Masuri";
        np.range_no = 1;
        np.packaging = "50 kg jute bags";
        np.storage_location = "Test godown, Hubballi";
        np.min_order_g = min_order_g;
        np.price_paise_per_kg = price;
        const i64 pool = eng.create_pool(np, manager);
        eng.add_photo(pool, manager, "photos/" + unique_name("p") + ".jpg", std::string(64, 'a'));
        if (open) eng.change_status(pool, "open", manager);
        return pool;
    }

    // The master §12 worked example: 600 / 800 / 600 kg.
    i64 make_worked_example_pool() {
        const i64 pool = make_pool();
        eng.record_contribution(pool, farmer[0], 600000, manager);
        eng.record_contribution(pool, farmer[1], 800000, manager);
        eng.record_contribution(pool, farmer[2], 600000, manager);
        return pool;
    }

    e2::SaleRequest order(i64 pool, i64 quantity_g, i64 price = 2500, const std::string& request_id = new_uuid()) const {
        e2::SaleRequest r;
        r.request_id = request_id;
        r.pool_id = pool;
        r.quantity_g = quantity_g;
        r.confirmed_price_paise_per_kg = price;
        r.buyer_id = buyer;
        r.actor_user_id = buyer;
        return r;
    }
};

// Row counts and stock for one pool, read as the superuser, to prove a rolled-back operation left nothing behind.
inline std::map<std::string, i64> snapshot(i64 pool) {
    pqxx::connection admin(super_conninfo());
    pqxx::nontransaction n(admin);
    std::map<std::string, i64> s;
    for (const char* t : {"event", "contribution", "sale", "inventory_ledger", "ownership_ledger", "financial_ledger",
                          "ledger_entry", "pool_change", "pool_photo", "pool_seller_balance"})
        s[t] = n.exec_params1(std::string("SELECT count(*) FROM ") + t + " WHERE pool_id = $1", pool)[0].as<i64>();
    s["payout"] = n.exec_params1("SELECT count(*) FROM payout p JOIN sale s USING (sale_id) WHERE s.pool_id = $1", pool)[0].as<i64>();
    s["stock"] = n.exec_params1("SELECT total_stock_g FROM pool WHERE pool_id = $1", pool)[0].as<i64>();
    s["balances"] = n.exec_params1("SELECT coalesce(sum(remaining_g), 0)::bigint FROM pool_seller_balance WHERE pool_id = $1", pool)[0].as<i64>();
    return s;
}

// Runs SQL as the superuser with ordinary triggers switched off, to simulate someone tampering with stored rows.
template <class... Args>
inline void tamper(const std::string& sql, Args... args) {
    pqxx::connection admin(super_conninfo());
    pqxx::work tx(admin);
    tx.exec0("SET LOCAL session_replication_role = replica");
    tx.exec_params0(sql, args...);
    tx.commit();
}

class DbTest : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_FALSE(app_conninfo().empty()) << "AGRI_TEST_CONNINFO is not set; run tests with scripts/test.sh";
        ASSERT_FALSE(super_conninfo().empty()) << "AGRI_TEST_SUPER_CONNINFO is not set; run tests with scripts/test.sh";
    }
    e2::Config config(std::size_t pool_size = 8, int lock_timeout_ms = 3000) const {
        e2::Config c;
        c.conninfo = app_conninfo();
        c.pool_size = pool_size;
        c.lock_timeout_ms = lock_timeout_ms;
        return c;
    }
};

}  // namespace testdb
