#include "e1/invariants.hpp"

#include <map>

#include "e1/wide_int.hpp"

namespace e1 {
namespace {
i128 sum_amounts(const std::vector<Share>& v) {
    i128 s = 0;
    for (const Share& x : v) s += x.amount;
    return s;
}
}  // namespace

std::vector<InvariantFailure> check_invariants(const PoolFacts& f) {
    std::vector<InvariantFailure> bad;

    // INV-1: pool stock = sum of farmer balances
    if (static_cast<i128>(f.total_stock_g) != sum_amounts(f.balances)) bad.push_back({1, 0});

    // INV-2: pool stock = newest inventory ledger row (a pool with no row must have no stock)
    const i64 ledger_stock = f.has_inventory_row ? f.latest_inventory_stock_g : 0;
    if (f.total_stock_g != ledger_stock) bad.push_back({2, 0});

    // INV-3: every farmer balance = that farmer's newest ownership ledger row (no row means 0)
    std::map<i64, i64> newest;
    for (const Share& o : f.latest_ownership) newest[o.id] = o.amount;
    std::map<i64, i64> balance;
    for (const Share& b : f.balances) balance[b.id] = b.amount;
    bool inv3 = true;
    for (const auto& b : balance) {
        const auto it = newest.find(b.first);
        if ((it == newest.end() ? 0 : it->second) != b.second) inv3 = false;
    }
    for (const auto& o : newest)
        if (o.second != 0 && balance.find(o.first) == balance.end()) inv3 = false;   // ledger row for a farmer with no balance
    if (!inv3) bad.push_back({3, 0});

    // INV-4: for every checked sale, sum of allocations = quantity and sum of payouts = gross
    for (const SaleFacts& s : f.sales)
        if (sum_amounts(s.alloc_g) != static_cast<i128>(s.quantity_g) ||
            sum_amounts(s.payout_paise) != static_cast<i128>(s.gross_paise))
            bad.push_back({4, s.event_id});

    // INV-5: stock is not negative, and nothing more was sold than was contributed
    if (f.total_stock_g < 0 || static_cast<i128>(f.sold_g) > static_cast<i128>(f.contributed_g)) bad.push_back({5, 0});
    return bad;
}

}  // namespace e1
