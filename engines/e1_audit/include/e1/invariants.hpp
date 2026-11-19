// E1: reconciliation invariants INV-1..INV-5 (master §6.3) over plain facts read by E2. No database access here.
#pragma once
#include <cstdint>
#include <vector>
#include "e1/allocation.hpp"

namespace e1 {

struct SaleFacts {
    i64 event_id;
    i64 quantity_g;
    i64 gross_paise;
    std::vector<Share> alloc_g;        // id = seller_id
    std::vector<Share> payout_paise;   // id = seller_id
};

struct PoolFacts {
    i64 total_stock_g;                       // pool.total_stock_g
    std::vector<Share> balances;             // pool_seller_balance: id = seller_id, amount = remaining_g
    bool has_inventory_row = false;          // does the pool have any inventory_ledger row?
    i64 latest_inventory_stock_g = 0;        // resulting_stock_g of the newest inventory_ledger row
    std::vector<Share> latest_ownership;     // newest ownership_ledger row per seller: amount = resulting_balance_g
    i64 contributed_g = 0;                   // sum of contributions
    i64 sold_g = 0;                          // sum of sales
    std::vector<SaleFacts> sales;            // sales to check for INV-4 (all of them, or just the new one)
};

struct InvariantFailure { int invariant; i64 event_id; };   // invariant 1..5; event_id of the sale for INV-4, else 0

// Returns every invariant that does not hold (empty = reconciled).
std::vector<InvariantFailure> check_invariants(const PoolFacts& facts);

}  // namespace e1
