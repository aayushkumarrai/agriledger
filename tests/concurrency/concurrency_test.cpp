// Stage 4 concurrency tests: master §11 scenarios, 50 buyers x 20 runs with zero oversold, and the harness-only
// unprotected comparison. Needs the real database (scripts/test.sh).
#include <chrono>
#include <iostream>

#include "harness.hpp"

using namespace testdb;
using namespace std::chrono_literals;
using harness::RunResult;

namespace {
constexpr i64 kQuintal = 100000;   // grams
}

class ConcurrencyTest : public DbTest {
protected:
    e2::Engine eng{config(10)};
    World world{eng};
    e3::Options options() const { return e3::Options{4, 3, 10ms, 10000ms, 10000}; }
};

// 50 concurrent buyers, one small pool (10 quintal, 1 quintal each), repeated 20 times.
TEST_F(ConcurrencyTest, Protected_FiftyBuyersTwentyRuns_ZeroOversold) {
    for (int run = 0; run < 20; ++run) {
        const RunResult r = harness::run_protected(eng, world, 50, kQuintal, 10 * kQuintal, options());
        ASSERT_EQ(r.total(), 50) << "run " << run << ": accepted + rejected must equal the number of requests";
        ASSERT_EQ(r.busy, 0) << "run " << run;
        ASSERT_EQ(r.errors, 0) << "run " << run;
        ASSERT_EQ(r.accepted, 10) << "run " << run;
        ASSERT_EQ(r.rejected, 40) << "run " << run;
        ASSERT_FALSE(r.oversold()) << "run " << run;
        ASSERT_EQ(r.accepted_g, 10 * kQuintal);
        ASSERT_EQ(r.final_stock_g, r.start_stock_g - r.accepted_g) << "run " << run << ": stock must be start minus sold";
        ASSERT_GE(r.final_stock_g, 0);
        ASSERT_TRUE(r.reconciled) << "run " << run;
        ASSERT_TRUE(r.chain_ok) << "run " << run << ": the hash chain must verify after concurrent sales";
    }
}

// The same load against the unprotected harness-only counter. This is the O6 comparison: it must oversell.
TEST_F(ConcurrencyTest, Unprotected_HarnessOnlyCounter_Oversells) {
    int oversold_runs = 0;
    i64 worst_extra_g = 0;
    for (int run = 0; run < 20; ++run) {
        const RunResult r = harness::run_unprotected(50, kQuintal, 10 * kQuintal);
        ASSERT_EQ(r.errors, 0);
        ASSERT_EQ(r.accepted + r.rejected, 50) << "every request is still accepted or rejected";
        if (r.oversold()) { ++oversold_runs; worst_extra_g = std::max(worst_extra_g, r.accepted_g - r.start_stock_g); }
    }
    harness::drop_scratch();
    std::cout << "[ O6 ] unprotected counter oversold in " << oversold_runs << " of 20 runs (worst: " << worst_extra_g / kQuintal
              << " quintal sold beyond the stock); the protected path oversold in 0 of 20\n";
    EXPECT_GE(oversold_runs, 1) << "the unprotected mode is supposed to show overselling";
}

// Master §11 scenario 1: stock 10 kg, two buyers each ask for 8 kg. First in the queue wins, the second is refused.
TEST_F(ConcurrencyTest, Scenario_TenKgTwoBuyersEightKgEach) {
    const i64 pool = world.make_pool(2500, 1000);
    eng.record_contribution(pool, world.farmer[0], 10000, world.manager);
    e3::SaleQueue q(e3::sale_callbacks(eng), options());
    auto first = q.submit(world.order(pool, 8000));    // arrives first
    auto second = q.submit(world.order(pool, 8000));
    const e2::SaleOutcome a = first.get(), b = second.get();
    ASSERT_EQ(a.status, e2::SaleOutcome::Status::Accepted);
    EXPECT_EQ(a.receipt.stock_after_g, 2000);
    ASSERT_EQ(b.status, e2::SaleOutcome::Status::Rejected);
    EXPECT_EQ(b.reason, e2::Reason::InsufficientStock);
    EXPECT_EQ(b.stock_g, 2000);
    EXPECT_EQ(snapshot(pool).at("stock"), 2000);
}

