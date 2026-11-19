// Internal to E2: connection pool, transaction runner and the helpers every operation shares.
#pragma once
#include <pqxx/pqxx>

#include <condition_variable>
#include <memory>
#include <mutex>
#include <type_traits>
#include <vector>

#include "e1/hashing.hpp"
#include "e1/invariants.hpp"
#include "e2/engine.hpp"

namespace e2::detail {

// Fixed-size pool of libpqxx connections, created lazily. A connection that broke is dropped, not reused.
class ConnectionPool {
public:
    ConnectionPool(std::string conninfo, std::size_t max_connections);

    class Lease {
    public:
        Lease(ConnectionPool& pool, std::unique_ptr<pqxx::connection> conn) : pool_(&pool), conn_(std::move(conn)) {}
        Lease(Lease&& o) noexcept : pool_(o.pool_), conn_(std::move(o.conn_)) { o.pool_ = nullptr; }
        Lease(const Lease&) = delete;
        Lease& operator=(const Lease&) = delete;
        ~Lease() { if (pool_) pool_->release(std::move(conn_)); }
        pqxx::connection& get() { return *conn_; }
    private:
        ConnectionPool* pool_;
        std::unique_ptr<pqxx::connection> conn_;
    };

    Lease acquire();

private:
    void release(std::unique_ptr<pqxx::connection> conn);
    std::string conninfo_;
    std::size_t max_;
    std::size_t total_ = 0;
    std::vector<std::unique_ptr<pqxx::connection>> idle_;
    std::mutex mutex_;
    std::condition_variable cv_;
};

}  // namespace e2::detail

struct e2::Engine::Impl {
    Config cfg;
    detail::ConnectionPool pool;
    explicit Impl(Config c) : cfg(std::move(c)), pool(cfg.conninfo, cfg.pool_size) {}
};

namespace e2::detail {

// SQL expression: a timestamptz column as UTC ISO-8601 with 6 fractional digits (spec §5.3).
std::string utc_micros(const std::string& column);

// Serialization failure, deadlock or lock timeout: nothing was changed and a retry may succeed.
bool is_transient(const pqxx::sql_error& e);

void set_lock_timeout(pqxx::transaction_base& tx, int ms);

struct PoolRow {
    std::string status;
    i64 total_stock_g = 0;
    i64 price_paise_per_kg = 0;
    i64 min_order_g = 0;
    std::string storage_location;
};
// SELECT ... FOR UPDATE: the pool row lock every operation takes first. DomainError NOT_FOUND if absent.
PoolRow lock_pool(pqxx::transaction_base& tx, i64 pool_id);

struct EventRef { i64 event_id; std::string created_at_utc; };
EventRef insert_event(pqxx::transaction_base& tx, i64 pool_id, const std::string& type, i64 actor_user_id);

// Reads the chain tail (safe: the pool row is locked), computes the E1 entry hash and inserts the ledger_entry row.
void seal_event(pqxx::transaction_base& tx, i64 pool_id, const EventRef& ev, const std::string& type,
                const e1::EventRows& rows);

// NOTIFY ledger_events, '<event_id>:<pool_id>' (delivered only if the transaction commits).
void notify_event(pqxx::transaction_base& tx, i64 event_id, i64 pool_id);

// Facts for E1's invariant check. `only_sale_event`: check INV-4 for that sale only (in-transaction), else all sales.
e1::PoolFacts load_pool_facts(pqxx::transaction_base& tx, i64 pool_id, std::optional<i64> only_sale_event);

// INV-1..5 inside the transaction; throws ReconciliationFailed (the caller's transaction then rolls back).
void reconcile(pqxx::transaction_base& tx, i64 pool_id, std::optional<i64> sale_event);

// Runs `body` in one transaction with the lock timeout set, commits, and turns transient SQL errors into
// TransientError. `body` returns a value or void.
template <class Body>
auto transact(Engine::Impl& impl, Body&& body) {
    using R = std::invoke_result_t<Body&, pqxx::work&>;
    auto lease = impl.pool.acquire();
    try {
        pqxx::work tx(lease.get());
        set_lock_timeout(tx, impl.cfg.lock_timeout_ms);
        if constexpr (std::is_void_v<R>) {
            body(tx);
            tx.commit();
        } else {
            R result = body(tx);
            tx.commit();
            return result;
        }
    } catch (const pqxx::sql_error& e) {
        if (is_transient(e)) throw TransientError(e.what());
        throw;
    }
}

}  // namespace e2::detail
