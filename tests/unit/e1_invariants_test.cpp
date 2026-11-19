// E1 reconciliation invariants INV-1..INV-5 on plain facts.
#include <gtest/gtest.h>

#include <algorithm>

#include "e1/invariants.hpp"

using namespace e1;

namespace {
// State after the worked example: stock 1,500,000 g, farmers 450k/600k/450k, one sale of 500 kg.
PoolFacts good() {
    PoolFacts f;
    f.total_stock_g = 1500000;
    f.balances = {{2, 450000}, {3, 600000}, {4, 450000}};
    f.has_inventory_row = true;
    f.latest_inventory_stock_g = 1500000;
    f.latest_ownership = {{2, 450000}, {3, 600000}, {4, 450000}};
    f.contributed_g = 2000000;
    f.sold_g = 500000;
    f.sales = {{6, 500000, 1250000, {{2, 150000}, {3, 200000}, {4, 150000}}, {{2, 375000}, {3, 500000}, {4, 375000}}}};
    return f;
}
bool fails(const PoolFacts& f, int inv) {
    const auto bad = check_invariants(f);
    return std::any_of(bad.begin(), bad.end(), [&](const InvariantFailure& x) { return x.invariant == inv; });
}
}  // namespace

TEST(E1Invariants, WorkedExampleReconciles) { EXPECT_TRUE(check_invariants(good()).empty()); }

TEST(E1Invariants, Inv1StockMustEqualSumOfBalances) {
    PoolFacts f = good();
    f.balances[0].amount += 1;
    EXPECT_TRUE(fails(f, 1));
}

TEST(E1Invariants, Inv2StockMustEqualNewestInventoryRow) {
    PoolFacts f = good();
    f.latest_inventory_stock_g = 1499999;
    EXPECT_TRUE(fails(f, 2));
    f = good();
    f.has_inventory_row = false;   // stock with no ledger at all
    EXPECT_TRUE(fails(f, 2));
}

TEST(E1Invariants, Inv3BalanceMustEqualNewestOwnershipRow) {
    PoolFacts f = good();
    f.latest_ownership[1].amount = 1;
    EXPECT_TRUE(fails(f, 3));
    f = good();
    f.latest_ownership.pop_back();   // farmer with a balance but no ledger row
    EXPECT_TRUE(fails(f, 3));
}

TEST(E1Invariants, Inv4SaleAllocationsAndPayoutsMustAddUp) {
    PoolFacts f = good();
    f.sales[0].alloc_g[0].amount -= 1;
    auto bad = check_invariants(f);
    ASSERT_EQ(bad.size(), 1u);
    EXPECT_EQ(bad[0].invariant, 4);
    EXPECT_EQ(bad[0].event_id, 6);
    f = good();
    f.sales[0].payout_paise[2].amount += 1;
    EXPECT_TRUE(fails(f, 4));
}

TEST(E1Invariants, Inv5NoNegativeStockAndNoOversell) {
    PoolFacts f = good();
    f.sold_g = 2000001;
    EXPECT_TRUE(fails(f, 5));
    f = good();
    f.total_stock_g = -1;
    EXPECT_TRUE(fails(f, 5));
}