// Scenario 2: stock 20 kg, both ask for 8 kg: both accepted, stock 4 kg.
TEST_F(ConcurrencyTest, Scenario_TwentyKgTwoBuyersEightKgEach) {
    const i64 pool = world.make_pool(2500, 1000);
    eng.record_contribution(pool, world.farmer[0], 20000, world.manager);
    e3::SaleQueue q(e3::sale_callbacks(eng), options());
    auto first = q.submit(world.order(pool, 8000));
    auto second = q.submit(world.order(pool, 8000));
    EXPECT_EQ(first.get().status, e2::SaleOutcome::Status::Accepted);
    EXPECT_EQ(second.get().status, e2::SaleOutcome::Status::Accepted);
    EXPECT_EQ(snapshot(pool).at("stock"), 4000);
    EXPECT_TRUE(eng.integrity_check(pool).ok);
}

// Scenario 3: a failure inside buyer 1's transaction rolls it back; buyer 2 is then served against unchanged stock.
TEST_F(ConcurrencyTest, Scenario_FailureInBuyerOnesTransaction_BuyerTwoSeesUnchangedStock) {
    const i64 pool = world.make_pool(2500, 1000);
    eng.record_contribution(pool, world.farmer[0], 10000, world.manager);
    const e2::SaleRequest buyer1 = world.order(pool, 8000);
    const e2::SaleRequest buyer2 = world.order(pool, 2000);
    const e2::SaleStepHook fail_after_ledgers = [](e2::SaleStep s) { if (s == e2::SaleStep::LedgersInserted) throw std::runtime_error("injected"); };
    e3::SaleQueue q(e3::sale_callbacks(eng, [&](const e2::SaleRequest& r) {
                        return r.request_id == buyer1.request_id ? eng.execute_sale(r, &fail_after_ledgers) : eng.execute_sale(r);
                    }), options());
    const auto before = snapshot(pool);
    auto f1 = q.submit(buyer1);
    auto f2 = q.submit(buyer2);
    EXPECT_THROW(f1.get(), std::runtime_error);
    const e2::SaleOutcome o2 = f2.get();
    ASSERT_EQ(o2.status, e2::SaleOutcome::Status::Accepted);
    EXPECT_EQ(o2.receipt.stock_after_g, before.at("stock") - 2000);   // buyer 1 changed nothing
    EXPECT_TRUE(eng.integrity_check(pool).ok);
}

// Two pools at once: each pool is protected on its own, and the lanes do not block each other.
TEST_F(ConcurrencyTest, TwoPoolsAtOnceEachStaySafe) {
    e2::Engine big(config(12));
    World w(big);
    RunResult a, b;
    std::thread ta([&] { a = harness::run_protected(big, w, 50, kQuintal, 10 * kQuintal, options()); });
    std::thread tb([&] { b = harness::run_protected(big, w, 50, kQuintal, 15 * kQuintal, options()); });
    ta.join(); tb.join();
    for (const RunResult* r : {&a, &b}) {
        EXPECT_EQ(r->total(), 50);
        EXPECT_EQ(r->busy + r->errors, 0);
        EXPECT_FALSE(r->oversold());
        EXPECT_EQ(r->final_stock_g, r->start_stock_g - r->accepted_g);
        EXPECT_TRUE(r->reconciled);
        EXPECT_TRUE(r->chain_ok);
    }
    EXPECT_EQ(a.accepted, 10);
    EXPECT_EQ(b.accepted, 15);
}

// E3 retries a transient E2 result up to 3 times, then reports BUSY; once the lock is free the sale works.
TEST_F(ConcurrencyTest, StalledPoolLock_RetriesThenBusy_ThenRecovers) {
    e2::Engine fast(config(4, 100));   // 100 ms lock timeout
    const i64 pool = world.make_worked_example_pool();
    e3::SaleQueue q(e3::sale_callbacks(fast), options());
    pqxx::connection holder(super_conninfo());
    pqxx::work hold(holder);
    hold.exec_params("SELECT 1 FROM pool WHERE pool_id = $1 FOR UPDATE", pool);   // an outside transaction keeps the pool locked
    const auto before = snapshot(pool);
    const auto t0 = std::chrono::steady_clock::now();
    const e2::SaleOutcome o = q.submit(world.order(pool, 500000)).get();
    const auto took = std::chrono::steady_clock::now() - t0;
    EXPECT_EQ(o.status, e2::SaleOutcome::Status::Busy);
    EXPECT_GE(took, 300ms);   // three attempts of at least the lock timeout each
    EXPECT_EQ(q.stats().retried, 2u);
    hold.abort();
    EXPECT_EQ(snapshot(pool), before);
    EXPECT_EQ(q.submit(world.order(pool, 500000)).get().status, e2::SaleOutcome::Status::Accepted);
}
