#include <algorithm>
#include <map>

#include "internal.hpp"

namespace e2 {

ReconciliationFailed::ReconciliationFailed(std::vector<e1::InvariantFailure> f)
    : std::runtime_error([&f] {
          std::string m = "reconciliation failed:";
          for (const auto& x : f) m += " INV-" + std::to_string(x.invariant);
          return m;
      }()),
      failures_(std::move(f)) {}

const char* to_string(Reason r) {
    switch (r) {
        case Reason::OutOfStock: return "OUT_OF_STOCK";
        case Reason::InsufficientStock: return "INSUFFICIENT_STOCK";
        case Reason::PriceChanged: return "PRICE_CHANGED";
        case Reason::BelowMinOrder: return "BELOW_MIN_ORDER";
        case Reason::PoolNotOpen: return "POOL_NOT_OPEN";
    }
    return "POOL_NOT_OPEN";
}

namespace detail {

// ---- connection pool ---------------------------------------------------------------------------------------------

ConnectionPool::ConnectionPool(std::string conninfo, std::size_t max_connections)
    : conninfo_(std::move(conninfo)), max_(max_connections == 0 ? 1 : max_connections) {}

ConnectionPool::Lease ConnectionPool::acquire() {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this] { return !idle_.empty() || total_ < max_; });
    if (!idle_.empty()) {
        auto conn = std::move(idle_.back());
        idle_.pop_back();
        return Lease(*this, std::move(conn));
    }
    ++total_;
    lock.unlock();
    try {
        return Lease(*this, std::make_unique<pqxx::connection>(conninfo_));
    } catch (...) {
        lock.lock();
        --total_;
        cv_.notify_one();
        throw;
    }
}

void ConnectionPool::release(std::unique_ptr<pqxx::connection> conn) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (conn && conn->is_open()) idle_.push_back(std::move(conn));
    else --total_;
    cv_.notify_one();
}

// ---- shared SQL helpers ------------------------------------------------------------------------------------------

std::string utc_micros(const std::string& column) {
    return "to_char(" + column + " AT TIME ZONE 'UTC','YYYY-MM-DD\"T\"HH24:MI:SS.US\"Z\"')";
}

bool is_transient(const pqxx::sql_error& e) {
    const std::string& s = e.sqlstate();
    return s == "40001" || s == "40P01" || s == "55P03";   // serialization, deadlock, lock not available
}

void set_lock_timeout(pqxx::transaction_base& tx, int ms) {
    tx.exec0("SET LOCAL lock_timeout = " + std::to_string(ms < 1 ? 1 : ms));   // integer from config, not user input
}

PoolRow lock_pool(pqxx::transaction_base& tx, i64 pool_id) {
    const pqxx::result r = tx.exec_params(
        "SELECT status, total_stock_g, price_paise_per_kg, min_order_g, storage_location "
        "FROM pool WHERE pool_id = $1 FOR UPDATE", pool_id);
    if (r.empty()) throw DomainError("NOT_FOUND", "pool not found");
    PoolRow p;
    p.status = r[0]["status"].as<std::string>();
    p.total_stock_g = r[0]["total_stock_g"].as<i64>();
    p.price_paise_per_kg = r[0]["price_paise_per_kg"].as<i64>();
    p.min_order_g = r[0]["min_order_g"].as<i64>();
    p.storage_location = r[0]["storage_location"].as<std::string>();
    return p;
}

EventRef insert_event(pqxx::transaction_base& tx, i64 pool_id, const std::string& type, i64 actor_user_id) {
    const pqxx::row r = tx.exec_params1(
        "INSERT INTO event(pool_id, event_type, actor_user_id) VALUES ($1, $2, $3) "
        "RETURNING event_id, " + utc_micros("created_at") + " AS ts",
        pool_id, type, actor_user_id);
    return EventRef{r["event_id"].as<i64>(), r["ts"].as<std::string>()};
}

