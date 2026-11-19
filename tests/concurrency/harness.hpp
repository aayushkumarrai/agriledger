// Concurrency test harness (master §11, O6). TEST CODE ONLY: nothing here is linked into the server.
//
//  * run_protected    - the production path: buyers -> E3 per-pool FIFO queue -> E2 transaction with the pool row lock.
//  * run_unprotected  - the deliberate comparison: its own scratch table in a separate schema (scratch.*), a plain stock
//                       counter, NO CHECK constraints, NO locking, read-then-write. It exists to show overselling.
#pragma once
#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <vector>

#include "../integration/db_fixture.hpp"
#include "e3/e2_adapter.hpp"

namespace harness {

using testdb::i64;

struct RunResult {
    int accepted = 0, rejected = 0, busy = 0, errors = 0;
    i64 accepted_g = 0, start_stock_g = 0, final_stock_g = 0;
    bool reconciled = true, chain_ok = true;
    int total() const { return accepted + rejected + busy + errors; }
    bool oversold() const { return accepted_g > start_stock_g || final_stock_g < 0; }
};

// ---- protected: real E3 -> real E2 -------------------------------------------------------------------------------

inline RunResult run_protected(e2::Engine& eng, testdb::World& world, int buyers, i64 per_buyer_g, i64 start_stock_g,
                               e3::Options options = {}) {
    const i64 pool = world.make_pool(2500, per_buyer_g);
    eng.record_contribution(pool, world.farmer[0], start_stock_g, world.manager);
    e3::SaleQueue queue(e3::sale_callbacks(eng), options);

    RunResult res;
    res.start_stock_g = start_stock_g;
    std::mutex m;
    std::atomic<bool> go{false};
    std::vector<std::thread> threads;
    for (int i = 0; i < buyers; ++i)
        threads.emplace_back([&, i] {
            e2::SaleRequest req = world.order(pool, per_buyer_g);
            req.buyer_id = req.actor_user_id = (i % 2 == 0) ? world.buyer : world.buyer2;
            while (!go) std::this_thread::yield();   // all buyers arrive together
            try {
                const e2::SaleOutcome o = queue.submit(req).get();
                std::lock_guard<std::mutex> lock(m);
                switch (o.status) {
                    case e2::SaleOutcome::Status::Accepted: ++res.accepted; res.accepted_g += o.receipt.quantity_g; break;
                    case e2::SaleOutcome::Status::Rejected: ++res.rejected; break;
                    default: ++res.busy; break;
                }
            } catch (...) {
                std::lock_guard<std::mutex> lock(m);
                ++res.errors;
            }
        });
    go = true;
    for (auto& t : threads) t.join();

    res.final_stock_g = testdb::snapshot(pool).at("stock");
    res.reconciled = eng.reconciliation(pool).ok;
    res.chain_ok = eng.integrity_check(pool).ok;
    return res;
}

// ---- unprotected: scratch schema, no CHECKs, no locks -----------------------------------------------------------------

inline void create_scratch() {
    pqxx::connection admin(testdb::super_conninfo());
    pqxx::nontransaction n(admin);
    n.exec("CREATE SCHEMA IF NOT EXISTS scratch");
    n.exec("CREATE TABLE IF NOT EXISTS scratch.stock_counter (id BIGSERIAL PRIMARY KEY, stock_g BIGINT NOT NULL)");
    n.exec("CREATE TABLE IF NOT EXISTS scratch.sold (id BIGSERIAL PRIMARY KEY, counter_id BIGINT NOT NULL, qty_g BIGINT NOT NULL)");
}

inline void drop_scratch() {
    pqxx::connection admin(testdb::super_conninfo());
    pqxx::nontransaction n(admin);
    n.exec("DROP SCHEMA IF EXISTS scratch CASCADE");
}

inline RunResult run_unprotected(int buyers, i64 per_buyer_g, i64 start_stock_g) {
    create_scratch();
    pqxx::connection admin(testdb::super_conninfo());
    i64 counter_id = 0;
    {
        pqxx::work tx(admin);
        counter_id = tx.exec_params1("INSERT INTO scratch.stock_counter(stock_g) VALUES ($1) RETURNING id", start_stock_g)[0].as<i64>();
        tx.commit();
    }
    RunResult res;
    res.start_stock_g = start_stock_g;
    std::mutex m;
    std::atomic<bool> go{false};
    std::vector<std::thread> threads;
    for (int i = 0; i < buyers; ++i)
        threads.emplace_back([&] {
            try {
                pqxx::connection c(testdb::super_conninfo());
                while (!go) std::this_thread::yield();
                pqxx::work tx(c);
                // read ... (no lock)
                const i64 stock = tx.exec_params1("SELECT stock_g FROM scratch.stock_counter WHERE id = $1", counter_id)[0].as<i64>();
                std::this_thread::sleep_for(std::chrono::milliseconds(3));   // time for other buyers to read the same stock
                // ... then write the value computed from the stale read
                if (per_buyer_g <= stock) {
                    tx.exec_params0("UPDATE scratch.stock_counter SET stock_g = $2 WHERE id = $1", counter_id, stock - per_buyer_g);
                    tx.exec_params0("INSERT INTO scratch.sold(counter_id, qty_g) VALUES ($1, $2)", counter_id, per_buyer_g);
                    tx.commit();
                    std::lock_guard<std::mutex> lock(m);
                    ++res.accepted;
                } else {
                    tx.abort();
                    std::lock_guard<std::mutex> lock(m);
                    ++res.rejected;
                }
            } catch (...) {
                std::lock_guard<std::mutex> lock(m);
                ++res.errors;
            }
        });
    go = true;
    for (auto& t : threads) t.join();

    pqxx::nontransaction n(admin);
    res.accepted_g = n.exec_params1("SELECT coalesce(sum(qty_g), 0)::bigint FROM scratch.sold WHERE counter_id = $1", counter_id)[0].as<i64>();
    res.final_stock_g = n.exec_params1("SELECT stock_g FROM scratch.stock_counter WHERE id = $1", counter_id)[0].as<i64>();
    return res;
}

}  // namespace harness
