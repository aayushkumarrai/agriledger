// Whole-history reads: reconciliation status and the hash-chain integrity check (spec §6.4).
#include <algorithm>
#include <map>

#include "internal.hpp"

namespace e2 {
using namespace detail;

ReconciliationStatus Engine::reconciliation(i64 pool_id) {
    auto lease = impl_->pool.acquire();
    pqxx::work tx(lease.get());
    tx.exec0("SET TRANSACTION ISOLATION LEVEL REPEATABLE READ READ ONLY");
    if (tx.exec_params("SELECT 1 FROM pool WHERE pool_id = $1", pool_id).empty()) throw DomainError("NOT_FOUND", "pool not found");
    ReconciliationStatus st;
    st.failures = e1::check_invariants(load_pool_facts(tx, pool_id, std::nullopt));
    st.ok = st.failures.empty();
    return st;
}

IntegrityResult Engine::integrity_check(i64 pool_id) {
    auto lease = impl_->pool.acquire();
    pqxx::work tx(lease.get());
    tx.exec0("SET TRANSACTION ISOLATION LEVEL REPEATABLE READ READ ONLY");   // one consistent snapshot of the whole history
    if (tx.exec_params("SELECT 1 FROM pool WHERE pool_id = $1", pool_id).empty()) throw DomainError("NOT_FOUND", "pool not found");

    // Rebuild every event's ledger rows from the tables with the same E1 serialiser the writers used.
    std::map<i64, e1::EventRows> rows;
    for (const auto& r : tx.exec_params("SELECT event_id, movement_type, delta_g, resulting_stock_g FROM inventory_ledger WHERE pool_id = $1 ORDER BY inv_id", pool_id))
        rows[r[0].as<i64>()].inventory.push_back({r[1].as<std::string>(), r[2].as<i64>(), r[3].as<i64>()});
    for (const auto& r : tx.exec_params("SELECT event_id, seller_id, delta_g, resulting_balance_g FROM ownership_ledger WHERE pool_id = $1 ORDER BY own_id", pool_id))
        rows[r[0].as<i64>()].ownership.push_back({r[1].as<i64>(), r[2].as<i64>(), r[3].as<i64>()});
    for (const auto& r : tx.exec_params("SELECT event_id, entry_type, seller_id, amount_paise FROM financial_ledger WHERE pool_id = $1 ORDER BY fin_id", pool_id))
        rows[r[0].as<i64>()].financial.push_back({r[1].as<std::string>(), r[2].is_null() ? std::nullopt : std::optional<i64>(r[2].as<i64>()), r[3].as<i64>()});
    for (const auto& r : tx.exec_params("SELECT event_id, field, old_value, new_value, changed_by FROM pool_change WHERE pool_id = $1", pool_id))
        rows[r[0].as<i64>()].change = e1::PoolChangeRow{r[1].as<std::string>(), r[2].as<std::string>(), r[3].as<std::string>(), r[4].as<i64>()};

    std::vector<e1::ChainEntry> chain;
    std::map<i64, i64> sequence_of_event;
    for (const auto& r : tx.exec_params(
             "SELECT l.sequence_no, l.event_id, e.event_type, " + utc_micros("e.created_at") + " AS ts, l.previous_hash, l.entry_hash "
             "FROM ledger_entry l JOIN event e ON e.event_id = l.event_id WHERE l.pool_id = $1 ORDER BY l.sequence_no", pool_id)) {
        const i64 event_id = r["event_id"].as<i64>();
        e1::EntryInput in{pool_id, r["sequence_no"].as<i64>(), event_id, r["event_type"].as<std::string>(),
                          r["ts"].as<std::string>(), rows[event_id], r["previous_hash"].as<std::string>()};
        sequence_of_event[event_id] = in.sequence_no;
        chain.push_back({std::move(in), r["entry_hash"].as<std::string>()});
    }

    IntegrityResult out;
    const e1::ChainResult verdict = e1::verify_chain(chain);
    out.checked_entries = chain.size();
    if (!verdict.ok) {
        out.ok = false;
        out.checked_entries = verdict.checked;
        out.first_broken = IntegrityBreak{verdict.first_broken->sequence_no, verdict.first_broken->event_type,
                                          e1::to_string(verdict.first_broken->reason)};
        return out;
    }

    // An event whose audit entry was removed from the end of the chain leaves no gap inside the chain.
    const pqxx::result unsealed = tx.exec_params(
        "SELECT event_type FROM event e WHERE pool_id = $1 AND NOT EXISTS (SELECT 1 FROM ledger_entry l WHERE l.event_id = e.event_id) "
        "ORDER BY event_id LIMIT 1", pool_id);
    if (!unsealed.empty()) {
        out.ok = false;
        out.first_broken = IntegrityBreak{static_cast<i64>(chain.size()) + 1, unsealed[0][0].as<std::string>(), "GAP"};
        return out;
    }

    // Finally the ledgers must reconcile with the current state (INV-1..5).
    const std::vector<e1::InvariantFailure> bad = e1::check_invariants(load_pool_facts(tx, pool_id, std::nullopt));
    if (!bad.empty()) {
        i64 seq = chain.empty() ? 0 : chain.back().input.sequence_no;
        std::string type = chain.empty() ? std::string("POOL_CREATED") : chain.back().input.event_type;
        if (bad[0].invariant == 4 && sequence_of_event.count(bad[0].event_id)) {
            seq = sequence_of_event[bad[0].event_id];
            type = "SALE";
        }
        out.ok = false;
        out.first_broken = IntegrityBreak{seq, type, "INVARIANT"};
    }
    out.head_hash = chain.empty() ? std::string() : chain.back().entry_hash;
    return out;
}

}  // namespace e2