void seal_event(pqxx::transaction_base& tx, i64 pool_id, const EventRef& ev, const std::string& type,
                const e1::EventRows& rows) {
    const pqxx::result tail = tx.exec_params(
        "SELECT ledger_entry_id, sequence_no, entry_hash FROM ledger_entry WHERE pool_id = $1 "
        "ORDER BY sequence_no DESC LIMIT 1", pool_id);
    std::optional<i64> previous_id;
    i64 sequence_no = 1;
    std::string previous_hash = e1::kZeroHash;
    if (!tail.empty()) {
        previous_id = tail[0]["ledger_entry_id"].as<i64>();
        sequence_no = tail[0]["sequence_no"].as<i64>() + 1;
        previous_hash = tail[0]["entry_hash"].as<std::string>();
    }
    const e1::EntryInput in{pool_id, sequence_no, ev.event_id, type, ev.created_at_utc, rows, previous_hash};
    const std::string hash = e1::compute_entry_hash(in);
    tx.exec_params0(
        "INSERT INTO ledger_entry(event_id, pool_id, sequence_no, previous_entry_id, previous_hash, entry_hash) "
        "VALUES ($1, $2, $3, $4, $5, $6)",
        ev.event_id, pool_id, sequence_no, previous_id, previous_hash, hash);
}

void notify_event(pqxx::transaction_base& tx, i64 event_id, i64 pool_id) {
    tx.exec_params("SELECT pg_notify('ledger_events', $1)", std::to_string(event_id) + ":" + std::to_string(pool_id));
}

e1::PoolFacts load_pool_facts(pqxx::transaction_base& tx, i64 pool_id, std::optional<i64> only_sale_event) {
    e1::PoolFacts f;
    f.total_stock_g = tx.exec_params1("SELECT total_stock_g FROM pool WHERE pool_id = $1", pool_id)[0].as<i64>();

    for (const auto& row : tx.exec_params(
             "SELECT seller_id, remaining_g FROM pool_seller_balance WHERE pool_id = $1 ORDER BY seller_id", pool_id))
        f.balances.push_back({row[0].as<i64>(), row[1].as<i64>()});

    const pqxx::result inv = tx.exec_params(
        "SELECT resulting_stock_g FROM inventory_ledger WHERE pool_id = $1 ORDER BY inv_id DESC LIMIT 1", pool_id);
    if (!inv.empty()) {
        f.has_inventory_row = true;
        f.latest_inventory_stock_g = inv[0][0].as<i64>();
    }

    for (const auto& row : tx.exec_params(
             "SELECT DISTINCT ON (seller_id) seller_id, resulting_balance_g FROM ownership_ledger "
             "WHERE pool_id = $1 ORDER BY seller_id, own_id DESC", pool_id))
        f.latest_ownership.push_back({row[0].as<i64>(), row[1].as<i64>()});

    f.contributed_g = tx.exec_params1("SELECT coalesce(sum(quantity_g), 0)::bigint FROM contribution WHERE pool_id = $1",
                                      pool_id)[0].as<i64>();
    f.sold_g = tx.exec_params1("SELECT coalesce(sum(quantity_g), 0)::bigint FROM sale WHERE pool_id = $1",
                               pool_id)[0].as<i64>();

    std::map<i64, std::size_t> index;   // sale_id -> position in f.sales
    for (const auto& row : tx.exec_params(
             "SELECT sale_id, event_id, quantity_g, gross_paise FROM sale "
             "WHERE pool_id = $1 AND ($2::bigint IS NULL OR event_id = $2) ORDER BY sale_id", pool_id, only_sale_event)) {
        index[row[0].as<i64>()] = f.sales.size();
        f.sales.push_back({row[1].as<i64>(), row[2].as<i64>(), row[3].as<i64>(), {}, {}});
    }
    for (const auto& row : tx.exec_params(
             "SELECT p.sale_id, p.seller_id, p.quantity_allocated_g, p.payout_amount_paise FROM payout p "
             "JOIN sale s ON s.sale_id = p.sale_id "
             "WHERE s.pool_id = $1 AND ($2::bigint IS NULL OR s.event_id = $2) ORDER BY p.sale_id, p.seller_id",
             pool_id, only_sale_event)) {
        e1::SaleFacts& s = f.sales[index.at(row[0].as<i64>())];
        s.alloc_g.push_back({row[1].as<i64>(), row[2].as<i64>()});
        s.payout_paise.push_back({row[1].as<i64>(), row[3].as<i64>()});
    }
    return f;
}

void reconcile(pqxx::transaction_base& tx, i64 pool_id, std::optional<i64> sale_event) {
    const std::vector<e1::InvariantFailure> bad = e1::check_invariants(load_pool_facts(tx, pool_id, sale_event));
    if (!bad.empty()) throw ReconciliationFailed(bad);
}

}  // namespace detail
}  // namespace e2
